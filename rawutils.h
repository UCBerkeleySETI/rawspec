#ifndef _RAWUTILS_H_
#define _RAWUTILS_H_

#include <stdint.h>
#include <sys/types.h>

typedef struct {
  int directio;
  size_t blocsize;
  unsigned int npol;
  unsigned int obsnchan;
  int64_t pktidx; // TODO make uint64_t?
  double obsfreq;
  double obsbw;
  double tbin;
} raw_hdr_t;

// Multiple of 80 and 512
#define MAX_RAW_HDR_SIZE (25600)

#ifdef __cplusplus
extern "C" {
#endif

int raw_get_int(const char * buf, const char * key, int def);

int raw_get_s64(const char * buf, const char * key, int64_t def);

int raw_get_u64(const char * buf, const char * key, uint64_t def);

double raw_get_dbl(const char * buf, const char * key, double def);

int raw_header_size(char * hdr, size_t len, int directio);

// Reads obs params from fd.  On entry, fd is assumed to be at the start of a
// RAW header section.  On success, this function returns the file offset of
// the subsequent data block and the file descriptor `fd` will also refer to
// that location in the file.  On EOF, this function returns 0.  On failure,
// this function returns -1 and the location to which fd refers is undefined.
off_t raw_read_header(int fd, raw_hdr_t * raw_hdr);

#ifdef __cplusplus
}
#endif

#endif // _RAWUTILS_H_
