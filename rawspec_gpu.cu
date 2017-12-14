#include "rawspec.h"

#include <cufft.h>
#include <cufftXt.h>
#include <helper_cuda.h>

#define NO_PLAN   ((cufftHandle)-1)
#define NO_STREAM ((cudaStream_t)-1)

#define PRINT_ERRMSG(error)                  \
  fprintf(stderr, "got error %s at %s:%d\n", \
      _cudaGetErrorEnum(error),  \
      __FILE__, __LINE__)

// Stream callback data structure
typedef struct {
  rawspec_context * ctx;
  int output_product;
} dump_cb_data_t;

// GPU context structure
typedef struct {
  // Device pointer to FFT input buffer
  char2 * d_fft_in;
  // Array of device pointers to FFT output buffers
  cufftComplex * d_fft_out[MAX_OUTPUTS];
  // Array of device pointers to power buffers
  float * d_pwr_out[MAX_OUTPUTS];
  // Array of handles to FFT plans
  cufftHandle plan[MAX_OUTPUTS];
  // Array of Ns values (number of specta (FFTs) per input buffer for Nt)
  unsigned int Nss[MAX_OUTPUTS];
  // Array of cudaStream_t values
  cudaStream_t stream[MAX_OUTPUTS];
  // Array of grids for accumulate kernel
  dim3 grid[MAX_OUTPUTS];
  // Array of number of threads to use per block for accumulate kernel
  int nthreads[MAX_OUTPUTS];
  // Array of Nd values (number of spectra per dump)
  unsigned int Nds[MAX_OUTPUTS];
  // Array of Ni values (number of input buffers per dump)
  unsigned int Nis[MAX_OUTPUTS];
  // A count of the number of input buffers processed
  unsigned int inbuf_count;
  // Array of dump_cb_data_t structures for dump callback
  dump_cb_data_t dump_cb_data[MAX_OUTPUTS];
  // Flag indicating that the caller is managing the input block buffers
  // Non-zero when caller is managing (i.e. allocating and freeing) the
  // buffers; zero when we are.
  int caller_managed;
} rawspec_gpu_context;

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

#define MAX_THREADS (1024)

// Accumulate kernel
__global__ void accumulate(float * pwr_buf, unsigned int Na, size_t xpitch, size_t ypitch, size_t zpitch)
{
  unsigned int i;

  // TODO Add check for past end of spectrum

  off_t offset0 = blockIdx.z * zpitch
                + blockIdx.y * ypitch
                + blockIdx.x * MAX_THREADS
                + threadIdx.x;

  off_t offset = offset0;

  float sum = pwr_buf[offset];

  for(i=1; i<Na; i++) {
    offset += xpitch;
    sum += pwr_buf[offset];
  }

  pwr_buf[offset0] = sum;
}

// Stream callback function that is called right after an output product's GPU
// power buffer has been copied to the host power buffer.
static void CUDART_CB dump_stream_callback(cudaStream_t stream,
                                           cudaError_t status,
                                           void *data)
{
  dump_cb_data_t * dump_cb_data = (dump_cb_data_t *)data;
  if(dump_cb_data->ctx->dump_callback) {
    dump_cb_data->ctx->dump_callback(dump_cb_data->ctx,
                                     dump_cb_data->output_product);
  }
}

// Sets ctx->Ntmax.
// Allocates host and device buffers based on the ctx->N values.
// Allocates and sets the ctx->gpu_ctx field.
// Creates CuFFT plans.
// Creates streams.
// Returns 0 on success, non-zero on error.
int rawspec_initialize(rawspec_context * ctx)
{
  int i;
  size_t inbuf_size;
  cudaError_t cuda_rc;
  cufftResult cufft_rc;

  // Host copies of cufft callback pointers
  cufftCallbackLoadC h_cufft_load_callback;
  cufftCallbackStoreC h_cufft_store_callback;

  // Validate No
  if(ctx->No == 0 || ctx->No > MAX_OUTPUTS) {
    fprintf(stderr, "output products must be in range [1..%d], not %d\n",
        MAX_OUTPUTS, ctx->No);
    return 1;
  }

  // Validate Np
  if(ctx->Np == 0 || ctx->Np > 2) {
    fprintf(stderr,
        "number of polarizations must be in range [1..2], not %d\n", ctx->Np);
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
    // Cannot calculate Nb for caller-managed h_blkbufs
    if(ctx->h_blkbufs) {
      fprintf(stderr,
          "Must specify number of input blocks when caller-managed\n");
      return 1;
    }

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

  // Validate Nas
  for(i=0; i < ctx->No; i++) {
    if(ctx->Nas[i] == 0) {
      fprintf(stderr, "Nas[%d] cannot be 0\n", i);
      return 1;
    }
    // If mulitple integrations per input buffer
    if(ctx->Nts[i]*ctx->Nas[i] < ctx->Nb*ctx->Ntpb) {
      // Must have integer integrations per input buffer
      if((ctx->Nb * ctx->Ntpb) % (ctx->Nts[i] * ctx->Nas[i]) != 0) {
        fprintf(stderr,
            "Nts[%d] * Nas[%d] (%u * %u) must divide Nb * Ntpb (%u * %u)\n",
            i, i, ctx->Nts[i], ctx->Nas[i], ctx->Nb, ctx->Ntpb);
        return 1;
      }
    } else {
      // Must have integer input buffers per integration
      if((ctx->Nts[i] * ctx->Nas[i]) % (ctx->Nb * ctx->Ntpb) != 0) {
        fprintf(stderr,
            "Nb * Ntpb (%u * %u) must divide Nts[%d] * Nas[%d] (%u * %u)\n",
            ctx->Nb, ctx->Ntpb, i, i, ctx->Nts[i], ctx->Nas[i]);
        return 1;
      }
    }
  }

  // Null out all pointers
  // TODO Add support for client managed host buffers
  for(i=0; i < MAX_OUTPUTS; i++) {
    ctx->h_pwrbuf[i] = NULL;
  }
  ctx->gpu_ctx = NULL;

  // Allocate GPU context
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)malloc(sizeof(rawspec_gpu_context));

  if(!gpu_ctx) {
    rawspec_cleanup(ctx);
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
    gpu_ctx->stream[i] = NO_STREAM;
    gpu_ctx->dump_cb_data[i].ctx = ctx;
    gpu_ctx->dump_cb_data[i].output_product = i;
  }

  // Initialize inbuf_count
  gpu_ctx->inbuf_count = 0;

  if(!ctx->h_blkbufs) {
    // Remember that we (not the caller) are managing these buffers
    // (i.e. we will need to free them when cleaning up).
    gpu_ctx->caller_managed = 0;

    // Alllocate host input block buffers
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
  } else {
    // Remember that the caller is managing these buffers
    // (i.e. we will only need to unregister them when cleaning up).
    gpu_ctx->caller_managed = 1;

    // Register these buffers with CUDA.  It is the caller's responsibility to
    // ensure that the blocks meet memory alignment requirements, etc.
    for(i=0; i < ctx->Nb; i++) {
      cuda_rc = cudaHostRegister(&ctx->h_blkbufs[i],
                         ctx->Ntpb*ctx->Np*ctx->Nc*sizeof(char2),
                         cudaHostRegisterDefault);
      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        return 1;
      }
    }
  }

  // Calculate Ns and allocate host power output buffers
  for(i=0; i < ctx->No; i++) {
    // Ns[i] is number of specta (FFTs) per coarse channel for one input buffer
    // for Nt[i] points per spectra.
    gpu_ctx->Nss[i] = (ctx->Nb * ctx->Ntpb) / ctx->Nts[i];

    // Calculate number of spectra per dump
    gpu_ctx->Nds[i] = gpu_ctx->Nss[i] / ctx->Nas[i];
    if(gpu_ctx->Nds[i] == 0) {
      gpu_ctx->Nds[i] = 1;
    }

    // Calculate number of input buffers per dump
    gpu_ctx->Nis[i] = ctx->Nas[i] / gpu_ctx->Nss[i];
    if(gpu_ctx->Nis[i] == 0) {
      gpu_ctx->Nis[i] = 1;
    }

    // Calculate grid dimensions
    gpu_ctx->grid[i].x = (ctx->Nts[i] + MAX_THREADS - 1) / MAX_THREADS;
    gpu_ctx->grid[i].y = gpu_ctx->Nds[i];
    gpu_ctx->grid[i].z = ctx->Nc;

    // Calculate number of threads per block
    gpu_ctx->nthreads[i] = ctx->Nts[i] < MAX_THREADS ? ctx->Nts[i]
                                                     : MAX_THREADS;

    // Host buffer needs to accommodate the number of integrations that will be
    // dumped at one time (Nd).
    ctx->h_pwrbuf_size[i] = gpu_ctx->Nds[i]*ctx->Nts[i]*ctx->Nc*sizeof(float);
    cuda_rc = cudaHostAlloc(&ctx->h_pwrbuf[i], ctx->h_pwrbuf_size[i],
                       cudaHostAllocDefault);

    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
  }

  // Allocate buffers

  // FFT input buffer
  // The input buffer is padded to the next multiple of 32KB to facilitate 2D
  // texture lookups by treating the input buffer as a 2D array that is 32KB
  // wide.
  inbuf_size = ctx->Nb*ctx->Ntpb*ctx->Np*ctx->Nc*sizeof(char2);
  if((inbuf_size & 0x7fff) != 0) {
    // Round up to next multiple of 32KB
    inbuf_size = (inbuf_size & ~0x7fff) + 0x8000;
  }

  cuda_rc = cudaMalloc(&gpu_ctx->d_fft_in, inbuf_size);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  // Bind texture to device input buffer
  // Width is 32KB, height is inbuf_size/32KB, pitch is 32KB
  cuda_rc = cudaBindTexture2D(NULL, char_tex, gpu_ctx->d_fft_in,
                              1<<15, inbuf_size>>15, 1<<15);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  // For each output product
  for(i=0; i < ctx->No; i++) {
    // FFT output buffer
    cuda_rc = cudaMalloc(&gpu_ctx->d_fft_out[i], ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(cufftComplex));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
    // Power output buffer
    cuda_rc = cudaMalloc(&gpu_ctx->d_pwr_out[i], ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
    // Clear power output buffer
    cuda_rc = cudaMemset(gpu_ctx->d_pwr_out[i], 0, ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
  }

  // Get host pointers to cufft callbacks
  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_load_callback,
                                 d_cufft_load_callback,
                                 sizeof(h_cufft_load_callback));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_store_callback,
                                 d_cufft_store_callback,
                                 sizeof(h_cufft_store_callback));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  // Generate FFT plans and associate callbacks
  for(i=0; i < ctx->No; i++) {
    // Make the plan
    cufft_rc = cufftPlanMany(&gpu_ctx->plan[i],      // *plan handle
                             1,                      // rank
                             (int *)&ctx->Nts[i],    // *n
                             (int *)&ctx->Nts[i],    // *inembed (unused for 1d)
                             ctx->Np,                // istride
                             ctx->Nts[i]*ctx->Np,    // idist
                             (int *)&ctx->Nts[i],    // *onembed (unused for 1d)
                             1,                      // ostride
                             ctx->Nts[i],            // odist
                             CUFFT_C2C,              // type
                             gpu_ctx->Nss[i]*ctx->Nc // batch
                            );

    if(cufft_rc != CUFFT_SUCCESS) {
      PRINT_ERRMSG(cufft_rc);
      rawspec_cleanup(ctx);
      return 1;
    }

    // Now associate the callbacks with the plan.
    cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i],
                                  (void **)&h_cufft_load_callback,
                                  CUFFT_CB_LD_COMPLEX,
                                  (void **)&gpu_ctx->d_fft_in);
    if(cufft_rc != CUFFT_SUCCESS) {
      PRINT_ERRMSG(cufft_rc);
      rawspec_cleanup(ctx);
      return 1;
    }

    cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i],
                                  (void **)&h_cufft_store_callback,
                                  CUFFT_CB_ST_COMPLEX,
                                  (void **)&gpu_ctx->d_pwr_out[i]);
    if(cufft_rc != CUFFT_SUCCESS) {
      PRINT_ERRMSG(cufft_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
  }

  // Create streams and associate with plans
  for(i=0; i < ctx->No; i++) {
    cuda_rc = cudaStreamCreateWithFlags(&gpu_ctx->stream[i], cudaStreamNonBlocking);
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }

    cufft_rc = cufftSetStream(gpu_ctx->plan[i], gpu_ctx->stream[i]);
    if(cufft_rc != CUFFT_SUCCESS) {
      PRINT_ERRMSG(cufft_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
  }

  return 0;
}

// Frees host and device buffers based on the ctx->N values.
// Frees and sets the ctx->rawspec_gpu_ctx field.
// Destroys CuFFT plans.
// Destroys streams.
void rawspec_cleanup(rawspec_context * ctx)
{
  int i;
  rawspec_gpu_context * gpu_ctx;

  for(i=0; i<MAX_OUTPUTS; i++) {
    if(ctx->h_pwrbuf[i]) {
      cudaFreeHost(ctx->h_pwrbuf[i]);
      ctx->h_pwrbuf[i] = NULL;
    }
  }

  if(ctx->gpu_ctx) {
    gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

    if(gpu_ctx->caller_managed) {
      for(i=0; i < ctx->Nb; i++) {
        cudaHostUnregister(ctx->h_blkbufs[i]);
      }
    } else {
      if(ctx->h_blkbufs) {
        for(i=0; i < ctx->Nb; i++) {
          cudaFreeHost(ctx->h_blkbufs[i]);
        }
        free(ctx->h_blkbufs);
        ctx->h_blkbufs = NULL;
      }
    }

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
      if(gpu_ctx->stream[i] != NO_STREAM) {
        cudaStreamDestroy(gpu_ctx->stream[i]);
      }
    }

    free(ctx->gpu_ctx);
    ctx->gpu_ctx = NULL;
  }
}

// Copy `ctx->h_blkbufs` to GPU input buffer.
// Returns 0 on success, non-zero on error.
int rawspec_copy_blocks_to_gpu(rawspec_context * ctx,
    off_t src_idx, off_t dst_idx, size_t num_blocks)
{
  int b;
  off_t sblk;
  off_t dblk;
  cudaError_t rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  // TODO Store in GPU context?
  size_t width = ctx->Ntpb * ctx->Np * sizeof(char2);

  for(b=0; b < num_blocks; b++) {
    sblk = (src_idx + b) % ctx->Nb;
    dblk = (dst_idx + b) % ctx->Nb;

    rc = cudaMemcpy2D(gpu_ctx->d_fft_in + dblk * width / sizeof(char2),
                      ctx->Nb * width,      // dpitch
                      ctx->h_blkbufs[sblk], // *src
                      width,                // spitch
                      width,                // width
                      ctx->Nc,              // height
                      cudaMemcpyHostToDevice);

    if(rc != cudaSuccess) {
      PRINT_ERRMSG(rc);
      return 1;
    }
  }

  return 0;
}

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
int rawspec_start_processing(rawspec_context * ctx, int fft_dir)
{
  int i;
  int p;
  int d;
  float * dst;
  size_t dpitch;
  float * src;
  size_t spitch;
  size_t width;
  size_t height;
  cufftHandle plan;
  cudaStream_t stream;
  cudaError_t cuda_rc;
  cufftResult cufft_rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  // Increment inbuf_count
  gpu_ctx->inbuf_count++;

  // For each output product
  for(i=0; i < ctx->No; i++) {

    // Get plan and stream
    plan   = gpu_ctx->plan[i];
    stream = gpu_ctx->stream[i];

    // For each polarization
    for(p=0; p < ctx->Np; p++) {
      // Add FFT to stream
      cufft_rc = cufftExecC2C(plan,
                              ((cufftComplex *)gpu_ctx->d_fft_in) + p,
                              gpu_ctx->d_fft_out[i],
                              fft_dir <= 0 ? CUFFT_INVERSE : CUFFT_FORWARD);

      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        return 1;
      }
    }

    // If time to dump
    if(gpu_ctx->inbuf_count % gpu_ctx->Nis[i] == 0) {
      // If the number of spectra to dump per input buffer is less than the
      // number of spectra per input buffer, then we need to accumulate the
      // sub-integrations together.
      if(gpu_ctx->Nds[i] < gpu_ctx->Nss[i]) {
        accumulate<<<gpu_ctx->grid[i],
                     gpu_ctx->nthreads[i],
                     0, stream>>>(gpu_ctx->d_pwr_out[i],
                                  ctx->Nas[i],
                                  ctx->Nts[i],
                                  ctx->Nas[i]*ctx->Nts[i],
                                  ctx->Nb*ctx->Ntpb);
      }

      // Copy integrated power spectra (or spectrum) to host.  This is done as
      // two 2D copies to get channel 0 in the center of the spectrum.  Special
      // care is taken in the unlikely event that Nt is odd.
      src    = gpu_ctx->d_pwr_out[i];
      dst    = ctx->h_pwrbuf[i];
      spitch = gpu_ctx->Nss[i] * ctx->Nts[i] * sizeof(float);
      dpitch = ctx->Nts[i] * sizeof(float);
      height = ctx->Nc;

      for(d=0; d<gpu_ctx->Nds[i]; d++) {

        // Lo to hi
        width  = ((ctx->Nts[i]+1) / 2) * sizeof(float);
        cuda_rc = cudaMemcpy2DAsync(dst + ctx->Nts[i]/2,
                                    dpitch,
                                    src,
                                    spitch,
                                    width,
                                    height,
                                    cudaMemcpyDeviceToHost,
                                    stream);

        if(cuda_rc != cudaSuccess) {
          PRINT_ERRMSG(cuda_rc);
          rawspec_cleanup(ctx);
          return 1;
        }

        // Hi to lo
        width  = (ctx->Nts[i] / 2) * sizeof(float);
        cuda_rc = cudaMemcpy2DAsync(dst,
                                    dpitch,
                                    src + (ctx->Nts[i]+1) / 2,
                                    spitch,
                                    width,
                                    height,
                                    cudaMemcpyDeviceToHost,
                                    stream);

        if(cuda_rc != cudaSuccess) {
          PRINT_ERRMSG(cuda_rc);
          rawspec_cleanup(ctx);
          return 1;
        }

        // Increment src and dst pointers
        src += ctx->Nts[i] * ctx->Nas[i];
        dst += ctx->Nts[i] * ctx->Nc;
      }

      // Add stream callback
      cuda_rc = cudaStreamAddCallback(stream, dump_stream_callback,
                                      (void *)&gpu_ctx->dump_cb_data[i], 0);

      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        return 1;
      }

      // Add power buffer clearing cudaMemset call to stream
      cuda_rc = cudaMemsetAsync(gpu_ctx->d_pwr_out[i], 0,
                                gpu_ctx->Nds[i]*ctx->Nts[i]*ctx->Nc*sizeof(float),
                                stream);

      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        return 1;
      }

    } // If time to dump
  } // For each output product

  return 0;
}

// Waits for any processing to finish, then clears output power buffers and
// resets inbuf_count to 0.  Returns 0 on success, non-zero on error.
int rawspec_reset_integration(rawspec_context * ctx)
{
  int i;
  cudaError_t cuda_rc;
  rawspec_gpu_context * gpu_ctx;

  // Mae sure gpu_ctx exists
  if(!ctx->gpu_ctx) {
    return 1;
  }
  gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  // Wait for any/all pending work to complete
  rawspec_wait_for_completion(ctx);

  // For each output product
  for(i=0; i < ctx->No; i++) {
    // Clear power output buffer
    cuda_rc = cudaMemset(gpu_ctx->d_pwr_out[i], 0, ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      return 0;
    }
  }

  // Reset inbuf_count
  gpu_ctx->inbuf_count = 0;

  return 0;
}

// Returns the number of output products that are complete for the current
// input buffer.  More precisely, it returns the number of output products that
// are no longer processing (or never were processing) the input buffer.
unsigned int rawspec_check_for_completion(rawspec_context * ctx)
{
  int i;
  int num_complete = 0;
  cudaError_t rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  for(i=0; i<ctx->No; i++) {
    rc = cudaStreamQuery(gpu_ctx->stream[i]);
    if(rc == cudaSuccess) {
      num_complete++;
    }
  }

  return num_complete;
}

// Waits for any pending output products to be compete processing the current
// input buffer.  Returns zero when complete, non-zero on error.
int rawspec_wait_for_completion(rawspec_context * ctx)
{
  int i;
  cudaError_t rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  for(i=0; i < ctx->No; i++) {
    rc = cudaStreamSynchronize(gpu_ctx->stream[i]);
    if(rc != cudaSuccess) {
      return 1;
    }
  }

  return 0;
}
