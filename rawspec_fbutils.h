// Routines for reading and writing filterbank headers from/to files and memory
// buffers.  A filterbank header consists of ASCII keywords that are preceeded
// by a 4 byte integer specifying the ASCII keyword length.  Each keyword is
// followed by 0 or more values, but usually just 1.  Each keyword has a
// specific type of data that are expected.  For maximum compatibility with
// historic implementions and currently dominant byte endianness, this library
// outputs integer and double values in little endian format.
//
// The format of a filterbank header can be describled by this ASCII-gram:
//
//     IIIIHEADER_START
//     IIIImachine_idIIII
//     IIIItelescope_idIIII
//     IIIIsrc_rajDDDDDDDD
//     IIIIsrc_dejDDDDDDDD
//     IIIIaz_startDDDDDDDD
//     IIIIza_startDDDDDDDD
//     IIIIdata_typeIIII
//     IIIIfch1DDDDDDDD
//     IIIIfoffDDDDDDDD
//     IIIInchansIIII
//     IIIInbeamsIIII
//     IIIIibeamIIII
//     IIIInbitsIIII
//     IIIItstartDDDDDDDD
//     IIIItsampDDDDDDDD
//     IIIInifsIIII
//     IIIIsource_nameIIII[C...]
//     IIIIrawdatafileIIII[C...]
//     IIIIHEADER_END
//
// Where IIII is a 4-byte integer, DDDDDDDD is an 8 byte double precision
// floating point number, and [C...] is a variable length ASCII string (not NUL
// terminated).  Additional keywords exist in the SIGPROC spec, but these are
// the keywords currently supported by the routines in this file.
//
// Here is a sample output from the SIGPROC `header` program which reads a
// filterbank header and outputs a human-readable version:
//
// Data file                        : blc34_guppi_57856_70053_DIAG_DR21_0034.gpuspec.0000.fil
// Header size (bytes)              : 384
// Data size (bytes)                : 805306368
// Data type                        : filterbank (topocentric)
// Telescope                        : GBT
// Datataking Machine               : ?????
// Source Name                      : DIAG_DR21
// Source RA (J2000)                : 20:39:07.4
// Source DEC (J2000)               : +42:24:24.5
// Frequency of channel 1 (MHz)     : 4626.464842353016138
// Channel bandwidth      (MHz)     : -0.000002793967724
// Number of channels               : 67108864
// Number of beams                  : 1
// Beam number                      : 1
// Time stamp of first sample (MJD) : 57856.810798611114
// Gregorian date (YYYY/MM/DD)      : 2017/04/13
// Sample time (us)                 : 18253611.00800
// Number of samples                : 3
// Observation length (seconds)     : 54.8
// Number of bits per sample        : 32
// Number of IFs                    : 1

#ifndef _FBUTILS_H_
#define _FBUTILS_H_

#include <stdint.h>
#include <sys/types.h>

typedef struct {
  // 0=fake data; 1=Arecibo; 2=Ooty... others to be added
  int machine_id;
  // 0=FAKE; 1=PSPM; 2=WAPP; 3=OOTY... others to be added
  int telescope_id;
  // 1=filterbank; 2=time series... others to be added
  int data_type;
  // 1 if barycentric or 0 otherwise (only output if non-zero)
  int barycentric;
  // 1 if pulsarcentric or 0 otherwise (only output if non-zero)
  int pulsarcentric;
  // right ascension (J2000) of source (hours)
  // will be converted to/from hhmmss.s
  double src_raj;
  // declination (J2000) of source (degrees)
  // will be converted to/from ddmmss.s
  double src_dej;
  // telescope azimuth at start of scan (degrees)
  double az_start;
  // telescope zenith angle at start of scan (degrees)
  double za_start;
  // centre frequency (MHz) of first filterbank channel
  double fch1;
  // filterbank channel bandwidth (MHz)
  double foff;
  // number of filterbank channels
  int nchans;
  // total number of beams
  int nbeams;
  // total number of beams
  int ibeam;
  // number of bits per time sample
  int nbits;
  // time stamp (MJD) of first sample
  double tstart;
  // time interval between samples (s)
  double tsamp;
  // number of seperate IF channels
  int nifs;
  // the name of the source being observed by the telescope
  char source_name[81];
  // the name of the original data file
  char rawdatafile[81];
} fb_hdr_t;

#ifdef __cplusplus
extern "C" {
#endif

// Conversion utilities

double fb_ddd_to_dms(double ddd);
double fb_dms_to_ddd(double dms);

// Write utilities

ssize_t fb_fd_write_int(int fd, int32_t i);
void * fb_buf_write_int(void * buf, int32_t i);

ssize_t fb_fd_write_double(int fd, double d);
void * fb_buf_write_double(void * buf, double d);

ssize_t fb_fd_write_angle(int fd, double d);
void * fb_buf_write_angle(void * buf, double d);

ssize_t fb_fd_write_string(int fd, const char * c);
void * fb_buf_write_string(void * buf, const char * c);

// Read utilities

int32_t fb_fd_read_int(int fd, int32_t * i);
void * fb_buf_read_int(void * buf, int32_t * i);

double fb_fd_read_double(int fd, double * d);
void * fb_buf_read_double(void * buf, double * d);

double fb_fd_read_angle(int fd, double * d);
void * fb_buf_read_angle(void * buf, double * d);

ssize_t fb_fd_read_string(int fd, char * c, int32_t * n);
void * fb_buf_peek_string(void * buf, char ** c, int32_t * n);
void * fb_buf_read_string(void * buf, char * c, int32_t * n);

// Header functions

ssize_t fb_fd_write_padded_header(int fd, const fb_hdr_t * hdr, int32_t minlen);
ssize_t fb_fd_write_header(int fd, const fb_hdr_t * hdr);
void * fb_buf_write_padded_header(void * buf, const fb_hdr_t * hdr, int32_t minlen);
void * fb_buf_write_header(void * buf, const fb_hdr_t * hdr);

ssize_t fb_fd_read_header(int fd, fb_hdr_t * hdr, size_t * hdr_len);
void * fb_buf_read_header(void * buf, fb_hdr_t * hdr, size_t * hdr_len);

int fb_telescope_id(const char *telescope_name);
#ifdef __cplusplus
}
#endif

#endif // _FBUTILS_H_
