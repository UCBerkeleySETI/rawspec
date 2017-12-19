#ifndef _RAWSPEC_CALLBACK_H_
#define _RAWSPEC_CALLBACK_H_

#include "rawspec_fbutils.h"

typedef struct {
  int fd; // Output file descriptor or socket
  unsigned int total_spectra;
  unsigned int total_packets;
  fb_hdr_t fb_hdr;
} callback_data_t;

#endif // _RAWSPEC_CALLBACK_H_
