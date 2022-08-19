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
#include <sys/sendfile.h>

#include "rawspec.h"
#include "rawspec_file.h"
#include "rawspec_socket.h"
#include "rawspec_version.h"
#include "rawspec_rawutils.h"
#include "rawspec_fbutils.h"
#include "fbh5_defs.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#ifndef DEBUG_CALLBACKS
#define DEBUG_CALLBACKS (0)
#endif

void show_more_info() {
    unsigned    hdf5_majnum, hdf5_minnum, hdf5_relnum;  // Version/release info for the HDF5 library
    char *p_hdf5_plugin_path;

    H5get_libversion(&hdf5_majnum, &hdf5_minnum, &hdf5_relnum);
    printf("HDF5 library version: %d.%d.%d\n", hdf5_majnum, hdf5_minnum, hdf5_relnum);
    p_hdf5_plugin_path = getenv("HDF5_PLUGIN_PATH");
    if(p_hdf5_plugin_path == NULL)
      printf("The HDF5 library plugin directory (default) is %s.\n", H5_DEFAULT_PLUGINDIR);
    else
      printf("The HDF5 library plugin directory (env) is %s.\n", p_hdf5_plugin_path);
    if (H5Zfilter_avail(FILTER_ID_BITSHUFFLE) <= 0) {
        printf("WARNING: Plugin bitshuffle is NOT available so compression is DISABLED!\n");
        printf("Please copy the bitshuffle plugin to the plugin directory.\n\n");
    } else
        printf("The bitshuffle plugin is available.\n\n");
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

static struct option long_opts[] = {
  {"ant",     1, NULL, 'a'},
  {"batch",   0, NULL, 'b'},
  {"dest",    1, NULL, 'd'},
  {"ffts",    1, NULL, 'f'},
  {"gpu",     1, NULL, 'g'},
  {"help",    0, NULL, 'h'},
  {"hdrs",    0, NULL, 'H'},
  {"ics",     1, NULL, 'i'},
  {"fbh5",    0, NULL, 'j'},
  {"nchan",   1, NULL, 'n'},
  {"outidx",  1, NULL, 'o'},
  {"pols",    1, NULL, 'p'},
  {"rate",    1, NULL, 'r'},
  {"schan",   1, NULL, 's'},
  {"splitant",0, NULL, 'S'},
  {"ints",    1, NULL, 't'},
  {"version", 0, NULL, 'v'},
  {"debug",   0, NULL, 'z'},
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
    "  -a, --ant=ANT          The 0-indexed antenna to exclusively process [-1]\n"
    "  -b, --batch=BC         Batch process BC coarse-channels at a time (1: auto, <1: disabled) [0]\n"
    "  -d, --dest=DEST        Destination directory or host:port\n"
    "  -f, --ffts=N1[,N2...]  FFT lengths [1048576, 8, 1024]\n"
    "  -g, --GPU=IDX          Select GPU device to use [0]\n"
    "  -H, --hdrs             Save headers to separate file\n"
    "  -i, --ics=W1[,W2...]   Output incoherent-sum (exclusively, unless with -S)\n"
    "                         specifying per antenna-weights or a singular, uniform weight\n"
    "  -j, --fbh5             Format output Filterbank files as FBH5 (.h5) instead of SIGPROC(.fil)\n"
    "  -n, --nchan=N          Number of coarse channels to process [all]\n"
    "  -o, --outidx=N         First index number for output files [0]\n"
    "  -p  --pols={1|4}[,...] Number of output polarizations [1]\n"
    "                         1=total power, 4=cross pols, -4=full stokes\n"
    "  -r, --rate=GBPS        Desired net data rate in Gbps [6.0]\n"
    "  -s, --schan=C          First coarse channel to process [0]\n"
    "  -S, --splitant         Split output into per antenna files\n"
    "  -t, --ints=N1[,N2...]  Spectra to integrate [51, 128, 3072]\n"
    "  -z, --debug            Turn on selected debug output\n"
    "\n"
    "  -h, --help             Show this message\n"
    "  -v, --version          Show version and exit\n\n"
    , bname
  );
  show_more_info();
}

int open_headers_file(const char * dest, const char *stem)
{
  int fd;
  const char * basename;
  char fname[PATH_MAX+1];

  // If dest is given and it's not empty
  if(dest && dest[0]) {
    // Look for last '/' in stem
    basename = strrchr(stem, '/');
    if(basename) {
      // If found, advance beyond it to first char of basename
      basename++;
    } else {
      // If not found, use stem as basename
      basename = stem;
    }
    snprintf(fname, PATH_MAX, "%s/%s.rawspec.headers", dest, basename);
  } else {
    snprintf(fname, PATH_MAX, "%s.rawspec.headers", stem);
  }
  fname[PATH_MAX] = '\0';
  fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0664);
  if(fd == -1) {
    perror(fname);
  } else {
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
  }
  return fd;
}

int main(int argc, char *argv[])
{
  int si; // Indexes the stems
  int fi; // Indexes the files for a given stem
  int bi; // Counts the blocks processed for a given file
  int i, j, k;
  void * pv;
  int fdin;
  int fdhdrs = -1;
  int next_stem = 0;
  int save_headers = 0;
  int per_ant_out = 0;
  unsigned int Nc;   // Number of coarse channels across the observation (possibly multi-antenna)
  unsigned int Ncpa;// Number of coarse channels per antenna
  unsigned int Np;   // Number of polarizations
  unsigned int Ntpb; // Number of time samples per block
  unsigned int Nbps; // Number of bits per sample
  uint64_t block_byte_length; // Compute the length once
  char expand4bps_to8bps; // Expansion flag
  int64_t pktidx0;
  int64_t pktidx;
  int64_t dpktidx;
  char fname[PATH_MAX+1];
  int opt;
  char * argv0;
  char * pchar;
  char * bfname;
  char * dest = NULL; // default output dest is same place as input stem
  char * ics_output_stem = NULL;
  rawspec_output_mode_t output_mode = RAWSPEC_FILE;
  char * dest_port = NULL; // dest port for network output
  int fdout;
  int open_flags;
  size_t bytes_read;
  size_t total_bytes_read;
  off_t pos;
  rawspec_raw_hdr_t raw_hdr;
  callback_data_t cb_data[MAX_OUTPUTS];
  rawspec_context ctx;
  int ant = -1;
  unsigned int schan = 0;
  unsigned int nchan = 0;
  unsigned int outidx = 0;
  int input_conjugated = -1;
  int only_output_ics = 0;

  // Selected dynamic debugging
  int flag_debugging = 0;

  // FBH5 fields
  int flag_fbh5_output = 0;

  // For net data rate rate calculations
  double rate = 6.0;
  double sum_inv_na;
  uint64_t total_packets = 0;
  uint64_t total_bytes = 0;
  uint64_t total_ns = 0;

  // Show librawspec version on startup
  printf("rawspec %s using librawspec %s and cuFFT %s\n", 
         STRINGIFY(RAWSPEC_VERSION), 
         get_librawspec_version(), 
         get_cufft_version());

  // Init rawspec context
  memset(&ctx, 0, sizeof(ctx));
  ctx.Npolout[0] = 1; // others will be set later

  // Exit status after mallocs have occured.
  int exit_status = 0;

  // Parse command line.
  argv0 = argv[0];
  while((opt=getopt_long(argc, argv, "a:b:d:f:g:HSjzs:i:n:o:p:r:t:hv", long_opts, NULL)) != -1) {
    switch (opt) {
      case 'h': // Help
        usage(argv0);
        return 0;
        break;

      case 'j': // FBH5 output format requested
        flag_fbh5_output = 1;
        break;

      case 'z': // Selected dynamic debugging
        flag_debugging = 1;
        break;

      case 'a': // Antenna selection to process
        ant = strtol(optarg, NULL, 0);
        break;

      case 'b': // Batch-channels
        ctx.Nbc = strtol(optarg, NULL, 0);
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
                "error: up to %d fine channel counts supported.\n", MAX_OUTPUTS);
            return 1;
          }
          ctx.Nts[i] = strtoul(pchar, NULL, 0);
        }
        // If no comma (i.e. single value)
        if(i==0) {
          ctx.Nts[0] = strtoul(optarg, NULL, 0);
        }
        break;

      case 'g': // GPU device to use
        ctx.gpu_index = strtol(optarg, NULL, 0);
        printf("using requested GPU: %d\n", ctx.gpu_index);
        break;

      case 'H': // Save headers
        save_headers = 1;
        break;

      case 'i': // Incoherent sum
        printf("writing output for incoherent sum over all antennas\n");
        only_output_ics = 1; // will get reset if also splitting antennas
        ctx.incoherently_sum = 1;
        ctx.Naws = 1;
        // Count number of 
        for(i=0; i < strlen(optarg); i++)
          ctx.Naws += optarg[i]==',';
        
        char *weight_end;
        ctx.Aws = malloc(ctx.Naws*sizeof(float));

        for(i=0; i < ctx.Naws; i++){
          ctx.Aws[i] = strtof(optarg, &weight_end);
          optarg = weight_end;
        }
        
        break;

      case 'n': // Number of coarse channels to process
        nchan = strtoul(optarg, NULL, 0);
        break;

      case 'o': // Index number for first output product file name
        outidx = strtoul(optarg, NULL, 0);
        break;

      case 'p': // Number of pol products to output
        for(i=0, pchar = strtok(optarg,",");
            pchar != NULL; i++, pchar = strtok(NULL, ",")) {
          if(i>=MAX_OUTPUTS){
            fprintf(stderr,
                "error: up to %d pol modes supported.\n", MAX_OUTPUTS);
            return 1;
          }
          ctx.Npolout[i] = strtoul(pchar, NULL, 0);
        }
        // If no comma (i.e. single value)
        if(i==0) {
          ctx.Npolout[0] = strtoul(optarg, NULL, 0);
        }
        break;

      case 'r': // Relative rate to send packets
        rate = strtod(optarg, NULL);
        break;

      case 's': // First coarse channel to process
        schan = strtoul(optarg, NULL, 0);
        break;

      case 'S': // Split output per antenna
        per_ant_out = 1;
        break;

      case 't': // Number of spectra to accumumate
        for(i=0, pchar = strtok(optarg,",");
            pchar != NULL; i++, pchar = strtok(NULL, ",")) {
          if(i>=MAX_OUTPUTS){
            fprintf(stderr,
                "error: up to %d integration counts supported.\n", MAX_OUTPUTS);
            return 1;
          }
          ctx.Nas[i] = strtoul(pchar, NULL, 0);
        }
        // If no comma (i.e. single value)
        if(i==0) {
          ctx.Nas[0] = strtoul(optarg, NULL, 0);
        }
        break;

      case 'v': // Version
        show_more_info();
        return 0;
        break;

      case '?': // Command line parsing error
      default:
        printf("Unknown CLI option '%c'\n", opt);
        usage(argv0);
        return 1;
        break;
    }
  }

  // Skip past option args
  argc -= optind;
  argv += optind;

  // If no stems given, print usage and exit
  if(argc == 0) {
    fprintf(stderr, "error: a file stem must be specified\n");
    usage(argv0);
    return 1;
  }

  // Currently, there are potential conflicts in running -i and -S concurrently.
  if(ctx.incoherently_sum == 1 && per_ant_out == 1) {
    fprintf(stderr, "PLEASE NOTE: Currently, there are potential conflicts in running -i and -S concurrently.\n");
    fprintf(stderr, "PLEASE NOTE: -S (split antennas) is being ignored.\n");
    per_ant_out = 0;
  }

  // If writing output files, show the format used
  if(output_mode == RAWSPEC_FILE) {
      if(flag_fbh5_output)
          printf("writing output files in FBH5 format\n");
      else
          printf("writing output files in SIGPROC Filterbank format\n");
  }

  // If schan is non-zero, nchan must be too
  if(schan != 0 && nchan == 0) {
    fprintf(stderr, "error: nchan must be non-zero if schan is non-zero\n");
    return 1;
  }

  // Saving headers is only supported for file output
  if(save_headers && output_mode != RAWSPEC_FILE) {
    fprintf(stderr,
        "warning: saving headers is only supported for file output\n");
    save_headers = 0;
  }

  // Validate user input
  for(i=0; i < MAX_OUTPUTS; i++) {
    // If both Nt and Na are zero, stop validating/counting
    if(ctx.Nts[i] == 0 && ctx.Nas[i] == 0) {
      break;
    } else if(ctx.Nts[i] ==0 || ctx.Nas[i] == 0) {
      // If only one of Nt or Ni are zero, error out
      fprintf(stderr,
          "error: must specify same number of FFT and integration lengths\n");
      return 1;
    };
  }
  // Remember number of output products specified
  ctx.No = i;

  if(ctx.No == 0) {
    printf("using default FFT and integration lengths\n");
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

  // Validate polout values
  for(i=0; i<ctx.No; i++) {
    if(ctx.Npolout[i] == 0 && i > 0) {
      // Copy value from previous output product
      ctx.Npolout[i] = ctx.Npolout[i-1];
    } else if(ctx.Npolout[i]!=1 && abs(ctx.Npolout[i])!=4) {
      fprintf(stderr,
          "error: number of output pols must be 1 or +/- 4\n");
      return 1;
    }

    // Full-pol mode is not supported for network output
    if(ctx.Npolout[i] != 1 && output_mode != RAWSPEC_FILE) {
      fprintf(stderr,
          "error: full-pol mode is not supported for network output\n");
      return 1;
    }
  }

  // Init user_data to be array of callback data structures
  ctx.user_data = &cb_data;

  // Zero-out the callback data sructures.
  // Turn on dynamic debugging if requested.
  for(i=0; i<ctx.No; i++) {
    memset(&cb_data[i], 0, sizeof(callback_data_t));
    cb_data[i].debug_callback = flag_debugging;
  }

  // Init pre-defined filterbank headers and save rate
  for(i=0; i<ctx.No; i++) {
    cb_data[i].fb_hdr.machine_id = 20;
    cb_data[i].fb_hdr.telescope_id = -1; // Unknown
    cb_data[i].fb_hdr.data_type = 1;
    cb_data[i].fb_hdr.nbeams =  1;
    cb_data[i].fb_hdr.ibeam  = -1; // Unknown or single pixel
    cb_data[i].fb_hdr.nbits  = 32;
    cb_data[i].fb_hdr.nifs   = abs(ctx.Npolout[i]);
    cb_data[i].rate          = rate;
    cb_data[i].Nant          = 1;

    // Init callback file descriptors to sentinal values
    cb_data[i].fd = malloc(sizeof(int));
    cb_data[i].fd[0] = -1;
    if(flag_fbh5_output) {
        cb_data[i].flag_fbh5_output = 1;
        cb_data[i].fbh5_ctx_ant = malloc(sizeof(fbh5_context_t));
        cb_data[i].fbh5_ctx_ant[0].active = 0;
    } else {
        cb_data[i].flag_fbh5_output = 0;
    }
  }

  // Set output mode specific callback function
  // and open socket if outputting over network.
  if(output_mode == RAWSPEC_FILE) {
    ctx.dump_callback = dump_file_callback;
  } else {
    ctx.dump_callback = dump_net_callback;

#if 1
    // Open socket and store for all output products
    cb_data[0].fd[0] = open_output_socket(dest, dest_port);
    if(cb_data[0].fd[0] == -1) {
      fprintf(stderr, "cannot open output socket, giving up\n");
      return 1; // Give up
    }
    // Share socket descriptor with other callbacks
    for(i=1; i<ctx.No; i++) {
      cb_data[i].fd[0] = cb_data[0].fd[0];
    }
#else
    for(i=0; i<ctx.No; i++) {
      cb_data[i].fd = open_output_socket(dest, dest_port);
      if(cb_data[i].fd == -1) {
        fprintf(stderr, "cannot open output socket %d, giving up\n", i);
        return 1; // Give up
      }
    }
#endif
  }

  // For each stem
  for(si=0; si<argc; si++) {
    printf("working stem: %s\n", argv[si]);
    if(ctx.incoherently_sum){
      if(ics_output_stem){
        free(ics_output_stem);
      }
      ics_output_stem = malloc(strlen(argv[si])+5);
      snprintf(ics_output_stem, strlen(argv[si])+5, "%s-ics", argv[si]);
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
      pos = rawspec_raw_read_header(fdin, &raw_hdr);
      if(pos <= 0) {
        if(pos == -1) {
          fprintf(stderr, "error getting obs params from %s\n", fname);
        } else {
          fprintf(stderr, "no data found in %s\n", fname);
        }
        close(fdin);
        break; // Goto next stem
      }

      // If first file for stem, check sizing
      if(fi == 0) {
        if(raw_hdr.nbeam != 0) {
          printf("Header has NBEAM (%d), which indicates that the data is that of a beam, overriding NANTS (%d) with 1.\n",
            raw_hdr.nbeam, raw_hdr.nants
          );
          raw_hdr.nants = 1;
        }

        // Verify that obsnchan is divisible by nants
        if(raw_hdr.obsnchan % raw_hdr.nants != 0) {
          fprintf(stderr, "bad obsnchan/nants: %u %% %u != 0\n",
              raw_hdr.obsnchan, raw_hdr.nants);
          close(fdin);
          break; // Goto next stem
        }

        // Calculate Ntpb and validate block dimensions
        Nc = raw_hdr.obsnchan;
        Ncpa = raw_hdr.obsnchan/raw_hdr.nants;
        Np = raw_hdr.npol;
        Nbps = raw_hdr.nbits;
        
        Ntpb = raw_hdr.blocsize / ((2 * Np * Nc * Nbps)/8);

        // First pktidx of first file
        pktidx0 = raw_hdr.pktidx;
        // Previous pktidx
        pktidx  = pktidx0;
        // Expected difference be between raw_hdr.pktidx and previous pktidx
        dpktidx = 0;

        if((2 * Np * Nc * Nbps)/8 * Ntpb != raw_hdr.blocsize) {
          printf("bad block geometry: 2*%upol*%uchan*%utpb*(%ubps/8) != %lu\n",
              Np, Nc, Ntpb, Nbps, raw_hdr.blocsize);
          close(fdin);
          break; // Goto next stem
        }

#ifdef VERBOSE
        fprintf(stderr, "BLOCSIZE = %lu\n", raw_hdr.blocsize);
        fprintf(stderr, "OBSNCHAN = %d\n",  raw_hdr.obsnchan);
        fprintf(stderr, "NANTS    = %d\n",  raw_hdr.nants);
        fprintf(stderr, "NBITS    = %d\n",  raw_hdr.nbits);
        fprintf(stderr, "DATATYPE = %s\n",  raw_hdr.float_data ? "FLOAT" : "INTEGER");
        fprintf(stderr, "NPOL     = %d\n",  raw_hdr.npol);
        fprintf(stderr, "OBSFREQ  = %g\n",  raw_hdr.obsfreq);
        fprintf(stderr, "OBSBW    = %g\n",  raw_hdr.obsbw);
        fprintf(stderr, "TBIN     = %g\n",  raw_hdr.tbin);
#endif // VERBOSE
        if(raw_hdr.nants > 1 && !(per_ant_out || ctx.incoherently_sum)){
          printf("NANTS = %d >1: Enabling --split-ant in lieu of neither --split-ant nor --ics flags.\n", raw_hdr.nants);
          per_ant_out = 1;
        }

        // If splitting output per antenna, re-alloc the fd array.
        if(per_ant_out) {
          if(output_mode == RAWSPEC_FILE){
            if(ant != -1){
              printf("Ignoring --ant %d option:\n\t", ant);
            }
            printf("Splitting output per %d antennas\n",
                raw_hdr.nants);
            // close previous
            for(i=0; i<ctx.No; i++) {
              if (cb_data[i].Nant != raw_hdr.nants){
                // For each antenna .....
                for(j=0; j<cb_data[i].Nant; j++) {
                  // If output file for antenna j is still open, close it.
                  if(flag_fbh5_output) {
                      if(cb_data[i].fbh5_ctx_ant[j].active) {
                        if(fbh5_close(&(cb_data[i].fbh5_ctx_ant[j]), cb_data[i].debug_callback) != 0)
                          exit_status = 1;
                      }
                  } else {
                      if(cb_data[i].fd[j] != -1) {
                        if(close(cb_data[i].fd[j]) < 0) {
                          fprintf(stderr, "SIGPROC-CLOSE-ERROR\n");
                          exit_status = 1;
                        }                      
                        cb_data[i].fd[j] = -1;
                      }
                  }
                }
                // Free all output file resources
                if(flag_fbh5_output) {
                    free(cb_data[i].fbh5_ctx_ant);
                }
                free(cb_data[i].fd);

                cb_data[i].per_ant_out = per_ant_out;
                // Re-init callback file descriptors to sentinal values
                // Memory is allocated by the output files are not yet open.
                if(flag_fbh5_output) {
                    cb_data[i].flag_fbh5_output = 1;
                    cb_data[i].fbh5_ctx_ant = malloc(sizeof(fbh5_context_t) * raw_hdr.nants);
                    for(j=0; j<raw_hdr.nants; j++) {
                        cb_data[i].fbh5_ctx_ant[j].active = 0;
                    }
                } else {
                    cb_data[i].flag_fbh5_output = 0;
                }
                cb_data[i].fd = malloc(sizeof(int)*raw_hdr.nants);
                for(j=0; j<raw_hdr.nants; j++){
                  cb_data[i].fd[j] = -1;
                }
              }
            }
          }
          else{
            printf("Ignoring --splitant flag in network mode\n");
          }
          if(only_output_ics){
            only_output_ics = 0;
          }
        }

        // If processing a specific antenna
        if(ant != -1 && !per_ant_out) {
          // Validate ant
          if(ant > raw_hdr.nants - 1 || ant < 0) {
            printf("bad antenna selection: ant <> {0, nants} (%u <> {0, %d})\n",
                ant, raw_hdr.nants);
            close(fdin);
            break; // Goto next stem
          }
          if(schan >= Ncpa) {
            printf("bad schan specification with antenna selection: "
                   "schan > antnchan {obsnchan/nants} (%u > %u {%d/%d})\n",
                schan, Ncpa, raw_hdr.obsnchan, raw_hdr.nants);
            close(fdin);
            break; // Goto next stem
          }

          // Set Nc to Ncpa and skip previous antennas
          printf("Selection of antenna %d equates to a starting channel of %d\n", ant, ant*Ncpa);
          schan += ant * Ncpa;
          Nc = Ncpa;
        }

        // If processing a subset of coarse channels
        if(nchan != 0) {
          // Validate schan and nchan
          if(ant == -1 && // no antenna selection
              (schan + nchan > Nc)) {

            printf("bad channel range: schan + nchan > obsnchan (%u + %u > %d)\n",
                schan, nchan, raw_hdr.obsnchan);
            close(fdin);
            break; // Goto next stem
          }
          else if(ant != -1 && // antenna selection
                 (schan + nchan > (ant + 1) * Ncpa)) {
            printf("bad channel range: schan + nchan > antnchan {obsnchan/nants} (%u + %u > %d {%d/%d})\n",
                schan - ant * Ncpa, nchan, Ncpa, raw_hdr.obsnchan, raw_hdr.nants);
            close(fdin);
            break; // Goto next stem
          }
          // Use nchan as Nc
          Nc = nchan;
        }

        // Determine if input is conjugated
        input_conjugated = (raw_hdr.obsbw < 0) ? 1 : 0;

        // If block dimensions or input conjugation have changed
        if(Nc != ctx.Nc || Np != ctx.Np || Nbps != ctx.Nbps || Ntpb != ctx.Ntpb
        || input_conjugated != ctx.input_conjugated) {
          // Cleanup previous block, if it has been initialized
          if(ctx.Ntpb != 0) {
            rawspec_cleanup(&ctx);
          }
          // Remember new dimensions and input conjugation
          ctx.Nant = raw_hdr.nants;
          ctx.Nc   = Nc;
          ctx.Np   = Np;
          ctx.Ntpb = Ntpb;
          ctx.Nbps = Nbps;
          ctx.input_conjugated = input_conjugated;
          ctx.float_data = raw_hdr.float_data;

          // Initialize for new dimensions and/or conjugation
          ctx.Nb = 0;           // auto-calculate
          ctx.Nb_host = 0;      // auto-calculate
          ctx.h_blkbufs = NULL; // auto-allocate
          if(rawspec_initialize(&ctx)) {
            fprintf(stderr, "rawspec initialization failed\n");
            return 1; // fixes issue #23
          } else {
            // printf("initialization succeeded for new block dimensions\n");
            block_byte_length = (2 * ctx.Np * ctx.Nc * ctx.Nbps)/8 * ctx.Ntpb;

            // The GPU supports only 8bit and 16bit sample bit-widths. The strategy
            // for handling 4bit samples is to expand them out to 8bits, and there-onwards 
            // use the expanded 8bit samples. The device side rawspec_initialize actually still
            // complains about the indication of the samples being 4bits. But the 
            // expand4bps_to8bps flag is used to call rawspec_copy_blocks_to_gpu_expanding_complex4,
            // leading to the samples being expanded before any device side computation happens
            // in rawspec_start_processing. The ctx.Nbps is left as 8.
            if (ctx.Nbps == 8 && Nbps == 4){
              printf("CUDA memory initialised for %d bits per sample,\n\t"
                     "will expand header specified %d bits per sample.\n", ctx.Nbps, Nbps);
              expand4bps_to8bps = 1;
            }

            // Copy fields from ctx to cb_data
            for(i=0; i<ctx.No; i++) {
              cb_data[i].h_pwrbuf = ctx.h_pwrbuf[i];
              cb_data[i].h_pwrbuf_size = ctx.h_pwrbuf_size[i];
              cb_data[i].h_icsbuf = ctx.h_icsbuf[i];
              cb_data[i].Nds = ctx.Nds[i];
              cb_data[i].Nf  = ctx.Nts[i] * ctx.Nc;
              if(flag_debugging > 0) {
                printf("output %d Nds = %u, Nf = %u\n", i, cb_data[i].Nds, cb_data[i].Nf);
              }
              cb_data[i].Nant = raw_hdr.nants;
            }
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

        // Open output filterbank files and write the header.
        for(i=0; i<ctx.No; i++) {
          // Update callback data based on raw params and Nts etc.
          // Same for all products
          cb_data[i].fb_hdr.telescope_id = fb_telescope_id(raw_hdr.telescop);
          cb_data[i].fb_hdr.src_raj = raw_hdr.ra;
          cb_data[i].fb_hdr.src_dej = raw_hdr.dec;
          cb_data[i].fb_hdr.tstart = raw_hdr.mjd;
          cb_data[i].fb_hdr.ibeam = raw_hdr.beam_id;
          strncpy(cb_data[i].fb_hdr.source_name, raw_hdr.src_name, 80);
          cb_data[i].fb_hdr.source_name[80] = '\0';
          strncpy(cb_data[i].fb_hdr.rawdatafile, bfname, 80);
          cb_data[i].fb_hdr.rawdatafile[80] = '\0';

          // Output product dependent
          // raw_hdr.obsnchan is total for all nants
          cb_data[i].fb_hdr.foff =
            raw_hdr.obsbw/(raw_hdr.obsnchan/raw_hdr.nants)/ctx.Nts[i];
          // This computes correct first fine channel frequency (fch1) for odd or even number of fine channels.
          // raw_hdr.obsbw is always for single antenna
          // raw_hdr.obsnchan is total for all nants
          cb_data[i].fb_hdr.fch1 = raw_hdr.obsfreq
            - raw_hdr.obsbw*((raw_hdr.obsnchan/raw_hdr.nants)-1)
                /(2*raw_hdr.obsnchan/raw_hdr.nants)
            - (ctx.Nts[i]/2) * cb_data[i].fb_hdr.foff
            + (schan % (raw_hdr.obsnchan/raw_hdr.nants)) * // Adjust for schan
                raw_hdr.obsbw / (raw_hdr.obsnchan/raw_hdr.nants);
          cb_data[i].fb_hdr.nfpc = ctx.Nts[i];  // Number of fine channels per coarse channel.
          cb_data[i].fb_hdr.nchans = ctx.Nc * ctx.Nts[i] / raw_hdr.nants; // Number of fine channels.
          cb_data[i].fb_hdr.tsamp = raw_hdr.tbin * ctx.Nts[i] * ctx.Nas[i]; // Time integration sampling rate in seconds.

          if(output_mode == RAWSPEC_FILE) {
            // Open one or more output files.
            // Handle both per-antenna output and single file output.
            if(!only_output_ics) {
              // Open nants=0 case or open all of the antennas.
              int retcode = open_output_file_per_antenna_and_write_header(&cb_data[i], 
                                                               dest, 
                                                               argv[si], 
                                                               outidx + i);
              if(retcode != 0)
                return 1; // give up
              if(cb_data->debug_callback)
                  printf("rawspec-main: open_output_file_per_antenna_and_write_header - successful\n");
            }
            // Handle ICS.
            if(ctx.incoherently_sum) {
              cb_data[i].fd_ics = open_output_file(&cb_data[i], 
                                                   dest, 
                                                   ics_output_stem, 
                                                   outidx + i,
                                                   /* ICS */ -1);
              if(cb_data[i].fd_ics == -1) {
                // If we can't open this output file, we probably won't be able to
                // open any more output files, so print message and bail out.
                fprintf(stderr, "cannot open output file, giving up\n");
                return 1; // Give up
              if(cb_data->debug_callback)
                  printf("rawspec-main: open_output_file - successful\n");
              }

              // Write filterbank header to SIGPROC output ICS file.
              // If FBH5, the header was already written by fbh5_open().
              if(! flag_fbh5_output) {
                fb_fd_write_header(cb_data[i].fd_ics, &cb_data[i].fb_hdr);
              }
            } // if(ctx.incoherently_sum)
          } // if(output_mode == RAWSPEC_FILE)
        } // for(i=0; i<ctx.No; i++)

        // Save header information if requested.
        if(save_headers) {
          // Open headers output file
          fdhdrs = open_headers_file(dest, argv[si]);
          if(fdhdrs == -1) {
            fprintf(stderr, "unable to save headers\n");
          }
        }

        // Output to socket initialisation.
        if(output_mode == RAWSPEC_NET) {
          // Apportion net data rate to output products proportional to their
          // data volume.  Interestingly, data volume is proportional to the
          // inverse of Na.  To apportion the total Gbps, we can calculate a
          // scaling factor for each output product:
          //
          //                                      1.0
          //     scaling_factor[j] = ----------------------------
          //                          Nas[j] * sum_i(1.0/Nas[i])
          sum_inv_na = 0;
          for(i=0; i<ctx.No; i++) {
            sum_inv_na += 1.0 / ctx.Nas[i];
          }
          for(i=0; i<ctx.No; i++) {
            // Calculate output rate for this output product
            cb_data[i].rate = rate / ctx.Nas[i] / sum_inv_na;
            fprintf(stderr, "output product %d data rate %6.3f Gbps\n",
                i, cb_data[i].rate);
          }
        }
      } // if first file

      // For all blocks in file
      for(;;) {
        // Save headers if requested (and headers output file was opened ok)
        if(save_headers && fdhdrs != -1) {
          // Copy header to headers file
          sendfile(fdhdrs, fdin, &raw_hdr.hdr_pos, raw_hdr.hdr_size);
        }

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
          } else if (raw_hdr.pktidx == pktidx ){
            printf("got null jump in pktidx: (%ld - %ld) == 0\n",
                   raw_hdr.pktidx, pktidx);
            // just skip this block
            break;
          }

          // Put in filler blocks of zeros
          while(raw_hdr.pktidx - pktidx != dpktidx) {
            // Increment pktidx to next missing value
            pktidx += dpktidx;

            // Fill block buffer with zeros
            memset(ctx.h_blkbufs[bi%ctx.Nb_host], 0, raw_hdr.blocsize);

#ifdef VERBOSE
            fprintf(stderr, "%3d %016lx:", bi, pktidx);
            fprintf(stderr, " -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --\n");
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
              if(rawspec_wait_for_completion(&ctx) != 0)
                return 1;
              rawspec_copy_blocks_to_gpu_and_start_processing(&ctx, ctx.Nb, expand4bps_to8bps, RAWSPEC_FORWARD_FFT);
            }

            // Increment block counter
            bi++;
          } // filler zero blocks
        } // irregular pktidx step

        // Seek past first schan channel
        lseek(fdin, (2 * ctx.Np * schan * Nbps)/8 * ctx.Ntpb, SEEK_CUR);

        // Read ctx.Nc coarse channels from this block
        bytes_read = read_fully(fdin,
                                ctx.h_blkbufs[bi % ctx.Nb_host],
                                (expand4bps_to8bps ? block_byte_length/2 : block_byte_length));

        // Seek past channels after schan+nchan
        lseek(fdin, (2 * ctx.Np * (raw_hdr.obsnchan-(schan+Nc)) * Nbps)/8 * ctx.Ntpb, SEEK_CUR);

        if(bytes_read == -1) {
          perror("read");
          next_stem = 1;
          break; // Goto next file
        } else if(bytes_read < (expand4bps_to8bps ? block_byte_length/2 : block_byte_length)) {
          fprintf(stderr, "incomplete block at EOF\n");
          next_stem = 1;
          break; // Goto next file
        }
        total_bytes_read += bytes_read;

#ifdef VERBOSE
        fprintf(stderr, "%3d %016lx:", bi, raw_hdr.pktidx);
        for(j=0; j<16; j++) {
          fprintf(stderr, " %02x", ctx.h_blkbufs[bi%ctx.Nb_host][j] & 0xff);
        }
        fprintf(stderr, "\n");
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
          if(rawspec_wait_for_completion(&ctx) != 0)
            return 1;
          rawspec_copy_blocks_to_gpu_and_start_processing(&ctx, ctx.Nb, expand4bps_to8bps, RAWSPEC_FORWARD_FFT);
        }

        // Remember pktidx
        pktidx = raw_hdr.pktidx;

        // Increment block index to next block (which may be in the next file)
        bi++;

        // Read obs params of next block
        pos = rawspec_raw_read_header(fdin, &raw_hdr);
        if(pos <= 0) {
          if(pos == -1) {
            fprintf(stderr, "error getting obs params from %s [%s]\n",
                    fname, strerror(errno));
          }
          break;
        }
        if(raw_hdr.nbeam != 0) {
          raw_hdr.nants = 1;
        }
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
        // Antennas
        for(j=0; j < (cb_data[i].per_ant_out ? cb_data[i].Nant : 1); j++) {
          if(flag_fbh5_output) {
              if(cb_data[i].fbh5_ctx_ant[j].active) {
                if(fbh5_close(&(cb_data[i].fbh5_ctx_ant[j]), cb_data[i].debug_callback) != 0)
                  exit_status = 1;
              }
          } else {
              if(cb_data[i].fd[j] != -1) {
                if(close(cb_data[i].fd[j]) < 0) {
                  fprintf(stderr, "SIGPROC-CLOSE-ERROR ant %d\n", j);
                  exit_status = 1;
                }
                cb_data[i].fd[j] = -1;
              }
           }
        }
        // ICS
        if(ctx.incoherently_sum) {
          if(flag_fbh5_output) {
              if(cb_data[i].fbh5_ctx_ics.active) {
                  if(fbh5_close(&(cb_data[i].fbh5_ctx_ics), cb_data[i].debug_callback) != 0)
                    exit_status = 1;
              }
          } else {
              if(cb_data[i].fd_ics != -1) {
                if(close(cb_data[i].fd_ics) < 0) {
                  fprintf(stderr, "SIGPROC-CLOSE-ERROR cb %d ics\n", i);
                  exit_status = 1;
                }
                cb_data[i].fd_ics = -1;
              }
          }
        } // ctx.incoherently_sum
      }
    }
  } // each stem

  // Final cleanup
  rawspec_cleanup(&ctx);
  if(ics_output_stem){
    free(ics_output_stem);
  }

  // Close sockets (or should-"never"-happen unclosed files)
  for(i=0; i<ctx.No; i++) {
    if(cb_data[i].fd[0] != -1) {
      close(cb_data[i].fd[0]);
      cb_data[i].fd[0] = -1;
    }
    free(cb_data[i].fd);
  }

  // Print stats
  for(i=0; i<ctx.No; i++) {
    printf("output product %d: %u spectra", i, cb_data[i].total_spectra);
    if(cb_data[i].total_packets > 0) {
      printf(" (%u packets, %.3f Gbps)", cb_data[i].total_packets,
         8.0 * cb_data[i].total_bytes / cb_data[i].total_ns);

      total_packets += cb_data[i].total_packets;
      total_bytes += cb_data[i].total_bytes;
      total_ns += cb_data[i].total_ns;
    }
    printf("\n");
  }

  if(total_ns > 0) {
    printf("combined total  : %lu packets, %.3f Gbps\n",
        total_packets, 8.0 * total_bytes / total_ns);
  }

  if(exit_status != 0)
    fprintf(stderr, "*** At least one error occured during processing!\n");
  return exit_status;
}
