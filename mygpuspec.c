#include <stdio.h>
#include <unistd.h>

#include "mygpuspec.h"

int main(int argc, char * argv[])
{
  mygpuspec_context ctx;

  int blocsize = 92274688;

  ctx.No = 3;
  ctx.Np = 2;
  ctx.Nc = 88;
  ctx.Ntpb = blocsize / (2 * ctx.Np * ctx.Nc);
  ctx.Nts[0] = (1<<20);
  ctx.Nts[1] = (1<<3);
  ctx.Nts[2] = (1<<10);

  if(mygpuspec_initialize(&ctx)) {
    fprintf(stderr, "initialization failed\n");
    return 1;
  }

  printf("initialization succeeded\n");
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
