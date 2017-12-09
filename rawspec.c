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
#include "fitshead.h"

typedef struct {
  int directio;
  size_t blocsize;
  unsigned int npol;
  unsigned int obsnchan;
  int64_t pktidx; // TODO make uint64_t?
  double obsfreq;
  double obsbw;
  double tbin;
} obs_params_t;

// Multiple of 80 and 512
#define MAX_HDR_SIZE (25600)

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

int get_int(const char * buf, const char * key, int def)
{
	char tmpstr[48];
	int value;
	if (hgeti4(buf, key, &value) == 0) {
		if (hgets(buf, key, 48, tmpstr) == 0) {
			value = def;
		} else {
			value = strtol(tmpstr, NULL, 0);
		}
	}

	return value;
}

int get_s64(const char * buf, const char * key, int64_t def)
{
	char tmpstr[48];
	int64_t value;
	if (hgeti8(buf, key, &value) == 0) {
		if (hgets(buf, key, 48, tmpstr) == 0) {
			value = def;
		} else {
			value = strtoll(tmpstr, NULL, 0);
		}
	}

	return value;
}

int get_u64(const char * buf, const char * key, uint64_t def)
{
	char tmpstr[48];
	uint64_t value;
	if (hgetu8(buf, key, &value) == 0) {
		if (hgets(buf, key, 48, tmpstr) == 0) {
			value = def;
		} else {
			value = strtoull(tmpstr, NULL, 0);
		}
	}

	return value;
}

double get_dbl(const char * buf, const char * key, double def)
{
	char tmpstr[48];
	double value;
	if (hgetr8(buf, key, &value) == 0) {
		if (hgets(buf, key, 48, tmpstr) == 0) {
			value = def;
		} else {
			value = strtod(tmpstr, NULL);
		}
	}

	return value;
}

int header_size(char * hdr, size_t len, int directio)
{
  int i;

  // Loop over the 80-byte records
  for(i=0; i<len; i += 80) {
    // If we found the "END " record
    if(!strncmp(hdr+i, "END ", 4)) {
      //printf("header_size: found END at record %d\n", i);
      // Move to just after END record
      i += 80;
      // Move past any direct I/O padding
      if(directio) {
        i += (MAX_HDR_SIZE - i) % 512;
      }
      return i;
    }
  }
  return 0;
}

// Reads obs params from fd.  On entry, fd is assumed to be at the start of a
// RAW header section.  On success, this function returns the file offset of
// the subsequent data block and the file descriptor `fd` will also refer to
// that location in the file.  On EOF, this function returns 0.  On failure,
// this function returns -1 and the location to which fd refers is undefined.
off_t read_obs_params(int fd, obs_params_t * obs_params)
{
  int i;
  char hdr[MAX_HDR_SIZE];
  int hdr_size;
  off_t pos = lseek(fd, 0, SEEK_CUR);
  //printf("ROP: pos=%lu\n", pos);

  // Read header (plus some data, probably)
  hdr_size = read(fd, hdr, MAX_HDR_SIZE);

  if(hdr_size < 80) {
    return 0;
  }

  obs_params->blocsize = get_int(hdr, "BLOCSIZE", 0);
  obs_params->npol     = get_int(hdr, "NPOL",     0);
  obs_params->obsnchan = get_int(hdr, "OBSNCHAN", 0);
  obs_params->obsfreq  = get_dbl(hdr, "OBSFREQ",  0.0);
  obs_params->obsbw    = get_dbl(hdr, "OBSBW",    0.0);
  obs_params->tbin     = get_dbl(hdr, "TBIN",     0.0);
  obs_params->directio = get_int(hdr, "DIRECTIO", 0);
  obs_params->pktidx   = get_int(hdr, "PKTIDX",  -1);

  if(obs_params->blocsize ==  0
  || obs_params->obsnchan ==  0
  || obs_params->obsfreq  ==  0.0
  || obs_params->obsbw    ==  0.0
  || obs_params->tbin     ==  0.0
  || obs_params->npol     ==  0
  || obs_params->pktidx   == -1) {
    return -1;
  }
  // 4 is the number of possible cross pol products
  if(obs_params->npol == 4) {
    // 2 is the actual number of polarizations present
    obs_params->npol = 2;
  }

  // Get actual size of header (plus any padding)
  hdr_size = header_size(hdr, hdr_size, obs_params->directio);
  //printf("ROP: hdr=%lu\n", hdr_size);

  // Seek forward from original position past header (and any padding)
  pos = lseek(fd, pos + hdr_size, SEEK_SET);
  //printf("ROP: seek=%ld\n", pos);

  return pos;
}

// Returns 0 on success
int do_mmap(rawspec_context *ctx, int fd, size_t blocsize)
{
  int i;
  size_t file_size;
  int num_blocks;
  size_t total_bytes_read = 1;

  // Timing variables
  struct timespec ts_start, ts_stop;
  uint64_t elapsed_ns=0;

  file_size = lseek(fd, 0, SEEK_END);
  num_blocks = file_size / blocsize;

  clock_gettime(CLOCK_MONOTONIC, &ts_start);

  for(i=0; i<num_blocks; i++) {
    if(mmap(ctx->h_blkbufs[i % ctx->Nb], blocsize, PROT_READ,
            MAP_PRIVATE | MAP_FIXED | MAP_POPULATE,
            fd, i*blocsize) == MAP_FAILED) {
      perror("mmap");
      return 1;
    }
    total_bytes_read += blocsize;
  }

  clock_gettime(CLOCK_MONOTONIC, &ts_stop);
  elapsed_ns = ELAPSED_NS(ts_start, ts_stop);

  perror("mmap");

  printf("mmap'd %lu bytes in %.6f sec (%.3f GBps)\n",
         total_bytes_read,
         elapsed_ns / 1e9,
         total_bytes_read / (double)elapsed_ns);

  for(i=0; i<ctx->Nb; i++) {
    munmap(ctx->h_blkbufs[i], blocsize);
  }
}

int main(int argc, char *argv[])
{
	int s; // Indexes the stems
  int i; // Indexes the files for a given stem
  int b; // Counts the blocks processed for a given file
int j, k;
char tmp[16];
  int fdin;
  int next_stem;
  unsigned int Nc;   // Number of coarse channels
  unsigned int Np;   // Number of polarizations
  unsigned int Ntpb; // Number of time samples per block
  int64_t pktidx0;
  int64_t pktidx;
  int64_t dpktidx;
  char fname[PATH_MAX+1];
  int fdout[MAX_OUTPUTS];
  int open_flags;
  size_t bytes_read;
  obs_params_t obs_params;
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
  // One dump per output product
  ctx.Nas[0] = (1<<(20 - 20));
  ctx.Nas[1] = (1<<(20 -  3));
  ctx.Nas[2] = (1<<(20 - 10));

  // For each stem
  for(s=1; s<argc; s++) {
    printf("working stem: %s\n", argv[s]);

    // For each file from stem
    for(i=0; /* until break */; i++) {
      // Build next input file name
      snprintf(fname, PATH_MAX, "%s.%04d.raw", argv[s], i);
      fname[PATH_MAX] = '\0';

      printf("opening file: %s", fname);
      fdin = open(fname, O_RDONLY);
      if(fdin == -1) {
        printf(" [%s]\n", strerror(errno));
        break; // Goto next stem
      }
      printf("\n");

      // Read obs params
      if(read_obs_params(fdin, &obs_params) == -1) {
        fprintf(stderr, "error getting obs params from %s\n", fname);
        close(fdin);
        break; // Goto next stem
      }

      // If first file for stem, check sizing
      if(i == 0) {
        // Calculate Ntpb and validate block dimensions
        Nc = obs_params.obsnchan;
        Np = obs_params.npol;
        Ntpb = obs_params.blocsize / (2 * Np * Nc);

        // First pktidx of first file
        pktidx0 = obs_params.pktidx;
        // Previous pktidx
        pktidx  = pktidx0;
        // Expected difference be between obs_params.pktidx and previous pktidx
        dpktidx = 0;

        if(2 * Np * Nc * Ntpb != obs_params.blocsize) {
          printf("bad block geometry: 2*%d*%d*%u != %lu\n",
              Np, Nc, Ntpb, obs_params.blocsize);
          close(fdin);
          break; // Goto next stem
        }

#ifdef VERBOSE
        printf("BLOCSIZE = %lu\n", obs_params.blocsize);
        printf("OBSNCHAN = %d\n",  obs_params.obsnchan);
        printf("NPOL     = %d\n",  obs_params.npol);
        printf("OBSFREQ  = %g\n",  obs_params.obsfreq);
        printf("OBSBW    = %g\n",  obs_params.obsbw);
        printf("TBIN     = %g\n",  obs_params.tbin);
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
          }
        } else {
          // Same as previous stem, just reset for new integration
          rawspec_reset_integration(&ctx);
        }
      } // if first file

      // For all blocks in file
      for(b=0; /* until break */; b++) {
        // Lazy init dpktidx as soon as possible
        if(dpktidx == 0 && obs_params.pktidx != pktidx) {
          dpktidx = obs_params.pktidx - pktidx;
        }

        // Handle cases were the current pktidx is not the expected distance
        // from the previous pktidx.
        if(obs_params.pktidx - pktidx != dpktidx) {
          // Cannot go backwards or forwards by non-multiple of dpktidx
          if(obs_params.pktidx < pktidx) {
            printf("got backwards jump in pktidx: %ld -> %ld\n",
                   pktidx, obs_params.pktidx);
            // Give up on this stem and go to next stem
            next_stem = 1;
            break;
          } else if((obs_params.pktidx - pktidx) % dpktidx != 0) {
            printf("got misaligned jump in pktidx: (%ld - %ld) %% %ld != 0\n",
                   obs_params.pktidx, pktidx, dpktidx);
            // Give up on this stem and go to next stem
            next_stem = 1;
            break;
          }

          // Put in filler blocks of zeros
          while(obs_params.pktidx - pktidx != dpktidx) {
            // TODO memset
            printf("%3d %016lx:", b, obs_params.pktidx);
            printf(" -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --\n");

            b++;
            pktidx += dpktidx;
          }
        }

        // TODO mmap block, but for now just printf first few blocks
        read(fdin, tmp, 16);
        lseek(fdin, -16, SEEK_CUR);
        printf("%3d %016lx:", b, obs_params.pktidx);
        for(j=0; j<16; j++) {
          printf(" %02x", tmp[j] & 0xff);
        }
        printf("\n");

        // seek past data block
        lseek(fdin, obs_params.blocsize, SEEK_CUR);

        // Remember pktidx
        pktidx = obs_params.pktidx;

        // Read obs params of next block
        if(read_obs_params(fdin, &obs_params) <= 0) {
          if(read_obs_params(fdin, &obs_params) == -1) {
            fprintf(stderr, "error getting obs params from %s [%s]\n",
                    fname, strerror(errno));
          }
          break;
        }
      } // For each block

      // Done with input file
      close(fdin);

      // If skipping to next stem
      if(next_stem) {
        next_stem = 0;
        break;
      }
    } // each file for stem
  } // each stem

  return 0;
}

#if 0
  int blocsize = 92274688;

  ctx.No = 3;
  ctx.Np = 2;
  ctx.Nc = 88;
  ctx.Ntpb = blocsize / (2 * ctx.Np * ctx.Nc);
  ctx.Nts[0] = (1<<20);
  ctx.Nts[1] = (1<<3);
  ctx.Nts[2] = (1<<10);
  // One dump per output product
  ctx.Nas[0] = (1<<(20 - 20));
  ctx.Nas[1] = (1<<(20 -  3));
  ctx.Nas[2] = (1<<(20 - 10));
  // Auto-calculate Nb
  ctx.Nb = 0;
  if(argc<2) {
    fprintf(stderr, "usage: %s FILENAME\n", argv[0]);
    return 1;
  }

  open_flags = O_RDONLY;

  if(argc>2 && strstr(argv[2], "direct")) {
    printf("using Direct I/O\n");
    open_flags |= O_DIRECT; 
  }
  
  // Initialize
  if(rawspec_initialize(&ctx)) {
    fprintf(stderr, "initialization failed\n");
    return 1;
  }
  printf("initialization succeeded\n");

  // Open file
  fd = open(argv[1], open_flags);
  if(fd == -1) {
    perror("open");
    return 1;
  }
  printf("file open succeeded\n");

  if(argc>2 && strstr(argv[2], "read")) {
    do_read(&ctx, fd, blocsize);
  } else if(argc>2 && strstr(argv[2], "memcpy")) {
    do_memcpy(&ctx, fd, blocsize);
  } else {
    do_mmap(&ctx, fd, blocsize);
  }

  return 0;
}
#endif
