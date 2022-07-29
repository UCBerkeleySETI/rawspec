#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "rawspec_rawutils.h"

int32_t rawspec_raw_get_s32(const char * buf, const char * key, int32_t def)
{
  char tmpstr[48];
  int32_t value;
  if (hgeti4(buf, key, &value) == 0) {
    if (hgets(buf, key, 48, tmpstr) == 0) {
      value = def;
    } else {
      value = strtol(tmpstr, NULL, 0);
    }
  }

  return value;
}

uint32_t rawspec_raw_get_u32(const char * buf, const char * key, uint32_t def)
{
  char tmpstr[48];
  uint32_t value;
  if (hgetu4(buf, key, &value) == 0) {
    if (hgets(buf, key, 48, tmpstr) == 0) {
      value = def;
    } else {
      value = strtoul(tmpstr, NULL, 0);
    }
  }

  return value;
}

int64_t rawspec_raw_get_s64(const char * buf, const char * key, int64_t def)
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

uint64_t rawspec_raw_get_u64(const char * buf, const char * key, uint64_t def)
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

double rawspec_raw_get_dbl(const char * buf, const char * key, double def)
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

void rawspec_raw_get_str(const char * buf, const char * key, const char * def,
                 char * out, size_t len)
{
  if (hgets(buf, key, len, out) == 0) {
    strncpy(out, def, len);
    out[len-1] = '\0';
  }
}

double rawspec_raw_dmsstr_to_d(char * dmsstr)
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

int rawspec_raw_header_size(char * hdr, size_t len, int directio)
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

const uint64_t KEY_UINT64_BLOCSIZE = GUPPI_RAW_KEY_UINT64_ID_LE('B','L','O','C','S','I','Z','E');
const uint64_t KEY_UINT64_NPOL     = GUPPI_RAW_KEY_UINT64_ID_LE('N','P','O','L',' ',' ',' ',' ');
const uint64_t KEY_UINT64_OBSNCHAN = GUPPI_RAW_KEY_UINT64_ID_LE('O','B','S','N','C','H','A','N');
const uint64_t KEY_UINT64_NBITS    = GUPPI_RAW_KEY_UINT64_ID_LE('N','B','I','T','S',' ',' ',' ');
const uint64_t KEY_UINT64_OBSFREQ  = GUPPI_RAW_KEY_UINT64_ID_LE('O','B','S','F','R','E','Q',' ');
const uint64_t KEY_UINT64_OBSBW    = GUPPI_RAW_KEY_UINT64_ID_LE('O','B','S','B','W',' ',' ',' ');
const uint64_t KEY_UINT64_TBIN     = GUPPI_RAW_KEY_UINT64_ID_LE('T','B','I','N',' ',' ',' ',' ');
const uint64_t KEY_UINT64_DIRECTIO = GUPPI_RAW_KEY_UINT64_ID_LE('D','I','R','E','C','T','I','O');
const uint64_t KEY_UINT64_PKTIDX   = GUPPI_RAW_KEY_UINT64_ID_LE('P','K','T','I','D','X',' ',' ');
const uint64_t KEY_UINT64_BEAM_ID  = GUPPI_RAW_KEY_UINT64_ID_LE('B','E','A','M','_','I','D',' ');
const uint64_t KEY_UINT64_NBEAM    = GUPPI_RAW_KEY_UINT64_ID_LE('N','B','E','A','M',' ',' ',' ');
const uint64_t KEY_UINT64_NANTS    = GUPPI_RAW_KEY_UINT64_ID_LE('N','A','N','T','S',' ',' ',' ');
const uint64_t KEY_UINT64_RA_STR   = GUPPI_RAW_KEY_UINT64_ID_LE('R','A','_','S','T','R',' ',' ');
const uint64_t KEY_UINT64_DEC_STR  = GUPPI_RAW_KEY_UINT64_ID_LE('D','E','C','_','S','T','R',' ');
const uint64_t KEY_UINT64_STT_IMJD = GUPPI_RAW_KEY_UINT64_ID_LE('S','T','T','_','I','M','J','D');
const uint64_t KEY_UINT64_STT_SMJD = GUPPI_RAW_KEY_UINT64_ID_LE('S','T','T','_','S','M','J','D');
const uint64_t KEY_UINT64_SRC_NAME = GUPPI_RAW_KEY_UINT64_ID_LE('S','R','C','_','N','A','M','E');
const uint64_t KEY_UINT64_TELESCOP = GUPPI_RAW_KEY_UINT64_ID_LE('T','E','L','E','S','C','O','P');

void _rawspec_header_parse_metadata(const char* entry, void* raw_hdr_void) {
  rawspec_raw_hdr_t * raw_hdr = (rawspec_raw_hdr_t*) raw_hdr_void;

  if(((uint64_t*)entry)[0] == KEY_UINT64_BLOCSIZE)
    hgeti4(entry, "BLOCSIZE", &raw_hdr->blocsize);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_NPOL)
    hgeti4(entry, "NPOL", &raw_hdr->npol);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_OBSNCHAN)
    hgeti4(entry, "OBSNCHAN", &raw_hdr->obsnchan);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_NBITS)
    hgetu4(entry, "NBITS", &raw_hdr->nbits);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_OBSFREQ)
    hgetr8(entry, "OBSFREQ", &raw_hdr->obsfreq);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_OBSBW)
    hgetr8(entry, "OBSBW", &raw_hdr->obsbw);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_TBIN)
    hgetr8(entry, "TBIN", &raw_hdr->tbin);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_DIRECTIO)
    hgeti4(entry, "DIRECTIO", &raw_hdr->directio);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_PKTIDX)
    hgetu8(entry, "PKTIDX", &raw_hdr->pktidx);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_BEAM_ID)
    hgeti4(entry, "BEAM_ID", &raw_hdr->beam_id);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_NBEAM)
    hgeti4(entry, "NBEAM", &raw_hdr->nbeam);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_NANTS)
    hgetu4(entry, "NANTS", &raw_hdr->nants);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_RA_STR) {
    char tmp[72];
    hgets(entry, "RA_STR", 72, tmp);
    raw_hdr->ra = rawspec_raw_hmsstr_to_h(tmp);
  }
  else if(((uint64_t*)entry)[0] == KEY_UINT64_DEC_STR) {
    char tmp[72];
    hgets(entry, "DEC_STR", 72, tmp);
    raw_hdr->dec = rawspec_raw_dmsstr_to_d(tmp);
  }
  else if(((uint64_t*)entry)[0] == KEY_UINT64_STT_IMJD) {
    double tmp = 0.0;
    hgetr8(entry, "STT_IMJD", &tmp);
    raw_hdr->mjd += tmp/86400.0;
  }
  else if(((uint64_t*)entry)[0] == KEY_UINT64_STT_SMJD) {
    double tmp = 0.0;
    hgetr8(entry, "STT_SMJD", &tmp);
    raw_hdr->mjd += tmp/86400.0;
  }
  else if(((uint64_t*)entry)[0] == KEY_UINT64_SRC_NAME)
    hgets(entry, "SRC_NAME", 72, raw_hdr->src_name);
  else if(((uint64_t*)entry)[0] == KEY_UINT64_TELESCOP)
    hgets(entry, "TELESCOP", 72, raw_hdr->telescop);
}

// Parses rawspec related RAW header params from buf into raw_hdr.
void rawspec_raw_parse_header(const char * buf, rawspec_raw_hdr_t * raw_hdr)
{
  snprintf(raw_hdr->src_name, 72, "Unknown");
  snprintf(raw_hdr->telescop, 72, "Unknown");
  raw_hdr->mjd = 0.0;

  guppiraw_metadata_t metadata = {0};
  metadata.user_data = raw_hdr;
  metadata.user_callback = _rawspec_header_parse_metadata;
  guppiraw_header_string_parse_metadata(&metadata, buf, -1);
}

// Reads RAW file params from fd.  On entry, fd is assumed to be at the start
// of a RAW header section.  On success, this function returns the file offset
// of the subsequent data block and the file descriptor `fd` will also refer to
// that location in the file.  On EOF, this function returns 0.  On failure,
// this function returns -1 and the location to which fd refers is undefined.
off_t rawspec_raw_read_header(int fd, rawspec_raw_hdr_t * raw_hdr)
{
  int i;
  // Ensure that hdr is aligned to a 512-byte boundary so that it can be used
  // with files opened with O_DIRECT.
  char hdr[MAX_RAW_HDR_SIZE] __attribute__ ((aligned (512)));
  int hdr_size;
  off_t pos = lseek(fd, 0, SEEK_CUR);

  // Read header (plus some data, probably)
  hdr_size = read(fd, hdr, MAX_RAW_HDR_SIZE);

  if(hdr_size == -1) {
    return -1;
  } else if(hdr_size < 80) {
    return 0;
  }

  rawspec_raw_parse_header(hdr, raw_hdr);

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

  // Save header pos/size
  raw_hdr->hdr_pos = pos;
  raw_hdr->hdr_size = rawspec_raw_header_size(hdr, hdr_size, 0);

  // Get actual size of header (plus any padding)
  hdr_size = rawspec_raw_header_size(hdr, hdr_size, raw_hdr->directio);
  //printf("RRP: hdr=%lu\n", hdr_size);

  // Seek forward from original position past header (and any padding)
  pos = lseek(fd, pos + hdr_size, SEEK_SET);
  //printf("RRP: seek=%ld\n", pos);

  return pos;
}
