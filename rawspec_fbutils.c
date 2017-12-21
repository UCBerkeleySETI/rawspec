// Routines for reading and writing filterbank headers from/to files and memory
// buffers.  See fbutils.h for more details.

#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>

#include "rawspec_fbutils.h"

// Conversion utilities

double fb_ddd_to_dms(double ddd)
{
  int sign = ddd < 0 ? -1 : +1;
  ddd = fabs(ddd);
  double mm = 60*fmod(ddd, 1.0);
  double ss = 60*fmod(mm,  1.0);
  ddd = floor(ddd);
  mm  = floor(mm);
  return sign * (10000*ddd + 100*mm + ss);
}

double fb_dms_to_ddd(double dms)
{
  double dd;
  double mm;
  double ss;

  int sign = dms < 0 ? -1 : +1;
  dms = fabs(dms);

  dd = floor(dms / 10000);
  dms -= 10000 * dd;
  mm = floor(dms / 100);
  ss = dms - 100 * mm;
  dd += mm/60.0 + ss/3600.0;
  return sign * dd;
}

// Write utilities

ssize_t fb_fd_write_int(int fd, int32_t i)
{
  i = htole32(i);
  return write(fd, &i, sizeof(int32_t));
}

void * fb_buf_write_int(void * buf, int32_t i)
{
  i = htole32(i);
  *((int32_t *)buf) = i;
  return buf + sizeof(int32_t);
}

ssize_t fb_fd_write_double(int fd, double d)
{
  uint64_t i = htole64(*(uint64_t *)&d);
  return write(fd, &i, sizeof(uint64_t));
}

void * fb_buf_write_double(void * buf, double d)
{
  uint64_t i = htole64(*(uint64_t *)&d);
  *((int64_t *)buf) = i;
  return buf + sizeof(int64_t);
}

ssize_t fb_fd_write_angle(int fd, double d)
{
  return fb_fd_write_double(fd, fb_ddd_to_dms(d));
}

void * fb_buf_write_angle(void * buf, double d)
{
  return fb_buf_write_double(buf, fb_ddd_to_dms(d));
}

// Only write max 80 characters
ssize_t fb_fd_write_string(int fd, const char * c)
{
  int32_t len = strnlen(c, 80);
  fb_fd_write_int(fd, len);
  return write(fd, c, len) + sizeof(int32_t);
}

// Only write max 80 characters
void * fb_buf_write_string(void * buf, const char * c)
{
  int32_t len = strnlen(c, 80);
  buf = fb_buf_write_int(buf, len);
  memcpy(buf, c, len);
  return buf + len;
}

// Read utilities

int32_t fb_fd_read_int(int fd, int32_t * i)
{
  int ii;
  read(fd, &ii, sizeof(int32_t));
  ii = le32toh(ii);
  if(i) {
    *i = ii;
  }
  return ii;
}

void * fb_buf_read_int(void * buf, int32_t * i)
{
  if(i) {
    *i = le32toh(*(int32_t *)buf);
  }
  return buf + sizeof(int32_t);
}

double fb_fd_read_double(int fd, double * d)
{
  uint64_t ii;
  read(fd, &ii, sizeof(int64_t));
  ii = le64toh(ii);
  if(d) {
    *d = *(double *)&ii;
  }
  return *(double *)&ii;
}

void * fb_buf_read_double(void * buf, double * d)
{
  uint64_t ii;
  if(d) {
    ii = *(uint64_t *)buf;
    ii = le64toh(ii);
    *d = *(double *)&ii;
  }
  return buf + sizeof(int64_t);
}

double fb_fd_read_angle(int fd, double * d)
{
  double dd = fb_fd_read_double(fd, NULL);
  dd = fb_dms_to_ddd(dd);
  if(d) {
    *d = dd;
  }
  return dd;
}

void * fb_buf_read_angle(void * buf, double * d)
{
  buf = fb_buf_read_double(buf, d);
  if(d) {
    *d = fb_dms_to_ddd(*d);
  }
  return buf;
}

// Only read max *n characters
ssize_t fb_fd_read_string(int fd, char * c, int32_t * n)
{
  int32_t total_len, read_len;
  if(!c || !n) {
    return -1;
  }
  // Read length
  read(fd, &total_len, sizeof(int32_t));
  read_len = total_len = le32toh(total_len);
  // If length to read is greater than size of buffer
  if(read_len > *n) {
    // Truncate read length
    read_len = *n;
  }
  // Read string
  read_len = read(fd, c, read_len);
  if(read_len == -1) {
    return -1;
  }
  // If space remains in buffer, NUL terminate
  if(read_len < *n) {
    c[read_len] = '\0';
  }
  // If read was truncated, seek past rest of string
  if(read_len < total_len) {
    lseek(fd, total_len - read_len, SEEK_CUR);
  }
  // Store and return length read
  *n = read_len;
  return read_len;
}

// Sets *c to point to start of NOT-nul-terminated string in buf.  Sets *n to
// length of string.  Returns buf+sizeof(int)+length_of_string unless c or n in
// NULL in which case it returns buf.
void * fb_buf_peek_string(void * buf, char ** c, int32_t * n)
{
  if(!c || !n) {
    return buf;
  }
  *c = fb_buf_read_int(buf, n);
  return *c + *n;
}

// Only read max *n characters
void * fb_buf_read_string(void * buf, char * c, int32_t * n)
{
  char * bufstr;
  int32_t len;
  if(!c || !n) {
    return buf;
  }
  // Peek at string
  buf = fb_buf_peek_string(buf, &bufstr, &len);

  // If length to read is greater than size of buffer
  if(len > *n) {
    // Truncate read length
    len = *n;
  }
  // Copy string
  memcpy(c, bufstr, len);
  // If space remains in buffer, NUL terminate
  if(len < *n) {
    c[len] = '\0';
  }
  // Store length
  *n = len;

  return buf;
}

// Header functions

// TODO Add return value checking
ssize_t fb_fd_write_header(int fd, const fb_hdr_t * hdr)
{
  ssize_t n;

  n  = fb_fd_write_string(fd, "HEADER_START");
  n += fb_fd_write_string(fd, "machine_id");
  n += fb_fd_write_int(   fd, hdr->machine_id);
  n += fb_fd_write_string(fd, "telescope_id");
  n += fb_fd_write_int(   fd, hdr->telescope_id);
  n += fb_fd_write_string(fd, "src_raj");
  n += fb_fd_write_angle( fd, hdr->src_raj);
  n += fb_fd_write_string(fd, "src_dej");
  n += fb_fd_write_angle( fd, hdr->src_dej);
  n += fb_fd_write_string(fd, "az_start");
  n += fb_fd_write_double(fd, hdr->az_start);
  n += fb_fd_write_string(fd, "za_start");
  n += fb_fd_write_double(fd, hdr->za_start);
  n += fb_fd_write_string(fd, "data_type");
  n += fb_fd_write_int(   fd, hdr->data_type);
  n += fb_fd_write_string(fd, "fch1");
  n += fb_fd_write_double(fd, hdr->fch1);
  n += fb_fd_write_string(fd, "foff");
  n += fb_fd_write_double(fd, hdr->foff);
  n += fb_fd_write_string(fd, "nchans");
  n += fb_fd_write_int(   fd, hdr->nchans);
  n += fb_fd_write_string(fd, "nbeams");
  n += fb_fd_write_int(   fd, hdr->nbeams);
  n += fb_fd_write_string(fd, "ibeam");
  n += fb_fd_write_int(   fd, hdr->ibeam);
  n += fb_fd_write_string(fd, "nbits");
  n += fb_fd_write_int(   fd, hdr->nbits);
  n += fb_fd_write_string(fd, "tstart");
  n += fb_fd_write_double(fd, hdr->tstart);
  n += fb_fd_write_string(fd, "tsamp");
  n += fb_fd_write_double(fd, hdr->tsamp);
  n += fb_fd_write_string(fd, "nifs");
  n += fb_fd_write_int(   fd, hdr->nifs);
  if(hdr->barycentric) {
    n += fb_fd_write_string(fd, "barycentric");
    n += fb_fd_write_int(   fd, hdr->barycentric);
  }
  if(hdr->pulsarcentric) {
    n += fb_fd_write_string(fd, "pulsarcentric");
    n += fb_fd_write_int(   fd, hdr->pulsarcentric);
  }
  n += fb_fd_write_string(fd, "source_name");
  n += fb_fd_write_string(fd, hdr->source_name);
  n += fb_fd_write_string(fd, "rawdatafile");
  n += fb_fd_write_string(fd, hdr->rawdatafile);
  n += fb_fd_write_string(fd, "HEADER_END");

  return n;
}

void * fb_buf_write_header(void * buf, const fb_hdr_t * hdr)
{
  buf = fb_buf_write_string(buf, "HEADER_START");
  buf = fb_buf_write_string(buf, "machine_id");
  buf = fb_buf_write_int(   buf, hdr->machine_id);
  buf = fb_buf_write_string(buf, "telescope_id");
  buf = fb_buf_write_int(   buf, hdr->telescope_id);
  buf = fb_buf_write_string(buf, "src_raj");
  buf = fb_buf_write_angle( buf, hdr->src_raj);
  buf = fb_buf_write_string(buf, "src_dej");
  buf = fb_buf_write_angle( buf, hdr->src_dej);
  buf = fb_buf_write_string(buf, "az_start");
  buf = fb_buf_write_double(buf, hdr->az_start);
  buf = fb_buf_write_string(buf, "za_start");
  buf = fb_buf_write_double(buf, hdr->za_start);
  buf = fb_buf_write_string(buf, "data_type");
  buf = fb_buf_write_int(   buf, hdr->data_type);
  buf = fb_buf_write_string(buf, "fch1");
  buf = fb_buf_write_double(buf, hdr->fch1);
  buf = fb_buf_write_string(buf, "foff");
  buf = fb_buf_write_double(buf, hdr->foff);
  buf = fb_buf_write_string(buf, "nchans");
  buf = fb_buf_write_int(   buf, hdr->nchans);
  buf = fb_buf_write_string(buf, "nbeams");
  buf = fb_buf_write_int(   buf, hdr->nbeams);
  buf = fb_buf_write_string(buf, "ibeam");
  buf = fb_buf_write_int(   buf, hdr->ibeam);
  buf = fb_buf_write_string(buf, "nbits");
  buf = fb_buf_write_int(   buf, hdr->nbits);
  buf = fb_buf_write_string(buf, "tstart");
  buf = fb_buf_write_double(buf, hdr->tstart);
  buf = fb_buf_write_string(buf, "tsamp");
  buf = fb_buf_write_double(buf, hdr->tsamp);
  buf = fb_buf_write_string(buf, "nifs");
  buf = fb_buf_write_int(   buf, hdr->nifs);
  if(hdr->barycentric) {
    buf = fb_buf_write_string(buf, "barycentric");
    buf = fb_buf_write_int(   buf, hdr->barycentric);
  }
  if(hdr->pulsarcentric) {
    buf = fb_buf_write_string(buf, "pulsarcentric");
    buf = fb_buf_write_int(   buf, hdr->pulsarcentric);
  }
  buf = fb_buf_write_string(buf, "source_name");
  buf = fb_buf_write_string(buf, hdr->source_name);
  buf = fb_buf_write_string(buf, "rawdatafile");
  buf = fb_buf_write_string(buf, hdr->rawdatafile);
  buf = fb_buf_write_string(buf, "HEADER_END");

  return buf;
}

// TODO Make this more robust by using the value of *hdr_len on enrty as the
// max number of bytes to process from buf.
void * fb_buf_read_header(void * buf, fb_hdr_t * hdr, size_t * hdr_len)
{
  int32_t len;
  char * kw;
  void * p;

  // No NULLs allowed!
  if(!buf || !hdr || !hdr_len) {
    return buf;
  }

  // Zero out the header structure
  memset(hdr, 0, sizeof(fb_hdr_t));

  // Peek at first keyword
  p = fb_buf_peek_string(buf, &kw, &len);
  // If first string is not HEADER_START
  if(strncmp(kw, "HEADER_START", len)) {
    // buf is not a filterbank header, return original buf value.
    return buf;
  }

  // Read next keyword
  p = fb_buf_peek_string(p, &kw, &len);

  // While we're not at the end
  while(strncmp(kw, "HEADER_END", len)) {

    // Read and store value for keyword
    if(!strncmp(kw, "machine_id", len)) {
      p = fb_buf_read_int(p, &hdr->machine_id);
    } else if(!strncmp(kw, "telescope_id", len)) {
      p = fb_buf_read_int(p, &hdr->telescope_id);
    } else if(!strncmp(kw, "data_type", len)) {
      p = fb_buf_read_int(p, &hdr->data_type);
    } else if(!strncmp(kw, "barycentric", len)) {
      p = fb_buf_read_int(p, &hdr->barycentric);
    } else if(!strncmp(kw, "pulsarcentric", len)) {
      p = fb_buf_read_int(p, &hdr->pulsarcentric);
    } else if(!strncmp(kw, "src_raj", len)) {
      p = fb_buf_read_angle(p, &hdr->src_raj);
    } else if(!strncmp(kw, "src_dej", len)) {
      p = fb_buf_read_angle(p, &hdr->src_dej);
    } else if(!strncmp(kw, "az_start", len)) {
      p = fb_buf_read_double(p, &hdr->az_start);
    } else if(!strncmp(kw, "za_start", len)) {
      p = fb_buf_read_double(p, &hdr->za_start);
    } else if(!strncmp(kw, "fch1", len)) {
      p = fb_buf_read_double(p, &hdr->fch1);
    } else if(!strncmp(kw, "foff", len)) {
      p = fb_buf_read_double(p, &hdr->foff);
    } else if(!strncmp(kw, "nchans", len)) {
      p = fb_buf_read_int(p, &hdr->nchans);
    } else if(!strncmp(kw, "nbeams", len)) {
      p = fb_buf_read_int(p, &hdr->nbeams);
    } else if(!strncmp(kw, "ibeam", len)) {
      p = fb_buf_read_int(p, &hdr->ibeam);
    } else if(!strncmp(kw, "nbits", len)) {
      p = fb_buf_read_int(p, &hdr->nbits);
    } else if(!strncmp(kw, "tstart", len)) {
      p = fb_buf_read_double(p, &hdr->tstart);
    } else if(!strncmp(kw, "tsamp", len)) {
      p = fb_buf_read_double(p, &hdr->tsamp);
    } else if(!strncmp(kw, "nifs", len)) {
      p = fb_buf_read_int(p, &hdr->nifs);
    } else if(!strncmp(kw, "source_name", len)) {
      len = sizeof(hdr->source_name) - 1;
      p = fb_buf_read_string(p, hdr->source_name, &len);
      hdr->source_name[len] = '\0';
    } else if(!strncmp(kw, "rawdatafile", len)) {
      len = sizeof(hdr->rawdatafile) - 1;
      p = fb_buf_read_string(p, hdr->rawdatafile, &len);
      hdr->rawdatafile[len] = '\0';
    } else {
      // Ignore unknown keyword
    }

    // Peek next keyword
    p = fb_buf_peek_string(p, &kw, &len);
  }

  // Store length
  if(hdr_len) {
    *hdr_len = p - buf;
  }

  return p;
}

ssize_t fb_fd_read_header(int fd, fb_hdr_t * hdr, size_t * hdr_len)
{
  char buf[4096];
  char * p;
  size_t len;
  size_t bytes_read;

  // Make sure hdr is non-NULL
  if(!hdr) {
    return -1;
  }

  // Read more than the header could ever be
  len = bytes_read = read(fd, buf, sizeof(buf));

  // Parse header from buffer
  p = fb_buf_read_header(buf, hdr, &len);

  // If successful
  if(p != buf) {
    // Seek to end of header
    lseek(fd, len - 4096, SEEK_CUR);
    // Store length
    if(hdr_len) {
      *hdr_len = len;
    }
  }

  return len;
}


#ifdef FBUTILS_TEST

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char * argv[])
{
  fb_hdr_t hdr;

  // 0=fake data; 1=Arecibo; 2=Ooty... others to be added
  hdr.machine_id = 20; // wtf?
  // 0=FAKE; 1=PSPM; 2=WAPP; 3=OOTY... others to be added
  hdr.telescope_id = 6; // GBT
  // 1=filterbank; 2=time series... others to be added
  hdr.data_type = 1;
  // 1 if barycentric or 0 otherwise (only output if non-zero)
  hdr.barycentric = 1;
  // 1 if pulsarcentric or 0 otherwise (only output if non-zero)
  hdr.pulsarcentric = 1;
  // right ascension (J2000) of source (hours)
  // will be converted to/from hhmmss.s
  hdr.src_raj = 20.0 + 39/60.0 + 7.4/3600.0;
  // declination (J2000) of source (degrees)
  // will be converted to/from ddmmss.s
  hdr.src_dej = 42.0 + 24/60.0 + 24.5/3600.0;
  // telescope azimuth at start of scan (degrees)
  hdr.az_start = 12.3456;
  // telescope zenith angle at start of scan (degrees)
  hdr.za_start = 65.4321;
  // centre frequency (MHz) of first filterbank channel
  hdr.fch1 = 4626.464842353016138;
  // filterbank channel bandwidth (MHz)
  hdr.foff = -0.000002793967724;
  // number of filterbank channels
  hdr.nchans = 1;
  // total number of beams
  hdr.nbeams = 1;
  // total number of beams
  hdr.ibeam = 1;
  // number of bits per time sample
  hdr.nbits = 32;
  // time stamp (MJD) of first sample
  hdr.tstart = 57856.810798611114;
  // time interval between samples (s)
  hdr.tsamp = 1.825361100800;
  // number of seperate IF channels
  hdr.nifs = 1;
  // the name of the source being observed by the telescope
  // Max string size is supposed to be 80, but bug in sigproc if over 79
  strcpy(hdr.source_name, "1234567890123456789012345678901234567890123456789012345678901234567890123456789");
  // the name of the original data file
  // Max string size is supposed to be 80, but bug in sigproc if over 79
  strcpy(hdr.rawdatafile, "1234567890123456789012345678901234567890123456789012345678901234567890123456789");

  if(argc > 1) {
    int fd  = open(argv[1],  O_RDONLY);
    ssize_t hdr_size = fb_fd_read_header(fd, &hdr, NULL);
    printf("header size %lu bytes\n", hdr_size);
    printf("fch1 %.17g\n", hdr.fch1);
    printf("foff %.17g\n", hdr.foff);
  } else {
    float f = 0;

    int fdfd  = open("fbutils_fd.fil",  O_WRONLY | O_CREAT, 0664);
    int fdbuf = open("fbutils_buf.fil", O_WRONLY | O_CREAT, 0664);


    ssize_t nbytes = fb_fd_write_header(fdfd, &hdr);
    write(fdfd, (void *)&f, sizeof(float));
    printf("write %lu+4 fd bytes\n", nbytes);

    char buf[1024];
    char * end = fb_buf_write_header(buf, &hdr);
    nbytes = end-buf;
    write(fdbuf, buf, nbytes);
    write(fdbuf, (void *)&f, sizeof(float));
    printf("write %lu+4 buf bytes\n", nbytes);

    close(fdfd);
    close(fdbuf);
  }

  return 0;
}
#endif // FBUTILS_TEST
