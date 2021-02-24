#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "rawspec_file.h"

int open_output_file(const char * dest, const char *stem, int output_idx)
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
    snprintf(fname, PATH_MAX, "%s/%s.rawspec.%04d.fil", dest, basename, output_idx);
  } else {
    snprintf(fname, PATH_MAX, "%s.rawspec.%04d.fil", stem, output_idx);
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

int open_output_file_per_antenna_and_write_header(callback_data_t *cb_data, const char * dest, const char *stem, int output_idx)
{
  char ant_stem[PATH_MAX+1];
  if(cb_data->per_ant_out){
    cb_data->fb_hdr.nchans /= cb_data->Nant;
  }
  for(int i = 0; i < (cb_data->per_ant_out ? cb_data->Nant : 1); i++){
    if(cb_data->per_ant_out){
      snprintf(ant_stem, PATH_MAX, "%s-ant%03d", stem, i);
    }
    else{
      snprintf(ant_stem, PATH_MAX, "%s", stem);
    }

    cb_data->fd[i] = open_output_file(dest, ant_stem, output_idx);
    if(cb_data->fd[i] == -1) {
      // If we can't open this output file, we probably won't be able to
      // open any more output files, so print message and bail out.
      fprintf(stderr, "cannot open output file, giving up\n");
      return 1; // Give up
    }

    // Write filterbank header to output file
    fb_fd_write_header(cb_data->fd[i], &cb_data->fb_hdr);
  }
  if(cb_data->per_ant_out){
    cb_data->fb_hdr.nchans *= cb_data->Nant;
  }
  return 0;
}

void * dump_file_thread_func(void *arg)
{
  callback_data_t * cb_data = (callback_data_t *)arg;

  if(cb_data->fd && cb_data->h_pwrbuf){
    if(cb_data->per_ant_out){
      size_t spectra_stride = cb_data->h_pwrbuf_size / (cb_data->Nds * sizeof(float));
      size_t pol_stride = spectra_stride / cb_data->fb_hdr.nifs;
      size_t ant_stride = pol_stride / cb_data->Nant;

      for(size_t k = 0; k < cb_data->Nds; k++){// Spectra out
        for(size_t j = 0; j < cb_data->fb_hdr.nifs; j++){// Npolout
          for(size_t i = 0; i < cb_data->Nant; i++){ 
            if(cb_data->fd[i] == -1){
              // Assume that the following file-descriptors aren't valid
              break;
            }
            write(cb_data->fd[i], cb_data->h_pwrbuf + i * ant_stride + j * pol_stride + k * spectra_stride, ant_stride * sizeof(float));
          }
        }
      }
    }
    else{
      write(cb_data->fd[0], cb_data->h_pwrbuf, cb_data->h_pwrbuf_size);
    }
  }
  
  if(cb_data->fd_ics && cb_data->h_icsbuf){
    write(cb_data->fd_ics, cb_data->h_icsbuf, cb_data->h_pwrbuf_size/cb_data->Nant);
  }

  // Increment total spectra counter for this output product
  cb_data->total_spectra += cb_data->Nds;

  return NULL;
}

void dump_file_callback(
    rawspec_context * ctx,
    int output_product,
    int callback_type)
{
  int i;
  int rc;
  callback_data_t * cb_data =
    &((callback_data_t *)ctx->user_data)[output_product];

  if(callback_type == RAWSPEC_CALLBACK_PRE_DUMP) {
    if(cb_data->output_thread_valid) {
      // Join output thread
      if((rc=pthread_join(cb_data->output_thread, NULL))) {
        fprintf(stderr, "pthread_join: %s\n", strerror(rc));
      }
      // Flag thread as invalid
      cb_data->output_thread_valid = 0;
    }
  } else if(callback_type == RAWSPEC_CALLBACK_POST_DUMP) {
#ifdef VERBOSE
    fprintf(stderr, "cb %d writing %lu bytes:",
        output_product, ctx->h_pwrbuf_size[output_product]);
    for(i=0; i<16; i++) {
      fprintf(stderr, " %02x",
          ((char *)ctx->h_pwrbuf[output_product])[i] & 0xff);
    }
    fprintf(stderr, "\n");
#endif // VERBOSE
    if((rc=pthread_create(&cb_data->output_thread, NULL,
                      dump_file_thread_func, cb_data))) {
      fprintf(stderr, "pthread_create: %s\n", strerror(rc));
    } else {
      cb_data->output_thread_valid = 1;
    }
  }
}
