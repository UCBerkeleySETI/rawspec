#ifndef _RAWSPEC_CALLBACK_H_
#define _RAWSPEC_CALLBACK_H_

#include "fbutils.h"

typedef struct {
  int fd; // Output file descriptor or socket
  fb_hdr_t fb_hdr;
} callback_data_t;

#endif // _RAWSPEC_CALLBACK_H_
