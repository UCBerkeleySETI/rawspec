#ifndef _RAWSPEC_CALLBACK_H_
#define _RAWSPEC_CALLBACK_H_

#include <pthread.h>
#include "rawspec_fbutils.h"

typedef struct {
  int fd; // Output file descriptor or socket
  unsigned int total_spectra;
  unsigned int total_packets;
  double rate;
  // No way to tell if output_thread is valid expect via separate flag
  int output_thread_valid;
  pthread_t output_thread;
  // Copies of values in rawspec_context
  // (useful for output threads)
  float * h_pwrbuf;
  size_t h_pwrbuf_size;
  unsigned int Nds;
  // Filterbank header
  fb_hdr_t fb_hdr;
} callback_data_t;

#endif // _RAWSPEC_CALLBACK_H_
