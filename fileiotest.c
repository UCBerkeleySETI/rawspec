#define _GNU_SOURCE 1

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "mygpuspec.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

// Test I/O speeds for various combinations of "tricks"
// 1. No direct I/O, no mmap
// 1. No direct I/O, mmap
// 2. Direct I/O, no mmap
// 3. Direct I/O, mmap

void do_read(mygpuspec_context *ctx, int fd, size_t blocsize)
{
  int i;
  size_t bytes_read = 0;

  // Timing variables
  struct timespec ts_start, ts_stop;
  uint64_t elapsed_ns=0;

  clock_gettime(CLOCK_MONOTONIC, &ts_start);

  for(i=0; i<ctx->Nb; i++) {
    bytes_read += read(fd, ctx->h_blkbufs[i], blocsize);
  }

  clock_gettime(CLOCK_MONOTONIC, &ts_stop);
  elapsed_ns = ELAPSED_NS(ts_start, ts_stop);

  printf("read %lu bytes in %.6f sec (%.3f GBps)\n",
         bytes_read,
         elapsed_ns / 1e9,
         bytes_read / (double)elapsed_ns);

  if(bytes_read != ctx->Nb * blocsize) {
    printf("not all blocks fully populated\n");
  }
}

void do_mmap(mygpuspec_context *ctx, int fd, size_t blocsize)
{
  int i;

  // Timing variables
  struct timespec ts_start, ts_stop;
  uint64_t elapsed_ns=0;

  clock_gettime(CLOCK_MONOTONIC, &ts_start);

  for(i=0; i<ctx->Nb; i++) {
    if(mmap(ctx->h_blkbufs[i], blocsize, PROT_READ,
         MAP_PRIVATE | MAP_FIXED | MAP_POPULATE,
         fd, (i+4)*blocsize) == MAP_FAILED) {
      perror("mmap");
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &ts_stop);
  elapsed_ns = ELAPSED_NS(ts_start, ts_stop);

  printf("mmap'd %lu bytes in %.6f sec (%.3f GBps)\n",
         ctx->Nb*blocsize,
         elapsed_ns / 1e9,
         ctx->Nb*blocsize / (double)elapsed_ns);

  for(i=0; i<ctx->Nb; i++) {
    munmap(ctx->h_blkbufs[i], blocsize);
  }

}

int main(int argc, char *argv[])
{
  int fd;
  int open_flags;
  size_t bytes_read;
  mygpuspec_context ctx;

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
  if(mygpuspec_initialize(&ctx)) {
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
  }

  do_mmap(&ctx, fd, blocsize);

  return 0;
}
