#ifndef _MYGPUSPEC_H_
#define _MYGPUSPEC_H_

#define MAX_OUTPUTS (4)

// Structure for holding the context.
typedef struct {
  unsigned int No; // Number of output products (max MAX_OUTPUTS)
  unsigned int Np; // Number of polarizations
  unsigned int Nc; // Number of coarse channels
  unsigned int Ntpb; // Number of time samples per block
  unsigned int Nts[MAX_OUTPUTS]; // Array of Nt values
  unsigned int Nas[MAX_OUTPUTS]; // Array of Na values

  // Fields above here should be specified by client.  Fields below here are
  // managed by library.  The Nb field and the host pointers below will be used
  // to pass data to and from the GPU.

  unsigned int Nb; // Number of blocks needed for complete input buffer

  // Host pointer to block buffer.
  // The size, in bytes, is Nc * Ntpb * Np * 2.
  char * h_blkbuf;

  // Host pointers to the output power buffers.
  // The sizes, in bytes, will be Nc * Nts[i].
  float * h_pwrbuf[MAX_OUTPUTS];

  // Fields below here are not normally needed at all by the client

  unsigned int Ntmax; // Maximum Nt value
  void * gpu_ctx; // Host pointer to GPU specific context
} mygpuspec_context;

#ifdef __cplusplus
extern "C" {
#endif

// Sets ctx->Ntmax.
// Allocates host and device buffers based on the ctx->N values.
// Allocates and sets the ctx->mygpuspec_gpu_ctx field.
// Creates CuFFT plans.
// Returns 0 on success, non-zero on error.
int mygpuspec_initialize(mygpuspec_context * ctx);

// Frees host and device buffers based on the ctx->N values.
// Frees and sets the ctx->mygpuspec_gpu_ctx field.
// Destroys CuFFT plans.
// Returns 0 on success, non-zero on error.
void mygpuspec_cleanup(mygpuspec_context * ctx);

#ifdef __cplusplus
}
#endif

#endif // _MYGPUSPEC_H_
