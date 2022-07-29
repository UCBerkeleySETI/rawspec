#ifndef _RAWSPEC_CALLBACK_H_
#define _RAWSPEC_CALLBACK_H_

#include <pthread.h>
#include "hdf5.h"
#include "rawspec_fbutils.h"

typedef struct {
    int active;                 // Still active? 1=yes, 0=no
    hid_t file_id;              // File-level handle (similar to an fd)
    hid_t dataset_id;           // Dataset "data" handle
    hid_t dataspace_id;         // Dataspace handle for dataset "data"
    unsigned int elem_size;     // Byte size of one spectra element (E.g. 4 if nbits=32)
    hid_t elem_type;            // HDF5 type for all elements (derived from nbits in fbh5_open)
    size_t tint_size;           // Size of a time integration (computed in fbh5_open)
    hsize_t offset_dims[3];     // Next offset dimensions for the fbh5_write function
                                // (offset_dims[0] : time integration count)
    hsize_t filesz_dims[3];     // Accumulated file size in dimensions
    unsigned long byte_count;   // Number of bytes output so far
    unsigned long dump_count;   // Number of dumps processed so far
} fbh5_context_t;

enum rawspec_callback_file_format_t {
  FILE_FORMAT_FBSIGPROC = 0,
  FILE_FORMAT_FBH5      = 1,
  FILE_FORMAT_GUPPIRAW  = 2
};

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

  enum rawspec_callback_file_format_t flag_file_output;
  // Added for FBH5 2021-11-15
  fbh5_context_t fbh5_ctx_ics;    // Singleton fbh5 ctx for ics
  fbh5_context_t * fbh5_ctx_ant;  // Pointer to array of fbh5 ctx for individual antennas

  // Exit soon flag.
  // 0 : No output errors have occurred so far.
  // 1 : At least one output error has occured.
  unsigned int exit_soon;

  // Added for GUPPI RAW output 2022-07
} callback_data_t;

#endif // _RAWSPEC_CALLBACK_H_
