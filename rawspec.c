#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "rawspec.h"
#include "rawutils.h"
#include "fbutils.h"
#include "fitshead.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

typedef struct {
  int fd; // Output file descriptor
  fb_hdr_t fb_hdr;
} callback_data_t;

int open_output_file(const char *stem, int output_idx)
{
  int fd;
  char fname[PATH_MAX+1];

  snprintf(fname, PATH_MAX, "%s.rawspec.%04d.fil", stem, output_idx);
  fname[PATH_MAX] = '\0';
  fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if(fd == -1) {
    perror(fname);
  }
  return fd;
}

void dump_callback(rawspec_context * ctx,
                   int output_product)
{
  int i;
#ifdef VERBOSE
  fprintf(stderr, "cb %d writing %lu bytes:",
      output_product, ctx->h_pwrbuf_size[output_product]);
  for(i=0; i<16; i++) {
    fprintf(stderr, " %02x", ((char *)ctx->h_pwrbuf[output_product])[i] & 0xff);
  }
  fprintf(stderr, "\n");
#endif // VERBOSE
  callback_data_t * cb_data = (callback_data_t *)ctx->user_data;
  write(cb_data[output_product].fd,
        ctx->h_pwrbuf[output_product],
        ctx->h_pwrbuf_size[output_product]);
}

// Reads `bytes_to_read` bytes from `fd` into the buffer pointed to by `buf`.
// Returns the total bytes read or -1 on error.  A non-negative return value
// will be less than `bytes_to_read` only of EOF is reached.
ssize_t read_fully(int fd, void * buf, size_t bytes_to_read)
{
  ssize_t bytes_read;
  ssize_t total_bytes_read = 0;

  while(bytes_to_read > 0) {
    bytes_read = read(fd, buf, bytes_to_read);
    if(bytes_read <= 0) {
      if(bytes_read == 0) {
        break;
      } else {
        return -1;
      }
    }
    buf += bytes_read;
    bytes_to_read -= bytes_read;
    total_bytes_read += bytes_read;
  }

  return total_bytes_read;
}

int main(int argc, char *argv[])
{
  int si; // Indexes the stems
  int fi; // Indexes the files for a given stem
  int bi; // Counts the blocks processed for a given file
  int i;
int j, k;
char tmp[16];
  void * pv;
  int fdin;
  int next_stem;
  unsigned int Nc;   // Number of coarse channels
  unsigned int Np;   // Number of polarizations
  unsigned int Ntpb; // Number of time samples per block
  int64_t pktidx0;
  int64_t pktidx;
  int64_t dpktidx;
  char fname[PATH_MAX+1];
  char * bfname;
  int fdout;
  int open_flags;
  size_t bytes_read;
  size_t total_bytes_read;
  off_t pos;
  raw_hdr_t raw_hdr;
  callback_data_t cb_data[MAX_OUTPUTS];
  rawspec_context ctx;

  if(argc<2) {
    fprintf(stderr, "usage: %s STEM [...]\n", argv[0]);
    return 1;
  }

  // Init block sizing fields to 0
  ctx.Nc   = 0;
  ctx.Np   = 0;
  ctx.Ntpb = 0;

  // Init integration parameters
  ctx.No = 3;
  ctx.Nts[0] = (1<<20);
  ctx.Nts[1] = (1<<3);
  ctx.Nts[2] = (1<<10);
  // Number of fine spectra to accumulate per dump.  These values are defaults
  // for typical BL filterbank products.
  ctx.Nas[0] = 51;
  ctx.Nas[1] = 128;
  ctx.Nas[2] = 3072;

  // Init user_data to be array of callback data structures
  ctx.user_data = &cb_data;
  ctx.dump_callback = dump_callback;

  // Init pre-defined filterbank headers
  for(i=0; i<ctx.No; i++) {
    memset(&cb_data[i].fb_hdr, 0, sizeof(fb_hdr_t));
    cb_data[i].fb_hdr.machine_id = 20;
    cb_data[i].fb_hdr.telescope_id = 6; // GBT
    cb_data[i].fb_hdr.data_type = 1;
    cb_data[i].fb_hdr.nbeams =  1;
    cb_data[i].fb_hdr.ibeam  =  1;
    cb_data[i].fb_hdr.nbits  = 32;
    cb_data[i].fb_hdr.nifs   =  1;
  }

  // For each stem
  for(si=1; si<argc; si++) {
    printf("working stem: %s\n", argv[si]);

    for(i=0; i<ctx.No; i++) {
      fdout = open_output_file(argv[si], i);
      if(fdout == -1) {
        return 1; // Give up
      }
      cb_data[i].fd = fdout;
    }

    // bi is the block counter for the entire sequence of files for this stem.
    // Note that bi is the count of contiguous blocks that are fed to the GPU.
    // If the input file has missing blocks (based on PKTIDX gaps), bi will
    // still count through those missing blocks.
    bi = 0;

    // For each file from stem
    for(fi=0; /* until break */; fi++) {
      // Build next input file name
      snprintf(fname, PATH_MAX, "%s.%04d.raw", argv[si], fi);
      fname[PATH_MAX] = '\0';
      bfname = basename(fname);

      printf("opening file: %s", fname);
      fdin = open(fname, O_RDONLY);
      if(fdin == -1) {
        printf(" [%s]\n", strerror(errno));
        break; // Goto next stem
      }
      printf("\n");
      posix_fadvise(fdin, 0, 0, POSIX_FADV_SEQUENTIAL);

      // Read obs params
      pos = raw_read_header(fdin, &raw_hdr);
      if(pos <= 0) {
        if(pos == -1) {
          fprintf(stderr, "error getting obs params from %s\n", fname);
        } else {
          fprintf(stderr, "no data found in %s\n", fname);
        }
        close(fdin);
        next_stem = 1;
        break; // Goto next stem
      }

      // If first file for stem, check sizing
      if(fi == 0) {
        // Calculate Ntpb and validate block dimensions
        Nc = raw_hdr.obsnchan;
        Np = raw_hdr.npol;
        Ntpb = raw_hdr.blocsize / (2 * Np * Nc);

        // First pktidx of first file
        pktidx0 = raw_hdr.pktidx;
        // Previous pktidx
        pktidx  = pktidx0;
        // Expected difference be between raw_hdr.pktidx and previous pktidx
        dpktidx = 0;

        if(2 * Np * Nc * Ntpb != raw_hdr.blocsize) {
          printf("bad block geometry: 2*%d*%d*%u != %lu\n",
              Np, Nc, Ntpb, raw_hdr.blocsize);
          close(fdin);
          break; // Goto next stem
        }

#ifdef VERBOSE
        printf("BLOCSIZE = %lu\n", raw_hdr.blocsize);
        printf("OBSNCHAN = %d\n",  raw_hdr.obsnchan);
        printf("NPOL     = %d\n",  raw_hdr.npol);
        printf("OBSFREQ  = %g\n",  raw_hdr.obsfreq);
        printf("OBSBW    = %g\n",  raw_hdr.obsbw);
        printf("TBIN     = %g\n",  raw_hdr.tbin);
#endif // VERBOSE

        // If block dimensions have changed
        if(Nc != ctx.Nc || Np != ctx.Np || Ntpb != ctx.Ntpb) {
          // Cleanup previous block, if it has been initialized
          if(ctx.Ntpb != 0) {
            rawspec_cleanup(&ctx);
          }
          // Remember new dimensions
          ctx.Nc   = Nc;
          ctx.Np   = Np;
          ctx.Ntpb = Ntpb;

          // Initialize for new dimensions
          ctx.Nb = 0;
          ctx.h_blkbufs = NULL;
          if(rawspec_initialize(&ctx)) {
            fprintf(stderr, "rawspec initialization failed\n");
            close(fdin);
            break;
          } else {
            printf("initializtion succeeded for new block dimensions\n");
          }
        } else {
          // Same as previous stem, just reset for new integration
          printf("resetting integration buffers for new stem\n");
          rawspec_reset_integration(&ctx);
        }

        // Update filterbank headers based on raw params and Nts etc.
        for(i=0; i<ctx.No; i++) {
          // Same for all products
          cb_data[i].fb_hdr.src_raj = raw_hdr.ra;
          cb_data[i].fb_hdr.src_dej = raw_hdr.dec;
          cb_data[i].fb_hdr.tstart = raw_hdr.mjd;
          strncpy(cb_data[i].fb_hdr.source_name, raw_hdr.src_name, 80);
          cb_data[i].fb_hdr.source_name[80] = '\0';
          strncpy(cb_data[i].fb_hdr.rawdatafile, bfname, 80);
          cb_data[i].fb_hdr.rawdatafile[80] = '\0';
          // Output product dependent
          cb_data[i].fb_hdr.foff = raw_hdr.obsbw/raw_hdr.obsnchan/ctx.Nts[i];
          cb_data[i].fb_hdr.fch1 =
            raw_hdr.obsfreq - raw_hdr.obsbw/2 + cb_data[i].fb_hdr.foff/2;
          cb_data[i].fb_hdr.nchans = ctx.Nc * ctx.Nts[i];
          cb_data[i].fb_hdr.tsamp = raw_hdr.tbin * ctx.Nts[i] * ctx.Nas[i];

          // Write filterbank header to output file
          fb_fd_write_header(cb_data[i].fd, &cb_data[i].fb_hdr);
        }

      } // if first file

      // For all blocks in file
      for(;;) {
        // Lazy init dpktidx as soon as possible
        if(dpktidx == 0 && raw_hdr.pktidx > pktidx) {
          dpktidx = raw_hdr.pktidx - pktidx;
        }

        // Handle cases were the current pktidx is not the expected distance
        // from the previous pktidx.
        if(raw_hdr.pktidx - pktidx != dpktidx) {
          // Cannot go backwards or forwards by non-multiple of dpktidx
          if(raw_hdr.pktidx < pktidx) {
            printf("got backwards jump in pktidx: %ld -> %ld\n",
                   pktidx, raw_hdr.pktidx);
            // Give up on this stem and go to next stem
            next_stem = 1;
            break;
          } else if((raw_hdr.pktidx - pktidx) % dpktidx != 0) {
            printf("got misaligned jump in pktidx: (%ld - %ld) %% %ld != 0\n",
                   raw_hdr.pktidx, pktidx, dpktidx);
            // Give up on this stem and go to next stem
            next_stem = 1;
            break;
          }

          // Put in filler blocks of zeros
          while(raw_hdr.pktidx - pktidx != dpktidx) {
            // Increment pktidx to next missing value
            pktidx += dpktidx;

            // Fill block buffer with zeros
            memset(ctx.h_blkbufs[bi%ctx.Nb], 0, raw_hdr.blocsize);

#ifdef VERBOSE
            printf("%3d %016lx:", bi, pktidx);
            printf(" -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --\n");
#endif // VERBOSE

            // If this is the last block of an input buffer, start processing
            if(bi % ctx.Nb == ctx.Nb - 1) {
#ifdef VERBOSE
              fprintf(stderr, "block %3b buf 0: ", bi);
              for(j=0; j<16; j++) {
                fprintf(stderr, " %02x", ctx.h_blkbufs[0][j] & 0xff);
              }
              fprintf(stderr, "\n");
#endif // VERBOSE
              rawspec_wait_for_completion(&ctx);
              rawspec_copy_blocks_to_gpu(&ctx, 0, 0, ctx.Nb);
              rawspec_start_processing(&ctx, RAWSPEC_FORWARD_FFT);
            }

            // Increment block counter
            bi++;
          } // filler zero blocks
        } // irregular pktidx step

        bytes_read = read_fully(fdin,
                                ctx.h_blkbufs[bi % ctx.Nb],
                                raw_hdr.blocsize);
        if(bytes_read == -1) {
          perror("read");
          next_stem = 1;
          break;
        } else if(bytes_read < raw_hdr.blocsize) {
          fprintf(stderr, "incomplete block at EOF\n");
          next_stem = 1;
          break;
        }
        total_bytes_read += bytes_read;

#ifdef VERBOSE
        printf("%3d %016lx:", bi, raw_hdr.pktidx);
        for(j=0; j<16; j++) {
          printf(" %02x", ctx.h_blkbufs[bi%ctx.Nb][j] & 0xff);
        }
        printf("\n");
#endif // VERBOSE

        // If this is the last block of an input buffer, start processing
        if(bi % ctx.Nb == ctx.Nb - 1) {
#ifdef VERBOSE
          fprintf(stderr, "block %3d buf 0: ", bi);
          for(j=0; j<16; j++) {
            fprintf(stderr, " %02x", ctx.h_blkbufs[0][j] & 0xff);
          }
          fprintf(stderr, "\n");
#endif // VERBOSE
          rawspec_wait_for_completion(&ctx);
          rawspec_copy_blocks_to_gpu(&ctx, 0, 0, ctx.Nb);
          rawspec_start_processing(&ctx, RAWSPEC_FORWARD_FFT);
        }

        // Remember pktidx
        pktidx = raw_hdr.pktidx;

        // Read obs params of next block
        pos = raw_read_header(fdin, &raw_hdr);
        if(pos <= 0) {
          if(pos == -1) {
            fprintf(stderr, "error getting obs params from %s [%s]\n",
                    fname, strerror(errno));
          }
          break;
        }

        bi++;
      } // For each block

      // Done with input file
      close(fdin);

      // If skipping to next stem
      if(next_stem) {
        next_stem = 0;
        break;
      }
    } // each file for stem

    // Wait for GPU work to complete
    if(ctx.Nc) {
      rawspec_wait_for_completion(&ctx);
    }

    // Close output files
    for(i=0; i<ctx.No; i++) {
      close(cb_data[i].fd);
    }
  } // each stem

  // Final cleanup
  rawspec_cleanup(&ctx);

  return 0;
}
