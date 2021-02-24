#ifndef _RAWSPEC_CALLBACK_H_
#define _RAWSPEC_CALLBACK_H_

#include <pthread.h>
#include "rawspec_fbutils.h"

typedef struct {
  int *fd; // Output file descriptors (one for each antenna) or socket (at most 1)
  int fd_ics; // Output file descriptor or socket
  unsigned int Nant; // Number of antenna, splitting Nf per fd
  char per_ant_out; // Flag to account for Nant
  unsigned int total_spectra;
  unsigned int total_packets;
  unsigned int total_bytes;
  uint64_t total_ns;
  double rate;
  int debug_callback;
  // No way to tell if output_thread is valid expect via separate flag
  int output_thread_valid;
  pthread_t output_thread;
  // Copies of values in rawspec_context
  // (useful for output threads)
  float * h_pwrbuf;
  size_t h_pwrbuf_size;
  float * h_icsbuf;
  unsigned int Nds;
  unsigned int Nf; // Number of fine channels (== Nc*Nts[i])
  // Filterbank header
  fb_hdr_t fb_hdr;
} callback_data_t;

#endif // _RAWSPEC_CALLBACK_H_
