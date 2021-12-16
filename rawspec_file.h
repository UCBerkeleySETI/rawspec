#ifndef _RAWSPEC_FILE_H_
#define _RAWSPEC_FILE_H_

#include "rawspec.h"
#include "rawspec_callback.h"

#ifdef __cplusplus
extern "C" {
#endif

int open_output_file(callback_data_t *cb_data, const char * dest, const char *stem, int output_idx, int ics_flag);

int open_output_file_per_antenna_and_write_header(callback_data_t *cb_data, const char * dest, const char *stem, int output_idx);

void dump_file_callback(rawspec_context * ctx, int output_product, int callback_type);

#ifdef __cplusplus
}
#endif

#endif // _RAWSPEC_FILE_H_

