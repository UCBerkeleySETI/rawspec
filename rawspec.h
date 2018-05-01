#ifndef _MYGPUSPEC_H_
#define _MYGPUSPEC_H_

#include <unistd.h>

// Specifies forward or inverse FFT direction
#define RAWSPEC_FORWARD_FFT (+1)
#define RAWSPEC_INVERSE_FFT (-1)

#define MAX_OUTPUTS (4)

#define RAWSPEC_BLOCSIZE(pctx) ((pctx)->Nc * (pctx)->Ntpb * (pctx)->Np * 2)

#define RAWSPEC_CALLBACK_PRE_DUMP  (0)
#define RAWSPEC_CALLBACK_POST_DUMP (1)

// Forward declaration
typedef struct rawspec_context_s rawspec_context;

typedef void (* rawspec_dump_callback_t)(rawspec_context * ctx,
                                           int output_product,
                                           int callback_type);

// Structure for holding the context.
struct rawspec_context_s {
  unsigned int No; // Number of output products (max MAX_OUTPUTS)
  unsigned int Np; // Number of polarizations (in input data)
  unsigned int Nc; // Number of coarse channels
  unsigned int Ntpb; // Number of time samples per block

  // Noutpol is the number of output polarization values per fine channel.
  // This valid values for this field are 1 (total power only) and 4 (full
  // cross-pol powers).  A value of 4 is only valid if Np is 2.
  unsigned int Npolout;

  // Nts is an array of Nt values, one per output product.  The Nt value is the
  // number of time samples per FFT for a given output product.  All Nt values
  // must evenly divide the total number of time samples in the input buffer.
  unsigned int Nts[MAX_OUTPUTS]; // Array of Nt values
  // Nas is an array of Na values, one per output product.  The Na value is the
  // number of FFT spectra to accumulate per integration.
  // Nt*Na < Nb*Ntpb : Multiple integrations per input buffer,
  //                   must have integer integrations per input buffer
  // Nt*Na = Nb*Ntpb : One integration per input buffer
  // Nt*Na > Nb*Ntpb : Multiple input buffers per integration,
  //                   must have integer input buffers per integration
  unsigned int Nas[MAX_OUTPUTS]; // Array of Na values

  // dump_callback is a pointer to a user-supplied output callback function.
  // This function will be called twice per dump: one time just before data are
  // dumped to the the output power buffer (h_pwrbuf[i]) and a second time just
  // after the data are dumped to the output power buffer.  The first call
  // provides the client a chance to synchronize with any previous output
  // thread it may have launched.  The second call is when the client can
  // output the data (e.g. by launching an output thread).
  rawspec_dump_callback_t dump_callback;

  // Pointer to user data.  This is intended for use by the client's dump
  // callback function (e.g. to hold output file handles or network sockets).
  // The rawsepc library does not do anything with this field.
  void * user_data;

  // Nb is the number of input blocks per GPU input buffer.
  // Set to zero to NULL to have the library calculate Nb (as Ntmax/Ntpb).
  unsigned int Nb;

  // Array of host pointers to Nb block buffers.  Set h_blkbufs to NULL to have
  // the library allocate the host input block buffers.  For client managed
  // buffers, the user must allocate and array of Nb pointers, initialize them
  // to point to the caller allocated blocks, and set the Nb field of this
  // structure.  The size of each host input block buffer is Nc * Ntpb * Np * 2
  // (in bytes).
  char ** h_blkbufs;

  // Fields above here should be specified by client.  Fields below here are
  // managed by library (but can be used by the caller as needed).

  // Host pointers to the output power buffers.
  // In total power mode, the sizes (in bytes) will be:
  //     Nds[i] * Nc * Nts[i] * sizeof(float)
  // In total power mode, the output buffer is [P00+P11]
  // In full pol mode, the sizes (in bytes) will be:
  //     4 * Nds[i] * Nc * Nts[i] * sizeof(float)
  // In full pol mode, the output buffer is [P00, P11, P01re, P01im].
  float * h_pwrbuf[MAX_OUTPUTS];
  size_t h_pwrbuf_size[MAX_OUTPUTS];

  // Array of Nd values (number of spectra per dump)
  unsigned int Nds[MAX_OUTPUTS];

  // Fields below here are not normally needed at all by the client

  unsigned int Ntmax; // Maximum Nt value
  void * gpu_ctx; // Host pointer to opaque/private GPU specific context
};

// enum for output mode
typedef enum {
  RAWSPEC_FILE,
  RAWSPEC_NET
} rawspec_output_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

// Returns a pointer to a string containing the rawspec version
const char * rawspec_version_string();

// Sets ctx->Ntmax.
// Allocates host and device buffers based on the ctx->N values.
// Allocated buffers are not cleared, except for the power outbut buffers.
// Allocates and sets the ctx->rawspec_gpu_ctx field.
// Creates CuFFT plans.
// Creates streams.
// Returns 0 on success, non-zero on error.
int rawspec_initialize(rawspec_context * ctx);

// Frees host and device buffers based on the ctx->N values.
// Frees and sets the ctx->rawspec_gpu_ctx field.
// Destroys CuFFT plans.
// Destroys streams.
// Returns 0 on success, non-zero on error.
void rawspec_cleanup(rawspec_context * ctx);

// Copy `num_blocks` consecutive blocks from `ctx->h_blkbufs` to GPU input
// buffer.  Starts with source block `src_idx` to destination block `dst_idx`.
// Returns 0 on success, non-zero on error.
int rawspec_copy_blocks_to_gpu(rawspec_context * ctx,
    off_t src_idx, off_t dst_idx, size_t num_blocks);

// Launches FFTs of data in input buffer.  Whenever an output product
// integration is complete, the power spectrum is copied to the host power
// output buffer and the user provided callback, if any, is called.  This
// function returns zero on success or non-zero if an error is encountered.
//
// The direction of the FFT is determined by the fft_dir parameter.  If fft_dir
// is less than or equal to zero, an inverse (aka backward) transform is
// performed, otherwise a forward transform is performed.
//
// Processing occurs asynchronously.  Use `rawspec_check_for_completion` to
// see how many output products have completed or
// `rawspec_wait_for_completion` to wait for all output products to be
// complete.  New data should NOT be copied to the GPU until
// `rawspec_check_for_completion` returns `ctx->No` or
// `rawspec_wait_for_completion` returns 0.
int rawspec_start_processing(rawspec_context * ctx, int fft_dir);

// Waits for any processing to finish, then clears output power buffers and
// resets inbuf_count to 0.  Returns 0 on success, non-zero on error.
int rawspec_reset_integration(rawspec_context * ctx);

// Returns the number of output products that are complete for the current
// input buffer.  More precisely, it returns the number of output products that
// are no longer processing (or never were processing) the input buffer.
unsigned int rawspec_check_for_completion(rawspec_context * ctx);

// Waits for any pending output products to be compete processing the current
// input buffer.  Returns zero when complete, non-zero on error.
int rawspec_wait_for_completion(rawspec_context * ctx);

#ifdef __cplusplus
}
#endif

#endif // _MYGPUSPEC_H_
