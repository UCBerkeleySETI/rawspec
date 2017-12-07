#ifndef _MYGPUSPEC_H_
#define _MYGPUSPEC_H_

#define MAX_OUTPUTS (4)

// Forward declaration
typedef struct mygpuspec_context_s mygpuspec_context;

typedef void (* mygpuspec_dump_callback_t)(mygpuspec_context * ctx,
                                           int output_product);

// Structure for holding the context.
struct mygpuspec_context_s {
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
  mygpuspec_dump_callback_t dump_callback;

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
// Allocates and sets the ctx->mygpuspec_gpu_ctx field.
// Creates CuFFT plans.
// Creates streams.
// Returns 0 on success, non-zero on error.
int mygpuspec_initialize(mygpuspec_context * ctx);

// Frees host and device buffers based on the ctx->N values.
// Frees and sets the ctx->mygpuspec_gpu_ctx field.
// Destroys CuFFT plans.
// Destroys streams.
// Returns 0 on success, non-zero on error.
void mygpuspec_cleanup(mygpuspec_context * ctx);

// Copy `ctx->h_blkbufs` to GPU input buffer.
// Returns 0 on success, non-zero on error.
int mygpuspec_copy_blocks_to_gpu(mygpuspec_context * ctx);

// Launches FFTs of data in input buffer.  Whenever an output product
// integration is complete, the power spectrum is copied to the host power
// output buffer and the user provided callback, if any, is called.  This
// function returns zero on success or non-zero if an error is encountered.
//
// Processing occurs asynchronously.  Use `mygpuspec_check_for_completion` to
// see how many output products have completed or
// `mygpuspec_wait_for_completion` to wait for all output products to be
// complete.  New data should NOT be copied to the GPU until
// `mygpuspec_check_for_completion` returns `ctx->No` or
// `mygpuspec_wait_for_completion` returns 0.
int mygpuspec_start_processing(mygpuspec_context * ctx);

// Returns the number of output products that are complete for the current
// input buffer.  More precisely, it returns the number of output products that
// are no longer processing (or never were processing) the input buffer.
unsigned int mygpuspec_check_for_completion(mygpuspec_context * ctx);

// Waits for any pending output products to be compete processing the current
// input buffer.  Returns zero when complete, non-zero on error.
int mygpuspec_wait_for_completion(mygpuspec_context * ctx);

#ifdef __cplusplus
}
#endif

#endif // _MYGPUSPEC_H_
