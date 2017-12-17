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
#include <getopt.h>

#include "rawspec.h"
#include "rawspec_file.h"
#include "rawspec_socket.h"
#include "rawspec_callback.h"
#include "rawutils.h"
#include "fbutils.h"
#include "fitshead.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

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

static struct option long_opts[] = {
  {"help", 0, NULL, 'h'},
  {"ffts", 1, NULL, 'f'},
  {"ints", 1, NULL, 't'},
  {"dest", 1, NULL, 'd'},
  {0,0,0,0}
};

void usage(const char *argv0) {
  const char * bname = basename(argv0);
  // Should "never" happen
  if(!bname) {
    bname = argv0;
  }

  fprintf(stderr,
    "Usage: %s [options] STEM [...]\n"
    "\n"
    "Options:\n"
    "  -h, --help            Show this message\n"
    "  -f, --ffts=N1[,N2...] FFT lengths\n"
    "  -t, --ints=N1[,N2...] Spectra to integrate\n"
    "  -d, --dest=DEST       Destination directory or host:port\n"
    , bname
  );
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
  int opt;
  char * argv0;
  char * pchar;
  char * bfname;
  char * dest = "."; // default output destination is current directory
  rawspec_output_mode_t output_mode = RAWSPEC_FILE;
  char * dest_port = NULL; // dest port for network output
  int fdout;
  int open_flags;
  size_t bytes_read;
  size_t total_bytes_read;
  off_t pos;
  raw_hdr_t raw_hdr;
  callback_data_t cb_data[MAX_OUTPUTS];
  rawspec_context ctx;

  // Init rawspec context
  memset(&ctx, 0, sizeof(ctx));

  // Parse command line.
  argv0 = argv[0];
  while((opt=getopt_long(argc,argv,"hd:f:t:",long_opts,NULL))!=-1) {
    switch (opt) {
      case 'h': // Help
        usage(argv0);
        return 0;
        break;

      case 'd': // Output destination
        dest = optarg;
        // If dest contains at least one ':', it's HOST:PORT and we're
        // outputting over the network.
        pchar = strrchr(dest, ':');
        if(pchar) {
          // NUL terminate hostname, advance to port
          *pchar++ = '\0';
          dest_port = pchar;
          output_mode = RAWSPEC_NET;
        }
        break;

      case 'f': // Fine channel(s) per coarse channel
        for(i=0, pchar = strtok(optarg,",");
            pchar != NULL; i++, pchar = strtok(NULL, ",")) {
          if(i>=MAX_OUTPUTS){
            fprintf(stderr,
                "error: up to %d output products supported.\n", MAX_OUTPUTS);
            return 1;
          }
          ctx.Nts[i] = strtoul(pchar, NULL, 0);
        }
        break;

      case 't': // Number of spectra to accumumate
        for(i=0, pchar = strtok(optarg,",");
            pchar != NULL; i++, pchar = strtok(NULL, ",")) {
          if(i>=MAX_OUTPUTS){
            fprintf(stderr,
                "error: up to %d output products supported.\n", MAX_OUTPUTS);
            return 1;
          }
          ctx.Nas[i] = strtoul(pchar, NULL, 0);
        }
        break;

      case '?': // Command line parsing error
      default:
        return 1;
        break;
    }
  }

  // Skip past option args
  argc -= optind;
  argv += optind;

  // If no stems given, print usage and exit
  if(argc == 0) {
    usage(argv0);
    return 1;
  }

  // Validate user input
  for(i=0; i < MAX_OUTPUTS; i++) {
    // If both Nt and Na are zero, stop validating/counting
    if(ctx.Nts[i] == 0 && ctx.Nas[i] == 0) {
      break;
    } else if(ctx.Nts[i] ==0 || ctx.Nas[i] == 0) {
      // If only one of Nt or Ni are zero, error out
      fprintf(stderr,
          "error: must specify same number of FFTs and integration lengths\n");
      return 1;
    };
  }
  // Remember number of output products specified
  ctx.No = i;

  if(ctx.No == 0) {
    printf("using default FFTs and integration lengths\n");
    // These values are defaults for typical BL filterbank products.
    ctx.No = 3;
    // Number of fine channels per coarse channel (i.e. FFT size).
    ctx.Nts[0] = (1<<20);
    ctx.Nts[1] = (1<<3);
    ctx.Nts[2] = (1<<10);
    // Number of fine spectra to accumulate per dump.
    ctx.Nas[0] = 51;
    ctx.Nas[1] = 128;
    ctx.Nas[2] = 3072;
  }

  // Init user_data to be array of callback data structures
  ctx.user_data = &cb_data;

  // Zero-out the callback data sructures
  for(i=0; i<ctx.No; i++) {
    memset(&cb_data[i], 0, sizeof(callback_data_t));
  }

  // Init pre-defined filterbank headers
  for(i=0; i<ctx.No; i++) {
    cb_data[i].fb_hdr.machine_id = 20;
    cb_data[i].fb_hdr.telescope_id = 6; // GBT
    cb_data[i].fb_hdr.data_type = 1;
    cb_data[i].fb_hdr.nbeams =  1;
    cb_data[i].fb_hdr.ibeam  =  1;
    cb_data[i].fb_hdr.nbits  = 32;
    cb_data[i].fb_hdr.nifs   =  1;
  }

  // Init callback file descriptors to sentinal values
  for(i=0; i<ctx.No; i++) {
    cb_data[i].fd = -1;
  }

  // Set output mode specific callback function
  // and open socket if outputting over network.
  if(output_mode == RAWSPEC_FILE) {
    ctx.dump_callback = dump_file_callback;
  } else {
    ctx.dump_callback = dump_net_callback;

    // Open socket and store for all output products
    cb_data[0].fd = open_output_socket(dest, dest_port);
    if(cb_data[0].fd == -1) {
      fprintf(stderr, "cannot open output socket, giving up\n");
      return 1; // Give up
    }
    // Share socket descriptor with other callbacks
    for(i=1; i<ctx.No; i++) {
      cb_data[i].fd = cb_data[0].fd;
    }
  }

  // For each stem
  for(si=0; si<argc; si++) {
    printf("working stem: %s\n", argv[si]);

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
            printf("initialization succeeded for new block dimensions\n");
#if 0
            if(output_mode == RAWSPEC_NET) {
              set_socket_options(&ctx);
            }
#endif
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

          if(output_mode == RAWSPEC_FILE) {
            cb_data[i].fd = open_output_file(dest, argv[si], i);
            if(cb_data[i].fd == -1) {
              // If we can't open this output file, we probably won't be able to
              // open any more output files, so print message and bail out.
              fprintf(stderr, "cannot open output file, giving up\n");
              return 1; // Give up
            }

            // Write filterbank header to output file
            fb_fd_write_header(cb_data[i].fd, &cb_data[i].fb_hdr);
          }
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
        // break out of each file loop
        break;
      }
    } // each file for stem

    // Wait for GPU work to complete
    if(ctx.Nc) {
      rawspec_wait_for_completion(&ctx);
    }

    // Close output files
    if(output_mode == RAWSPEC_FILE) {
      for(i=0; i<ctx.No; i++) {
        if(cb_data[i].fd != -1) {
          close(cb_data[i].fd);
          cb_data[i].fd = -1;
        }
      }
    }
  } // each stem

  // Final cleanup
  rawspec_cleanup(&ctx);

  // Close sockets (or should-"never"-happen unclosed files)
  for(i=0; i<ctx.No; i++) {
    if(cb_data[i].fd != -1) {
      close(cb_data[i].fd);
      cb_data[i].fd = -1;
    }
  }

  // Print stats
  for(i=0; i<ctx.No; i++) {
    printf("output product %d: %u spectra", i, cb_data[i].total_spectra);
    if(cb_data[i].total_packets > 0) {
      printf(" (%u packets)", cb_data[i].total_packets);
    }
    printf("\n");
  }

  return 0;
}
