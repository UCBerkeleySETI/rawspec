#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "rawspec.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

void
dump_callback(rawspec_context * ctx, int output_product, int callback_type)
{
  printf("cb %d\n", output_product);
}

int main(int argc, char * argv[])
{
  int i;
  int j;
  rawspec_context ctx = {0};

  // Timing variables
  struct timespec ts_start, ts_stop;
  uint64_t elapsed_ns=0;

  int blocsize = 92274688;
  size_t nfine;

  ctx.No = 4;
  ctx.Np = 2;
  ctx.Nc = 88;
  ctx.Nbps = 8;
  if(argc > 1) {
    ctx.Nbps = strtoul(argv[1], NULL, 0);
    if(ctx.Nbps == 0) {
      ctx.Nbps = 8;
    }
  }
  printf("using %u bits per sample\n", ctx.Nbps);
  ctx.Ntpb = blocsize / (2 * ctx.Np * ctx.Nc * (ctx.Nbps/8));
  ctx.Nts[0] = (1<<20);
  ctx.Nts[1] = (1<<3);
  ctx.Nts[2] = (1<<10);
  ctx.Nts[3] = (1<<10);
  // One dump per output product
  ctx.Nas[0] = (1<<(20 - 20));
  ctx.Nas[1] = (1<<(20 -  3));
  ctx.Nas[2] = (1<<(20 - 10));
  ctx.Nas[3] = (1<<(20 - 10));
  // Auto-calculate Nb/Nb_host and let library manage input block buffers
  ctx.Nb = 0;
  ctx.Nb_host = 0;
  ctx.h_blkbufs = NULL;
  // Dump callback
  ctx.dump_callback = dump_callback;
  // Pol modes for each output product
  ctx.Npolout[0] = 1;
  ctx.Npolout[1] = 1;
  ctx.Npolout[2] = 4;
  ctx.Npolout[3] = -4;

  ctx.gpu_index = 0;
  ctx.input_conjugated = 1;

  // Initialize
  if(rawspec_initialize(&ctx)) {
    fprintf(stderr, "initialization failed\n");
    return 1;
  }
  printf("initialization succeeded, RAWSPEC_BLOCSIZE=%u\n",
         RAWSPEC_BLOCSIZE(&ctx));

  // Setup input data
  for(i=0; i<ctx.Nb_host; i++) {
    memset(ctx.h_blkbufs[i], 0, blocsize);
  }
  // Set sample 8 of pol 0 to (1+0j), in block Nb_host-1
  // Note that for 16-bit samples, this will really be (127/32767 + 0j)
  ctx.h_blkbufs[ctx.Nb_host-1][(8*ctx.Np*2/*complex*/)*(ctx.Nbps/8)] = 127;
  // Set sample 9 of pol 1 to (0+1j), in block Nb_host-1
  // Note that for 16-bit samples, this will really be (0 + 127j/32767)
  ctx.h_blkbufs[ctx.Nb_host-1][(9*ctx.Np*2/*complex*/+3)*(ctx.Nbps/8)] = 127;

  // Salt the output buffers (to detect whether they are not fully written)
  for(i=0; i<ctx.No; i++) {
    memset(ctx.h_pwrbuf[i], 0x55, ctx.h_pwrbuf_size[i]);
  }

  for(i=0; i<4; i++) {
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    rawspec_copy_blocks_to_gpu(&ctx, 0, 0, ctx.Nb);

    clock_gettime(CLOCK_MONOTONIC, &ts_stop);
    elapsed_ns = ELAPSED_NS(ts_start, ts_stop);

    printf("copied %u bytes in %.6f sec (%.3f GBps)\n",
           blocsize * ctx.Nb,
           elapsed_ns / 1e9,
           blocsize * ctx.Nb / (double)elapsed_ns);
  }

  printf("starting processing\n");

  for(i=0; i<4; i++) {
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    rawspec_start_processing(&ctx, -1);
    rawspec_wait_for_completion(&ctx);

    clock_gettime(CLOCK_MONOTONIC, &ts_stop);

    printf("processed %u blocks in %.3f ms\n", ctx.Nb,
           ELAPSED_NS(ts_start, ts_stop) / 1e6);
  }

  printf("processing done\n");

  for(i=0; i<ctx.No; i++) {
    nfine = ctx.Nc * ctx.Nts[i];
    for(j=0; j<16; j++) {
      if(ctx.Npolout[i] == 1) {
        printf("output product %d chan %d %f\n", i, j, ctx.h_pwrbuf[i][j]);
      } else {
        printf("output product %d chan %d %f %f %f %f\n", i, j,
            ctx.h_pwrbuf[i][        j],
            ctx.h_pwrbuf[i][1*nfine+j],
            ctx.h_pwrbuf[i][2*nfine+j],
            ctx.h_pwrbuf[i][3*nfine+j]);
      }
    }
  }

  // For checking mempry usage with nvidia-smi
  printf("sleeping for 10 seconds...");
  fflush(stdout);
  sleep(10);
  printf("done\n");

  printf("cleaning up...");
  fflush(stdout);
  rawspec_cleanup(&ctx);
  printf("done\n");

  return 0;
}
