#ifndef _RAWUTILS_H_
#define _RAWUTILS_H_

#include <stdint.h>
#include <sys/types.h>

typedef struct {
  int directio;
  size_t blocsize;
  unsigned int npol;
  unsigned int obsnchan;
  unsigned int nbits;
  int64_t pktidx; // TODO make uint64_t?
  double obsfreq;
  double obsbw;
  double tbin;
  double ra;  // hours
  double dec; // degrees
  double mjd;
  int beam_id; // -1 is unknown or single beam receiver
  int nbeam;   // -1 is unknown or single beam receiver
  unsigned int nants;
  char src_name[81];
  char telescop[81];
  off_t hdr_pos; // Offset of start of header
  size_t hdr_size; // Size of header in bytes (not including DIRECTIO padding)
} rawspec_raw_hdr_t;

// Multiple of 80 and 512
#define MAX_RAW_HDR_SIZE (25600)

#ifdef __cplusplus
extern "C" {
#endif

int32_t rawspec_raw_get_s32(const char * buf, const char * key, int32_t def);

uint32_t rawspec_raw_get_u32(const char * buf, const char * key, uint32_t def);

int64_t rawspec_raw_get_s64(const char * buf, const char * key, int64_t def);

uint64_t rawspec_raw_get_u64(const char * buf, const char * key, uint64_t def);

double rawspec_raw_get_dbl(const char * buf, const char * key, double def);

void rawspec_raw_get_str(const char * buf, const char * key, const char * def,
                 char * out, size_t len);

double rawspec_raw_dmsstr_to_d(char * dmsstr);

#define rawspec_raw_hmsstr_to_h(hmsstr) (rawspec_raw_dmsstr_to_d(hmsstr))

int rawspec_raw_header_size(char * hdr, size_t len, int directio);

// Parses rawspec related RAW header params from buf into raw_hdr.
void rawspec_raw_parse_header(const char * buf, rawspec_raw_hdr_t * raw_hdr);

// Reads obs params from fd.  On entry, fd is assumed to be at the start of a
// RAW header section.  On success, this function returns the file offset of
// the subsequent data block and the file descriptor `fd` will also refer to
// that location in the file.  On EOF, this function returns 0.  On failure,
// this function returns -1 and the location to which fd refers is undefined.
off_t rawspec_raw_read_header(int fd, rawspec_raw_hdr_t * raw_hdr);

#ifdef __cplusplus
}
#endif

#endif // _RAWUTILS_H_
