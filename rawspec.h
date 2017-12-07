#ifndef _MYGPUSPEC_H_
#define _MYGPUSPEC_H_

#define MAX_OUTPUTS (4)

// Forward declaration
typedef struct rawspec_context_s rawspec_context;

typedef void (* rawspec_dump_callback_t)(rawspec_context * ctx,
                                           int output_product);

// Structure for holding the context.
struct rawspec_context_s {
  unsigned int No; // Number of output products (max MAX_OUTPUTS)
  unsigned int Np; // Number of polarizations
  unsigned int Nc; // Number of coarse channels
  unsigned int Ntpb; // Number of time samples per block

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
  // Nb is the number of input blocks per GPU input buffer.
  // Set to zero to have it calculated as Ntmax/Ntpb.
  unsigned int Nb;
  // dump_callback is a pointer to a user-supplied output callback function.
  // This function will be called when one of of the output power buffers in
  // h_pwrbuf[] has new data to be written to disk.
  rawspec_dump_callback_t dump_callback;

  // Fields above here should be specified by client.  Fields below here are
  // managed by library.

  // Array of host pointers to Nb block buffers.
  // The size, in bytes, is Nc * Ntpb * Np * 2.
  char ** h_blkbufs;

  // Host pointers to the output power buffers.
  // The sizes, in bytes, will be Nc * Nts[i].
  float * h_pwrbuf[MAX_OUTPUTS];

  // Fields below here are not normally needed at all by the client

  unsigned int Ntmax; // Maximum Nt value
  void * gpu_ctx; // Host pointer to GPU specific context
};

#ifdef __cplusplus
extern "C" {
#endif

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
