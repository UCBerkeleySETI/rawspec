/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * fbh5_defs.h                                                                 *
 * -----------                                                                 *
 * Global Definitions       .                                                  *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef FBH5_DEFS_H
#define FBH5_DEFS_H

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

/*
 * HDF5 library definitions
 */
#include <hdf5/serial/hdf5.h>
#include <hdf5/serial/H5pubconf.h>
#include <hdf5/serial/H5DSpublic.h>
#ifndef H5_HAVE_THREADSAFE
#error The installed HDF5 run-time is not thread-safe!
#endif

/*
 * Rawspec callback definitions
 */
#include "rawspec_callback.h"

/*
 * Global definitions
 */
#define DATASETNAME         "data"  // FBH5 dataset name to hold the data matrix
#define NDIMS               3       // # of data matrix dimensions (rank)
#define FILTERBANK_CLASS    "FILTERBANK"    // File-level attribute "CLASS"
#define FILTERBANK_VERSION  "2.0"   // File-level attribute "VERSION"
#define ENABLER_FD_FOR_FBH5 42      // Fake fd value to enable dump_file_thread_func()

/*
 * fbh5 API functions
 */
int     fbh5_open(fbh5_context_t * p_fbh5_ctx, fb_hdr_t * p_fb_hdr, char * output_path, int debug_callback);
int     fbh5_write(fbh5_context_t * p_fbh5_ctx, fb_hdr_t * p_fb_hdr, void * buffer, size_t bufsize, int debug_callback);
int     fbh5_close(fbh5_context_t * p_fbh5_ctx, int debug_callback);

/*
 * fbh5_util.c functions
 */
void    fbh5_info(const char * format, ...);
void    fbh5_warning(char * srcfile, int linenum, char * msg);
void    fbh5_error(char * srcfile, int linenum, char * msg);
void    fbh5_set_str_attr(hid_t file_or_dataset_id, char * tag, char * value, int debug_callback);
void    fbh5_set_dataset_double_attr(hid_t dataset_id, char * tag, double * p_value, int debug_callback);
void    fbh5_set_dataset_int_attr(hid_t dataset_id, char * tag, int * p_value, int debug_callback);
void    fbh5_write_metadata(hid_t dataset_id, fb_hdr_t * p_metadata, int debug_callback);
void    fbh5_set_ds_label(fbh5_context_t * p_fbh5_ctx, char * label, int dims_index, int debug_callback);
void    fbh5_show_context(char * caller, fbh5_context_t * p_fbh5_ctx);

#endif
