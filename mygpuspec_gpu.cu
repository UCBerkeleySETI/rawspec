#include "mygpuspec.h"

#include <cufft.h>
#include <cufftXt.h>
#include <helper_cuda.h>

#define NO_PLAN ((cufftHandle)-1)

#define PRINT_ERRMSG(error)                  \
  fprintf(stderr, "got error %s at %s:%d\n", \
      _cudaGetErrorEnum(error),  \
      __FILE__, __LINE__)

// CPU context structure
typedef struct {
  char2 * d_fft_in; // Device pointer to FFT input buffer
  cufftComplex * d_fft_out[4]; // Array of device pointers to FFT output buffers
  float * d_pwr_out[4]; // Array of device pointers to power buffers
  cufftHandle plan[4]; // Array of handles to FFT plans
} mygpuspec_gpu_context;

// Texture declarations
texture<char, 2, cudaReadModeNormalizedFloat> char_tex;

__device__ cufftComplex load_callback(void *p_v_in,
                                      size_t offset,
                                      void *p_v_user,
                                      void *p_v_shared)
{
  cufftComplex c;
  offset += (cufftComplex *)p_v_in - (cufftComplex *)p_v_user;
  c.x = tex2D(char_tex, ((2*offset  ) & 0x7fff), ((  offset  ) >> 14));
  c.y = tex2D(char_tex, ((2*offset+1) & 0x7fff), ((2*offset+1) >> 15));
  return c;
}

__device__ void store_callback(void *p_v_out,
                               size_t offset,
                               cufftComplex element,
                               void *p_v_user,
                               void *p_v_shared)
{
  float pwr = element.x * element.x + element.y * element.y;
  ((float *)p_v_user)[offset] += pwr;
}

__device__ cufftCallbackLoadC d_cufft_load_callback = load_callback;
__device__ cufftCallbackStoreC d_cufft_store_callback = store_callback;

// Sets ctx->Ntmax.
// Allocates host and device buffers based on the ctx->N values.
// Allocates and sets the ctx->mygpuspec_gpu_ctx field.
// Creates CuFFT plans.
// Returns 0 on success, non-zero on error.
int mygpuspec_initialize(mygpuspec_context * ctx)
{
  int i;
  size_t inbuf_size;
  cudaError_t cuda_rc;
  cufftResult cufft_rc;

  // Host copies of cufft callback pointers
  cufftCallbackLoadC h_cufft_load_callback;
  cufftCallbackStoreC h_cufft_store_callback;

  // Validate ctx->No
  if(ctx->No == 0 || ctx->No > MAX_OUTPUTS) {
    fprintf(stderr, "output products must be in range [1..%d], not %d\n",
        MAX_OUTPUTS, ctx->No);
    return 1;
  }

  // Validate Ntpb
  if(ctx->Ntpb == 0) {
    fprintf(stderr, "number of time samples per block cannot be zero\n");
    return 1;
  }


  // Determine Ntmax (and validate Nts)
  ctx->Ntmax = 0;
  for(i=0; i<ctx->No; i++) {
    if(ctx->Nts[i] == 0) {
      fprintf(stderr, "Nts[%d] cannot be 0\n", i);
      return 1;
    }
    if(ctx->Ntmax < ctx->Nts[i]) {
      ctx->Ntmax = ctx->Nts[i];
    }
  }
  // Validate that all Nts are factors of Ntmax.  This constraint helps
  // simplify input buffer management.
  for(i=0; i<ctx->No; i++) {
    if(ctx->Ntmax % ctx->Nts[i] != 0) {
      fprintf(stderr, "Nts[%d] (%u) is not a factor of Ntmax (%u)\n",
          i, ctx->Nts[i], ctx->Ntmax);
      return 1;
    }
  }

  // Validate/calculate Nb
  // If ctx->Nb is given by caller (i.e. is non-zero)
  if(ctx->Nb != 0) {
    // Validate that Ntmax is a factor of (Nb * Ntpb)
    if((ctx->Nb * ctx->Ntpb) % ctx->Ntmax != 0) {
      fprintf(stderr,
          "Ntmax (%u) is not a factor of Nb*Ntpb (%u * %u = %u)\n",
          ctx->Ntmax, ctx->Nb, ctx->Ntpb, ctx->Nb*ctx->Ntpb);
      return 1;
    }
  } else {
    // Calculate Nb
    // If Ntmax is less than one block
    if(ctx->Ntmax < ctx->Ntpb) {
      // Validate that Ntmax is a factor of Ntpb
      if(ctx->Ntpb % ctx->Ntmax != 0) {
        fprintf(stderr, "Ntmax (%u) is not a factor of Ntpb (%u)\n",
            ctx->Ntmax, ctx->Ntpb);
        return 1;
      }
      ctx->Nb = 1;
    } else {
      // Validate that Ntpb is factor of Ntmax
      if(ctx->Ntmax % ctx->Ntpb != 0) {
        fprintf(stderr, "Ntpb (%u) is not a factor of Nmax (%u)\n",
            ctx->Ntpb, ctx->Ntmax);
        return 1;
      }
      ctx->Nb = ctx->Ntmax / ctx->Ntpb;
    }
  }

  // Null out all pointers
  ctx->h_blkbufs = NULL;
  for(i=0; i < MAX_OUTPUTS; i++) {
    ctx->h_pwrbuf[i] = NULL;
  }
  ctx->gpu_ctx = NULL;

  // Alllocate host buffers
  ctx->h_blkbufs = (char **)malloc(ctx->Nb * sizeof(char *));
  for(i=0; i < ctx->Nb; i++) {
    // Block buffer can use write combining
    cuda_rc = cudaHostAlloc(&ctx->h_blkbufs[i],
                       ctx->Ntpb*ctx->Np*ctx->Nc*sizeof(char2),
                       cudaHostAllocWriteCombined);
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      return 1;
    }
  }

  for(i=0; i < ctx->No; i++) {
    // TODO For small Nt values, it's probbaly more efficient to buffer
    // multiple power spectra in the output buffer, but this requires a little
    // more overhead so it is deferred for now.
    cuda_rc = cudaHostAlloc(&ctx->h_pwrbuf[i],
                       ctx->Nts[i]*ctx->Nc*sizeof(float),
                       cudaHostAllocDefault);
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      mygpuspec_cleanup(ctx);
      return 1;
    }
  }

  // Allocate GPU context
  mygpuspec_gpu_context * gpu_ctx = (mygpuspec_gpu_context *)malloc(sizeof(mygpuspec_gpu_context));

  if(!gpu_ctx) {
    mygpuspec_cleanup(ctx);
    return 1;
  }

  // Store pointer to gpu_ctx in ctx
  ctx->gpu_ctx = gpu_ctx;

  // NULL out pointers (and invalidate plans)
  gpu_ctx->d_fft_in = NULL;
  for(i=0; i<MAX_OUTPUTS; i++) {
    gpu_ctx->d_fft_out[i] = NULL;
    gpu_ctx->d_pwr_out[i] = NULL;
    gpu_ctx->plan[i] = NO_PLAN;
  }

  // Allocate buffers

  // FFT input buffer
  // The input buffer is padded to the next multiple of 32KB to facilitate 2D
  // texture lookups by treating the input buffer as a 2D array that is 32KB
  // wide.
  inbuf_size = ctx->Ntmax*ctx->Np*ctx->Nc*sizeof(char2);
  if((inbuf_size & 0x7fff) != 0) {
    // Round up to next multiple of 32KB
    inbuf_size = (inbuf_size & ~0x7fff) + 0x8000;
  }

  cuda_rc = cudaMalloc(&gpu_ctx->d_fft_in, inbuf_size);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    mygpuspec_cleanup(ctx);
    return 1;
  }

  // Bind texture to device input buffer
  // Width is 32KB, height is inbuf_size/32KB, pitch is 32KB
  cuda_rc = cudaBindTexture2D(NULL, char_tex, gpu_ctx->d_fft_in,
                              1<<15, inbuf_size>>15, 1<<15);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    mygpuspec_cleanup(ctx);
    return 1;
  }

  // For each output product
  for(i=0; i < ctx->No; i++) {
    // FFT output buffer
    cuda_rc = cudaMalloc(&gpu_ctx->d_fft_out[i], ctx->Nts[i]*ctx->Nc*sizeof(cufftComplex));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      mygpuspec_cleanup(ctx);
      return 1;
    }
    // Power output buffer
    cuda_rc = cudaMalloc(&gpu_ctx->d_pwr_out[i], ctx->Nts[i]*ctx->Nc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      mygpuspec_cleanup(ctx);
      return 1;
    }
    // Clear power output buffer
    cuda_rc = cudaMemset(gpu_ctx->d_pwr_out[i], 0, ctx->Nts[i]*ctx->Nc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      mygpuspec_cleanup(ctx);
      return 1;
    }
  }

  // Get host pointers to cufft callbacks
  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_load_callback,
                                 d_cufft_load_callback,
                                 sizeof(h_cufft_load_callback));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    mygpuspec_cleanup(ctx);
    return 1;
  }

  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_store_callback,
                                 d_cufft_store_callback,
                                 sizeof(h_cufft_store_callback));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    mygpuspec_cleanup(ctx);
    return 1;
  }

  // Generate FFT plans and associate callbacks
  for(i=0; i < ctx->No; i++) {
    // Make the plan
    cufft_rc = cufftPlanMany(&gpu_ctx->plan[i],   // *plan handle
                             1,                   // rank
                             (int *)&ctx->Nts[i], // *n
                             (int *)&ctx->Nts[i], // *inembed (unused for 1d)
                             ctx->Np,             // istride
                             ctx->Nts[i]*ctx->Np, // idist
                             (int *)&ctx->Nts[i], // *onembed (unused for 1d)
                             1,                   // ostride
                             ctx->Nts[i],         // odist
                             CUFFT_C2C,           // type
                             ctx->Nc              // batch
                            );

    if(cufft_rc != CUFFT_SUCCESS) {
      PRINT_ERRMSG(cufft_rc);
      mygpuspec_cleanup(ctx);
      return 1;
    }

    // Now associate the callbacks with the plan.
    cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i],
                                  (void **)&h_cufft_load_callback,
                                  CUFFT_CB_LD_COMPLEX,
                                  (void **)&gpu_ctx->d_fft_in);
    if(cufft_rc != CUFFT_SUCCESS) {
      PRINT_ERRMSG(cufft_rc);
      mygpuspec_cleanup(ctx);
      return 1;
    }

    cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i],
                                  (void **)&h_cufft_store_callback,
                                  CUFFT_CB_ST_COMPLEX,
                                  (void **)&gpu_ctx->d_pwr_out[i]);
    if(cufft_rc != CUFFT_SUCCESS) {
      PRINT_ERRMSG(cufft_rc);
      mygpuspec_cleanup(ctx);
      return 1;
    }
  }

  return 0;
}

// Frees host and device buffers based on the ctx->N values.
// Frees and sets the ctx->mygpuspec_gpu_ctx field.
// Destroys CuFFT plans.
void mygpuspec_cleanup(mygpuspec_context * ctx)
{
  int i;
  mygpuspec_gpu_context * gpu_ctx;

  if(ctx->h_blkbufs) {
    for(i=0; i < ctx->Nb; i++) {
      cudaFreeHost(ctx->h_blkbufs[i]);
    }
    free(ctx->h_blkbufs);
    ctx->h_blkbufs = NULL;
  }

  for(i=0; i<MAX_OUTPUTS; i++) {
    if(ctx->h_pwrbuf[i]) {
      cudaFreeHost(ctx->h_pwrbuf[i]);
      ctx->h_pwrbuf[i] = NULL;
    }
  }

  if(ctx->gpu_ctx) {
    gpu_ctx = (mygpuspec_gpu_context *)ctx->gpu_ctx;

    if(gpu_ctx->d_fft_in) {
      cudaFree(gpu_ctx->d_fft_in);
    }

    for(i=0; i<MAX_OUTPUTS; i++) {
      if(gpu_ctx->d_fft_out[i]) {
        cudaFree(gpu_ctx->d_fft_out[i]);
      }
      if(gpu_ctx->d_pwr_out[i]) {
        cudaFree(gpu_ctx->d_pwr_out[i]);
      }
      if(gpu_ctx->plan[i] != NO_PLAN) {
        cufftDestroy(gpu_ctx->plan[i]);
      }
    }

    free(ctx->gpu_ctx);
    ctx->gpu_ctx = NULL;
  }
}

// Copy `ctx->h_blkbufs` to GPU input buffer.
// Returns 0 on success, non-zero on error.
int mygpuspec_copy_blocks_to_gpu(mygpuspec_context * ctx)
{
  int b;
  cudaError_t rc;
  mygpuspec_gpu_context * gpu_ctx = (mygpuspec_gpu_context *)ctx->gpu_ctx;

  // TODO Store in GPU context?
  size_t width = ctx->Ntpb * ctx->Np * sizeof(char2);

  for(b=0; b < ctx->Nb; b++) {
    rc = cudaMemcpy2D(gpu_ctx->d_fft_in + b * width / sizeof(char2),
                      ctx->Nb * width,   // dpitch
                      ctx->h_blkbufs[b], // *src
                      width,             // spitch
                      width,             // width
                      ctx->Nc,           // height
                      cudaMemcpyHostToDevice);

    if(rc != cudaSuccess) {
      PRINT_ERRMSG(rc);
      return 1;
    }
  }

  return 0;
}
