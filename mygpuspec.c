#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include "mygpuspec.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

int main(int argc, char * argv[])
{
  int i;
  mygpuspec_context ctx;

  // Timing variables
  struct timespec ts_start, ts_stop;
  uint64_t elapsed_ns=0;

  int blocsize = 92274688;

  ctx.No = 3;
  ctx.Np = 2;
  ctx.Nc = 88;
  ctx.Ntpb = blocsize / (2 * ctx.Np * ctx.Nc);
  ctx.Nts[0] = (1<<20);
  ctx.Nts[1] = (1<<3);
  ctx.Nts[2] = (1<<10);
  ctx.Nb = 0;

  if(mygpuspec_initialize(&ctx)) {
    fprintf(stderr, "initialization failed\n");
    return 1;
  }
  printf("initialization succeeded\n");

  for(i=0; i<4; i++) {
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    mygpuspec_copy_blocks_to_gpu(&ctx);

    clock_gettime(CLOCK_MONOTONIC, &ts_stop);
    elapsed_ns = ELAPSED_NS(ts_start, ts_stop);

    printf("copied %u bytes in %.6f sec (%.3f GBps)\n",
        blocsize * ctx.Nb,
        elapsed_ns / 1e9,
        blocsize * ctx.Nb / (double)elapsed_ns);
  }

  printf("number of output products NOT processing: %d\n",
      mygpuspec_check_for_completion(&ctx));

  printf("waiting for completion...");
  fflush(stdout);
  mygpuspec_wait_for_completion(&ctx);
  printf("done\n");

  printf("sleeping for 10 seconds...");
  fflush(stdout);
  sleep(10);
  printf("done\n");

  printf("cleaning up...");
  fflush(stdout);
  mygpuspec_cleanup(&ctx);
  printf("done\n");

  return 0;
}
