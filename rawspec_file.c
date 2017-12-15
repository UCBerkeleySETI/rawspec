#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "rawspec_file.h"
#include "rawspec_callback.h"

int open_output_file(const char * dest, const char *stem, int output_idx)
{
  int fd;
  char fname[PATH_MAX+1];

  snprintf(fname, PATH_MAX, "%s/%s.rawspec.%04d.fil", dest, stem, output_idx);
  fname[PATH_MAX] = '\0';
  fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if(fd == -1) {
    perror(fname);
  }
  return fd;
}

void dump_file_callback(rawspec_context * ctx, int output_product)
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
