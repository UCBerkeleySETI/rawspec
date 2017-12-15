#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "rawutils.h"
#include "fitshead.h"

int raw_get_int(const char * buf, const char * key, int def)
{
  char tmpstr[48];
  int value;
  if (hgeti4(buf, key, &value) == 0) {
    if (hgets(buf, key, 48, tmpstr) == 0) {
      value = def;
    } else {
      value = strtol(tmpstr, NULL, 0);
    }
  }

  return value;
}

int raw_get_s64(const char * buf, const char * key, int64_t def)
{
  char tmpstr[48];
  int64_t value;
  if (hgeti8(buf, key, &value) == 0) {
    if (hgets(buf, key, 48, tmpstr) == 0) {
      value = def;
    } else {
      value = strtoll(tmpstr, NULL, 0);
    }
  }

  return value;
}

int raw_get_u64(const char * buf, const char * key, uint64_t def)
{
  char tmpstr[48];
  uint64_t value;
  if (hgetu8(buf, key, &value) == 0) {
    if (hgets(buf, key, 48, tmpstr) == 0) {
      value = def;
    } else {
      value = strtoull(tmpstr, NULL, 0);
    }
  }

  return value;
}

double raw_get_dbl(const char * buf, const char * key, double def)
{
  char tmpstr[48];
  double value;
  if (hgetr8(buf, key, &value) == 0) {
    if (hgets(buf, key, 48, tmpstr) == 0) {
      value = def;
    } else {
      value = strtod(tmpstr, NULL);
    }
  }

  return value;
}

void raw_get_str(const char * buf, const char * key, const char * def,
                 char * out, size_t len)
{
  if (hgets(buf, key, len, out) == 0) {
    strncpy(out, def, len);
    out[len-1] = '\0';
  }
}

#define raw_hmsstr_to_h(hmsstr) (raw_dmsstr_to_d(hmsstr))

double raw_dmsstr_to_d(char * dmsstr)
{
  int sign = 1;
  double d = 0.0;

  char * saveptr;
  char * tok;

  if(dmsstr[0] == '-') {
    sign = -1;
    dmsstr++;
  } else if(dmsstr[0] == '+') {
    dmsstr++;
  }

  tok = strtok_r(dmsstr, ":", &saveptr);
  if(tok) {
    // Degrees (or hours)
    d = strtod(tok, NULL);

    tok = strtok_r(NULL, ":", &saveptr);
    if(tok) {
      // Minutes
      d += strtod(tok, NULL) / 60.0;

      tok = strtok_r(NULL, ":", &saveptr);
      if(tok) {
        // Seconds
        d += strtod(tok, NULL) / 3600.0;
        tok = strtok_r(NULL, ":", &saveptr);
      }
    }
  } else {
    d = strtod(dmsstr, NULL);
  }

  return sign * d;
}

int raw_header_size(char * hdr, size_t len, int directio)
{
  int i;

  // Loop over the 80-byte records
  for(i=0; i<len; i += 80) {
    // If we found the "END " record
    if(!strncmp(hdr+i, "END ", 4)) {
      //printf("header_size: found END at record %d\n", i);
      // Move to just after END record
      i += 80;
      // Move past any direct I/O padding
      if(directio) {
        i += (MAX_RAW_HDR_SIZE - i) % 512;
      }
      return i;
    }
  }
  return 0;
}

// Reads RAW file params from fd.  On entry, fd is assumed to be at the start
// of a RAW header section.  On success, this function returns the file offset
// of the subsequent data block and the file descriptor `fd` will also refer to
// that location in the file.  On EOF, this function returns 0.  On failure,
// this function returns -1 and the location to which fd refers is undefined.
off_t raw_read_header(int fd, raw_hdr_t * raw_hdr)
{
  int i;
  int smjd;
  int imjd;
  // Ensure that hdr is aligned to a 512-byte boundary so that it can be used
  // with files opened with O_DIRECT.
  char hdr[MAX_RAW_HDR_SIZE] __attribute__ ((aligned (512)));
  int hdr_size;
  char tmp[80];
  off_t pos = lseek(fd, 0, SEEK_CUR);

  // Read header (plus some data, probably)
  hdr_size = read(fd, hdr, MAX_RAW_HDR_SIZE);

  if(hdr_size == -1) {
    return -1;
  } else if(hdr_size < 80) {
    return 0;
  }

  raw_hdr->blocsize = raw_get_int(hdr, "BLOCSIZE", 0);
  raw_hdr->npol     = raw_get_int(hdr, "NPOL",     0);
  raw_hdr->obsnchan = raw_get_int(hdr, "OBSNCHAN", 0);
  raw_hdr->obsfreq  = raw_get_dbl(hdr, "OBSFREQ",  0.0);
  raw_hdr->obsbw    = raw_get_dbl(hdr, "OBSBW",    0.0);
  raw_hdr->tbin     = raw_get_dbl(hdr, "TBIN",     0.0);
  raw_hdr->directio = raw_get_int(hdr, "DIRECTIO", 0);
  raw_hdr->pktidx   = raw_get_int(hdr, "PKTIDX",  -1);

  raw_get_str(hdr, "RA_STR", "0.0", tmp, 80);
  raw_hdr->ra = raw_hmsstr_to_h(tmp);

  raw_get_str(hdr, "DEC_STR", "0.0", tmp, 80);
  raw_hdr->dec = raw_dmsstr_to_d(tmp);

  imjd = raw_get_int(hdr, "STT_IMJD", 51545);
  smjd = raw_get_int(hdr, "STT_SMJD", 0);
  raw_hdr->mjd = ((double)imjd) + ((double)smjd)/86400.0;

  raw_get_str(hdr, "SRC_NAME", "Unknown", raw_hdr->src_name, 80);

  if(raw_hdr->blocsize ==  0) {
    fprintf(stderr, " BLOCSIZE not found in header\n");
    return -1;
  }
  if(raw_hdr->npol  ==  0) {
    fprintf(stderr, "NPOL not found in header\n");
    return -1;
  }
  if(raw_hdr->obsnchan ==  0) {
    fprintf(stderr, "OBSNCHAN not found in header\n");
    return -1;
  }
  if(raw_hdr->obsfreq  ==  0.0) {
    fprintf(stderr, "OBSFREQ not found in header\n");
    return -1;
  }
  if(raw_hdr->obsbw    ==  0.0) {
    fprintf(stderr, "OBSBW not found in header\n");
    return -1;
  }
  if(raw_hdr->tbin     ==  0.0) {
    fprintf(stderr, "TBIN not found in header\n");
    return -1;
  }
  if(raw_hdr->pktidx   == -1) {
    fprintf(stderr, "PKTIDX not found in header\n");
    return -1;
  }
  // 4 is the number of possible cross pol products
  if(raw_hdr->npol == 4) {
    // 2 is the actual number of polarizations present
    raw_hdr->npol = 2;
  }

  // Get actual size of header (plus any padding)
  hdr_size = raw_header_size(hdr, hdr_size, raw_hdr->directio);
  //printf("RRP: hdr=%lu\n", hdr_size);

  // Seek forward from original position past header (and any padding)
  pos = lseek(fd, pos + hdr_size, SEEK_SET);
  //printf("RRP: seek=%ld\n", pos);

  return pos;
}
