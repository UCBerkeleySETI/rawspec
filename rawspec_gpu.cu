#include "rawspec.h"
#include "rawspec_version.h"

#include <cufft.h>
#include <cufftXt.h>
#include <helper_cuda.h>

#define VERBOSE_ALLOC

#define NO_PLAN   ((cufftHandle)-1)
#define NO_STREAM ((cudaStream_t)-1)

#define LOAD_TEXTURE_WIDTH_POWER 15
#define LOAD_TEXTURE_WIDTH_MASK (unsigned int)((1<<LOAD_TEXTURE_WIDTH_POWER)-1)

#define MIN(a,b) ((a < b) ? (a) : (b))

#define PRINT_ERRMSG(error)                  \
  fprintf(stderr, "got error %s at %s:%d\n", \
      _cudaGetErrorEnum(error),  \
      __FILE__, __LINE__); fflush(stderr)

// Stream callback data structure
typedef struct {
  rawspec_context * ctx;
  int output_product;
} dump_cb_data_t;

// In full-Stokes mode (Npolout == -4) or full-pol mode (Npolout == 4), the
// CuFFT store callbacks are different depending on whether it is for pol0 or
// pol1:
//
// For full-Stokes mode, the store_callback_pol0_iquv function stores the
// voltage data into the first half of the 2x-sized FFT output buffer and
// accummulates (i.e. adds) the pol0 power into the first two quarters (I and
// Q) of the 4x-sized power buffer.
//
// For full-Stokes mode, the store_callback_pol1_iquv function accumulates
// (i.e. adds) the pol1 power into the first quarter of the 4x-sized power
// buffer (I), negatively accumulates (i.e. subtracts) the pol1 power into the
// second quarter of the 4x-sized power buffer (Q), reads the corresponding
// pol0 voltage from the first half of the 2x-sized FFT output buffer, and
// accumulates the complex pol0-pol1 power in the third (U) and fourth (V)
// quarters of the 4x-sized power buffer.
//
// For full-pol mode, the store_callback_pol0 function stores the voltage data
// into the first half of the 2x-sized FFT output buffer and accummulates the
// pol0 power into the first quarter of the 4x-sized power buffer.
//
// For full-pol mode, the store_callback_pol1 function accumulates the pol1
// power into the second quarter of the 4x-sized power buffer, reads the
// corresponding pol0 voltage from the first half of the 2x-sized FFT output
// buffer, accumulates the complex pol0-pol1 power in the third (real) and
// fourth (imaginary) quarters of the 4x-sized power buffer.
//
// We use a "store_cb_data_t" structure to pass device pointers to the
// various buffers involved.
typedef struct {
  cufftComplex * fft_out_pol0;
  float * pwr_buf_p00_i;
  float * pwr_buf_p11_q;
  float * pwr_buf_p01_re_u;
  float * pwr_buf_p01_im_v;
} store_cb_data_t;

// GPU context structure
typedef struct {
  // Device pointer to FFT input buffer
  char * d_fft_in;
  // Device pointer to complex4 expansion LUT
  char2 * d_comp4_exp_LUT;
  // Device pointer to intermediary buffer for expansion of complex4 samples
  char * d_blk_expansion_buf;
  // Device pointer to FFT output buffer
  cufftComplex * d_fft_out;
  // Array of device pointers to power buffers
  float * d_pwr_out[MAX_OUTPUTS];
  // Array of device pointers to incoherent-sum buffers
  float * d_ics_out[MAX_OUTPUTS];
  float * d_Aws;
  // Array of handles to FFT plans.
  // Each output product gets a pair of plans (one for each pol).
  cufftHandle plan[MAX_OUTPUTS][2];
  // Array of device pointers to store_cb_data_t structures
  // (one per output product)
  store_cb_data_t *d_scb_data[MAX_OUTPUTS];
  // Device pointer to work area (shared by all plans!)
  void * d_work_area;
  // Size of work area
  size_t work_size;
  // Array of Ns values (number of specta (FFTs) per input buffer for Nt)
  unsigned int Nss[MAX_OUTPUTS];
  // Compute stream (as opposed to a "copy stream")
  cudaStream_t compute_stream;
  // Array of grids for accumulate kernel
  dim3 grid[MAX_OUTPUTS];
  // Array of number of threads to use per block for accumulate kernel
  int nthreads[MAX_OUTPUTS];
  // Array of Ni values (number of input buffers per dump)
  unsigned int Nis[MAX_OUTPUTS];
  // A count of the number of input buffers processed
  unsigned int inbuf_count;
  // Array of dump_cb_data_t structures for dump callback
  dump_cb_data_t dump_cb_data[MAX_OUTPUTS];
  // CUDA Texture Object used to convert from integer to floating point
  cudaTextureObject_t tex_obj;
  // CUDA Texture Object used to convert from complex4bit byte data to complex8bit short data
  cudaTextureObject_t comp4_exp_tex_obj;
  // Flag indicating that the caller is managing the input block buffers
  // Non-zero when caller is managing (i.e. allocating and freeing) the
  // buffers; zero when we are.
  int caller_managed;
  // This is a commonly used value to stride between channels within GUPPI input-buffers,
  // the dimensionality of which is [channel (slowest), time, polarisation (fastest)]:
  // (ctx->Ntpb * ctx->Np * 2 /*complex*/ * ctx->Nbps)/8
  size_t guppi_channel_stride;
} rawspec_gpu_context;

// Device-side texture object declaration
__device__ cudaTextureObject_t d_tex_obj;
__device__ cudaTextureObject_t d_comp4_exp_tex_obj;

// The load_callback gets the input value through the texture memory to achieve
// a "for free" mapping of 8-bit integer values into 32-bit float values.
__device__ cufftComplex load_callback(void *p_v_in,
                                      size_t offset,
                                      void *p_v_user,
                                      void *p_v_shared)
{
  cufftComplex c;
  // p_v_in is input buffer (cast to cufftComplex*) plus polarization offset.
  // p_v_user is input buffer.  offset is complex element offset from start of
  // input buffer, but does not include any polarization offset so we compute
  // the polarization offset by subtracting p_v_user from p_v_in and add it to
  // offset.
  offset += (cufftComplex *)p_v_in - (cufftComplex *)p_v_user;
  c.x = tex2D<float>(d_tex_obj, ((2*offset  ) & LOAD_TEXTURE_WIDTH_MASK), ((  offset  ) >> (LOAD_TEXTURE_WIDTH_POWER-1)));
  c.y = tex2D<float>(d_tex_obj, ((2*offset+1) & LOAD_TEXTURE_WIDTH_MASK), ((2*offset+1) >> LOAD_TEXTURE_WIDTH_POWER));
  return c;
}

// For total-power-only mode (Npolout == 1), the store_callback just needs to
// accumulate the power into the one and only power buffer.  It doesn't matter
// if it's for pol0 or pol1 since they all get added together eventually.
__device__ void store_callback(void *p_v_out,
                               size_t offset,
                               cufftComplex element,
                               void *p_v_user,
                               void *p_v_shared)
{
  float pwr = element.x * element.x + element.y * element.y;
  ((float *)p_v_user)[offset] += pwr;
}

// For full-Stokes mode, the store_callback_pol0_iquv function stores the
// voltage data into the first half of the 2x-sized FFT output buffer and
// accummulates (i.e. adds) the pol0 power into the first two quarters (I and
// Q) of the 4x-sized power buffer.
__device__ void store_callback_pol0_iquv(void *p_v_out,
                                    size_t offset,
                                    cufftComplex p0,
                                    void *p_v_user,
                                    void *p_v_shared)
{
  store_cb_data_t * d_scb_data = (store_cb_data_t *)p_v_user;
  float pwr = p0.x * p0.x + p0.y * p0.y;
  d_scb_data->pwr_buf_p00_i[offset] += pwr;
  d_scb_data->pwr_buf_p11_q[offset] += pwr;
  d_scb_data->fft_out_pol0[offset] = p0;
}

// For full-Stokes mode, the store_callback_pol1_iquv function accumulates
// (i.e. adds) the pol1 power into the first quarter of the 4x-sized power
// buffer (I), negatively accumulates (i.e. subtracts) the pol1 power into the
// second quarter of the 4x-sized power buffer (Q), reads the corresponding
// pol0 voltage from the first half of the 2x-sized FFT output buffer, and
// accumulates the complex pol0-pol1 power in the third (U) and fourth (V)
// quarters of the 4x-sized power buffer.
__device__ void store_callback_pol1_iquv(void *p_v_out,
                                    size_t offset,
                                    cufftComplex p1,
                                    void *p_v_user,
                                    void *p_v_shared)
{
  store_cb_data_t * d_scb_data = (store_cb_data_t *)p_v_user;
  float pwr = p1.x * p1.x + p1.y * p1.y;
  d_scb_data->pwr_buf_p00_i[offset] += pwr;
  d_scb_data->pwr_buf_p11_q[offset] -= pwr;
  cufftComplex p0 = d_scb_data->fft_out_pol0[offset];
  // TODO Verify sign and factor-of-two scaling for U and V
  d_scb_data->pwr_buf_p01_re_u[offset] += p0.x * p1.x + p0.y * p1.y;
  d_scb_data->pwr_buf_p01_im_v[offset] += p0.y * p1.x - p0.x * p1.y;
}

// Conjugated form of store_callback_pol1_iquv().
__device__ void store_callback_pol1_iquv_conj(void *p_v_out,
                                    size_t offset,
                                    cufftComplex p1,
                                    void *p_v_user,
                                    void *p_v_shared)
{
  store_cb_data_t * d_scb_data = (store_cb_data_t *)p_v_user;
  float pwr = p1.x * p1.x + p1.y * p1.y;
  d_scb_data->pwr_buf_p00_i[offset] += pwr;
  d_scb_data->pwr_buf_p11_q[offset] -= pwr;
  cufftComplex p0 = d_scb_data->fft_out_pol0[offset];
  // TODO Verify sign and factor-of-two scaling for U and V
  d_scb_data->pwr_buf_p01_re_u[offset] += p0.x * p1.x + p0.y * p1.y;
  d_scb_data->pwr_buf_p01_im_v[offset] -= p0.y * p1.x - p0.x * p1.y;
}

// For full-pol mode, the store_callback_pol0 function stores the voltage data
// into the first half of the 2x-sized FFT output buffer and accummulates the
// pol0 power into the first quarter of the 4x-sized power buffer.
__device__ void store_callback_pol0(void *p_v_out,
                                    size_t offset,
                                    cufftComplex p0,
                                    void *p_v_user,
                                    void *p_v_shared)
{
  store_cb_data_t * d_scb_data = (store_cb_data_t *)p_v_user;
  float pwr = p0.x * p0.x + p0.y * p0.y;
  d_scb_data->pwr_buf_p00_i[offset] += pwr;
  d_scb_data->fft_out_pol0[offset] = p0;
}

// For full-pol mode, the store_callback_pol1 function accumulates the pol1
// power into the second quarter of the 4x-sized power buffer, reads the
// corresponding pol0 voltage from the first half of the 2x-sized FFT output
// buffer, accumulates the complex pol0-pol1 power in the third (real) and
// fourth (imaginary) quarters of the 4x-sized power buffer.
__device__ void store_callback_pol1(void *p_v_out,
                                    size_t offset,
                                    cufftComplex p1,
                                    void *p_v_user,
                                    void *p_v_shared)
{
  store_cb_data_t * d_scb_data = (store_cb_data_t *)p_v_user;
  float pwr = p1.x * p1.x + p1.y * p1.y;
  d_scb_data->pwr_buf_p11_q[offset] += pwr;
  cufftComplex p0 = d_scb_data->fft_out_pol0[offset];
  d_scb_data->pwr_buf_p01_re_u[offset] += p0.x * p1.x + p0.y * p1.y;
  d_scb_data->pwr_buf_p01_im_v[offset] += p0.y * p1.x - p0.x * p1.y;
}

// conjugated form of store_callback_pol1().
__device__ void store_callback_pol1_conj(void *p_v_out,
                                    size_t offset,
                                    cufftComplex p1,
                                    void *p_v_user,
                                    void *p_v_shared)
{
  store_cb_data_t * d_scb_data = (store_cb_data_t *)p_v_user;
  float pwr = p1.x * p1.x + p1.y * p1.y;
  d_scb_data->pwr_buf_p11_q[offset] += pwr;
  cufftComplex p0 = d_scb_data->fft_out_pol0[offset];
  d_scb_data->pwr_buf_p01_re_u[offset] += p0.x * p1.x + p0.y * p1.y;
  d_scb_data->pwr_buf_p01_im_v[offset] -= p0.y * p1.x - p0.x * p1.y;
}

__device__ cufftCallbackLoadC d_cufft_load_callback = load_callback;
__device__ cufftCallbackStoreC d_cufft_store_callback = store_callback;
__device__ cufftCallbackStoreC d_cufft_store_callback_pol0 = store_callback_pol0;
__device__ cufftCallbackStoreC d_cufft_store_callback_pol1 = store_callback_pol1;
__device__ cufftCallbackStoreC d_cufft_store_callback_pol1_conj = store_callback_pol1_conj;
__device__ cufftCallbackStoreC d_cufft_store_callback_pol0_iquv = store_callback_pol0_iquv;
__device__ cufftCallbackStoreC d_cufft_store_callback_pol1_iquv = store_callback_pol1_iquv;
__device__ cufftCallbackStoreC d_cufft_store_callback_pol1_iquv_conj = store_callback_pol1_iquv_conj;

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

// Incoherent summation kernel (across antenna)
__global__ void incoherent_sum(float * pwr_buf, float * incoh_buf, float * ant_weights, unsigned int Nant, size_t Nt,
                                size_t ant_pitch, size_t chan_pitch, size_t pol_pitch, size_t spectra_pitch,
                                size_t chan_out_pitch, size_t pol_out_pitch, size_t spectra_out_pitch
                              )
{
  const size_t coarse_chan_idx = (blockIdx.x* blockDim.x + threadIdx.x)/Nt;
  const size_t fine_chan_idx = (blockIdx.x* blockDim.x + threadIdx.x)%Nt;

  off_t offset_pwr =  blockIdx.z * spectra_pitch
                    + blockIdx.y * pol_pitch
                    + coarse_chan_idx * chan_pitch + fine_chan_idx;
  const off_t offset_ics =  blockIdx.z * spectra_out_pitch
                          + blockIdx.y * pol_out_pitch
                          + coarse_chan_idx * chan_out_pitch + fine_chan_idx + (fine_chan_idx < (Nt+1)/2 ? Nt/2 : -Nt/2);

  for(unsigned int i=0; i<Nant; i++) {
    incoh_buf[offset_ics] += ant_weights[i] * pwr_buf[offset_pwr];
    offset_pwr += ant_pitch;
  }
}

__global__ void complex4_expansion(char2 *lut){
  // The right shifts (>> 4) aren't perfectly necessary, as
  // with out them a scaling factor is introduced. They are kept
  // however, as the LUT only computes this 256 times, all in parallel,
  // and so no real speed gains are to be had.
  lut[blockIdx.x] = make_char2( ((char)(blockIdx.x&0xf0))>>4,       // Real component
                                ((char)((blockIdx.x&0x0f)<<4)) >> 4 // Imag component
                              );
}

// 4bit Expansion kernel
// Takes the half full blocks of the gpu_ctx->d_blk_expansion_buf buffer and expands 
// each complex4 byte. The transferal mimics the cudaMemcpy2D of 
// rawspec_copy_blocks_to_gpu:
// The src order is [time, channel, blocks]  (fastest --> slowest)
// The dst order is [time, blocks, channels] (fastest --> slowest)
//
// Expectation of blockDim, with ctx->Np threads each:
// grid.x = ctx->Ntpb;
// grid.y = ctx->Nc;
// grid.z = num_blocks;
__global__ void copy_expand_complex4(char *comp8_dst, char *comp4_src, size_t num_blocks,
                                     size_t block_pitch, size_t channel_pitch)
{                                     
  char* comp8_dst_offset = comp8_dst + 2*(blockIdx.y*num_blocks*channel_pitch +
                                          blockIdx.z*channel_pitch +
                                          blockIdx.x*blockDim.x + threadIdx.x);
  const char2 comp8 = tex1Dfetch<char2>(d_comp4_exp_tex_obj, (unsigned char) (comp4_src[blockIdx.z*block_pitch + 
                                                                      blockIdx.y*channel_pitch +
                                                                      blockIdx.x*blockDim.x + threadIdx.x]));
  comp8_dst_offset[0] = comp8.x;
  comp8_dst_offset[1] = comp8.y;
}

// Stream callback function that is called right before an output product's GPU
// power buffer has been copied to the host power buffer.
static void CUDART_CB pre_dump_stream_callback(cudaStream_t stream,
                                               cudaError_t status,
                                               void *data)
{
  dump_cb_data_t * dump_cb_data = (dump_cb_data_t *)data;
  if(dump_cb_data->ctx->dump_callback) {
    dump_cb_data->ctx->dump_callback(dump_cb_data->ctx,
                                     dump_cb_data->output_product,
                                     RAWSPEC_CALLBACK_PRE_DUMP);
  }
}

// Stream callback function that is called right after an output product's GPU
// power buffer has been copied to the host power buffer.
static void CUDART_CB post_dump_stream_callback(cudaStream_t stream,
                                                cudaError_t status,
                                                void *data)
{
  dump_cb_data_t * dump_cb_data = (dump_cb_data_t *)data;
  if(dump_cb_data->ctx->dump_callback) {
    dump_cb_data->ctx->dump_callback(dump_cb_data->ctx,
                                     dump_cb_data->output_product,
                                     RAWSPEC_CALLBACK_POST_DUMP);
  }
}

// This stringification trick is from "info cpp"
#define STRINGIFY1(s) #s
#define STRINGIFY(s) STRINGIFY1(s)
static const char rawspec_version[] = STRINGIFY(RAWSPEC_VERSION) " cuFFT"
#ifdef CUFFT_VER_MAJOR
  " " STRINGIFY(CUFFT_VER_MAJOR)
#ifdef CUFFT_VER_MINOR
  "." STRINGIFY(CUFFT_VER_MINOR)
#ifdef CUFFT_VER_PATCH
  "." STRINGIFY(CUFFT_VER_PATCH)
#ifdef CUFFT_VER_BUILD
  "." STRINGIFY(CUFFT_VER_BUILD)
#endif // CUFFT_VER_BUILD
#endif // CUFFT_VER_PATCH
#endif // CUFFT_VER_MINOR
#else
  " unknown/old"
#endif // CUFFT_VER_MAJOR
;

// Returns a pointer to a string containing the rawspec version
const char * rawspec_version_string()
{
  return rawspec_version;
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
  int p;
  // A simple bool-flag for now, but could rather hold expansion ratio
  // ^^ would require appropriate changes in rawspec.c (see expand4bps_to8bps)
  char NbpsIsExpanded = 0;
  uint64_t buf_size;
  size_t work_size = 0;
  store_cb_data_t h_scb_data;
  cudaError_t cuda_rc;
  cufftResult cufft_rc;
  cudaResourceDesc res_desc;
  cudaTextureDesc tex_desc;
  int texture_attribute_maximum;


  // Host copies of cufft callback pointers
  cufftCallbackLoadC h_cufft_load_callback;
  cufftCallbackStoreC h_cufft_store_callback;
  cufftCallbackStoreC h_cufft_store_callback_pols[2];
  cufftCallbackStoreC h_cufft_store_callback_iquv[2];

  // Validate No
  if(ctx->No == 0 || ctx->No > MAX_OUTPUTS) {
    fprintf(stderr, "output products must be in range [1..%d], not %d\n",
        MAX_OUTPUTS, ctx->No);
    fflush(stderr);
    return 1;
  }

  // Validate Np
  if(ctx->Np == 0 || ctx->Np > 2) {
    fprintf(stderr,
        "number of polarizations must be in range [1..2], not %d\n", ctx->Np);
    fflush(stderr);
    return 1;
  }

  // Validate/set Npolout values
  for(i=0; i<ctx->No; i++) {
    if(abs(ctx->Npolout[i]) != 4 || ctx->Np != 2) {
      ctx->Npolout[i] = 1;
    }
  }

  // Validate Ntpb
  if(ctx->Ntpb == 0) {
    fprintf(stderr, "number of time samples per block cannot be zero\n");
    fflush(stderr);
    return 1;
  }

  // Validate Nbps. Zero silently defaults to 8 for backwards compatibility
  // with pre-Nbps versions.  Any other value except 8 or 16 is treated as 8
  // and a warning is issued to stderr.
  if(ctx->Nbps == 0) {
    ctx->Nbps = 8;
  } else if(ctx->Nbps != 8 && ctx->Nbps != 16) {
    fprintf(stderr,
        "number of bits per sample must be 8 or 16 (not %d), using 8 bps\n",
        ctx->Nbps);
    fflush(stderr);
    NbpsIsExpanded = ctx->Nbps == 4;
    ctx->Nbps = 8;
  }

  // Determine Ntmax (and validate Nts)
  ctx->Ntmax = 0;
  for(i=0; i<ctx->No; i++) {
    if(ctx->Nts[i] == 0) {
      fprintf(stderr, "Nts[%d] cannot be 0\n", i);
      fflush(stderr);
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
      fflush(stderr);
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
      fflush(stderr);
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
        fflush(stderr);
        return 1;
      }
      ctx->Nb = 1;
    } else {
      // Validate that Ntpb is factor of Ntmax
      if(ctx->Ntmax % ctx->Ntpb != 0) {
        fprintf(stderr, "Ntpb (%u) is not a factor of Nmax (%u)\n",
            ctx->Ntpb, ctx->Ntmax);
        fflush(stderr);
        return 1;
      }
      ctx->Nb = ctx->Ntmax / ctx->Ntpb;
    }
  }

  // Ensure Nb_host is non-zero when host input buffers are caller managed
  if(ctx->Nb_host == 0 && ctx->h_blkbufs) {
    fprintf(stderr,
        "Must specify number of host input blocks when caller-managed\n");
    fflush(stderr);
    return 1;
  } else if(ctx->Nb_host == 0) {
    ctx->Nb_host = ctx->Nb;
  }

  // Validate Nas
  for(i=0; i < ctx->No; i++) {
    if(ctx->Nas[i] == 0) {
      fprintf(stderr, "Nas[%d] cannot be 0\n", i);
      fflush(stderr);
      return 1;
    }
    // If mulitple integrations per input buffer
    if(ctx->Nts[i]*ctx->Nas[i] < ctx->Nb*ctx->Ntpb) {
      // Must have integer integrations per input buffer
      if((ctx->Nb * ctx->Ntpb) % (ctx->Nts[i] * ctx->Nas[i]) != 0) {
        fprintf(stderr,
            "Nts[%d] * Nas[%d] (%u * %u) must divide Nb * Ntpb (%u * %u)\n",
            i, i, ctx->Nts[i], ctx->Nas[i], ctx->Nb, ctx->Ntpb);
        fflush(stderr);
        return 1;
      }
    } else {
      // Must have integer input buffers per integration
      if((ctx->Nts[i] * ctx->Nas[i]) % (ctx->Nb * ctx->Ntpb) != 0) {
        fprintf(stderr,
            "Nb * Ntpb (%u * %u) must divide Nts[%d] * Nas[%d] (%u * %u)\n",
            ctx->Nb, ctx->Ntpb, i, i, ctx->Nts[i], ctx->Nas[i]);
        fflush(stderr);
        return 1;
      }
    }
  }

  ctx->Nant = ctx->Nant <= 0 ? 1 : ctx->Nant;
  // Setup channel-chunk parametners
  if(ctx->Ncc == 0) { // Disable channel-chunking
    ctx->Ncc = ctx->Nc;
  }
  else{ // Enabled channel-chunking
    if(ctx->Ncc <= 1) { // Auto channel-chunking
      if(ctx->Nant > 1){
        ctx->Ncc = ctx->Nc/ctx->Nant;
      }
      else{ // find largest Nc factor <= 10
        for(i = 1; i <= 10; i++){
          if(ctx->Nc%i == 0){
            ctx->Ncc = ctx->Nc/i;
          }
        }
      }
    }
    else if(ctx->Nc%ctx->Ncc != 0) { // Manual channel-chunking, but inappropriate chunks
      fprintf(stderr, "%d channels cannot be factorised to chunks of %d\n",
        ctx->Nc, ctx->Ncc
      );
      return 1;
    }

    printf("Chunking %d channels into %d chunks of %d.\n", ctx->Nc, ctx->Nc/ctx->Ncc, ctx->Ncc);
  }

  // Null out all pointers
  // TODO Add support for client managed host buffers
  for(i=0; i < MAX_OUTPUTS; i++) {
    ctx->h_pwrbuf[i] = NULL;
    ctx->h_icsbuf[i] = NULL;
  }
  ctx->gpu_ctx = NULL;

  // Set CUDA device (validates gpu_index)
  cuda_rc = cudaSetDevice(ctx->gpu_index);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    // TODO return distinct error code
    return 1;
  }

  // Allocate GPU context
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)malloc(sizeof(rawspec_gpu_context));

  if(!gpu_ctx) {
    fprintf(stderr, "unable to allocate %lu bytes for rawspec GPU context\n",
        sizeof(rawspec_gpu_context));
    fflush(stderr);
    rawspec_cleanup(ctx);
    return 1;
  }

  // Store pointer to gpu_ctx in ctx
  ctx->gpu_ctx = gpu_ctx;

  // NULL out pointers (and invalidate plans)
  gpu_ctx->d_fft_in = NULL;
  gpu_ctx->d_comp4_exp_LUT = NULL;
  gpu_ctx->d_blk_expansion_buf = NULL;
  gpu_ctx->d_fft_out = NULL;
  gpu_ctx->d_work_area = NULL;
  gpu_ctx->work_size = 0;
  gpu_ctx->compute_stream = NO_STREAM;
  for(i=0; i<MAX_OUTPUTS; i++) {
    gpu_ctx->d_pwr_out[i] = NULL;
    gpu_ctx->d_scb_data[i] = NULL;
    gpu_ctx->plan[i][0] = NO_PLAN;
    gpu_ctx->plan[i][1] = NO_PLAN;
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
    ctx->h_blkbufs = (char **)malloc(ctx->Nb_host * sizeof(char *));
    for(i=0; i < ctx->Nb_host; i++) {
      // Block buffer can use write combining
      cuda_rc = cudaHostAlloc(&ctx->h_blkbufs[i],
          ctx->Ntpb * ctx->Np * ctx->Nc * 2 /*complex*/ * (ctx->Nbps/8),
          cudaHostAllocWriteCombined);
      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        rawspec_cleanup(ctx);
        return 1;
      }
    }
  } else {
    // Remember that the caller is managing these buffers
    // (i.e. we will only need to unregister them when cleaning up).
    gpu_ctx->caller_managed = 1;

    // Register these buffers with CUDA.  It is the caller's responsibility to
    // ensure that the blocks meet memory alignment requirements, etc.
    for(i=0; i < ctx->Nb_host; i++) {
      cuda_rc = cudaHostRegister(ctx->h_blkbufs[i],
          ctx->Ntpb * ctx->Np * ctx->Nc * 2 /*complex*/ * (ctx->Nbps/8),
          cudaHostRegisterDefault);
      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        rawspec_cleanup(ctx);
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
    ctx->Nds[i] = gpu_ctx->Nss[i] / ctx->Nas[i];
    if(ctx->Nds[i] == 0) {
      ctx->Nds[i] = 1;
    }

    // Calculate number of input buffers per dump
    gpu_ctx->Nis[i] = ctx->Nas[i] / gpu_ctx->Nss[i];
    if(gpu_ctx->Nis[i] == 0) {
      gpu_ctx->Nis[i] = 1;
    }

    // Calculate grid dimensions
    gpu_ctx->grid[i].x = (ctx->Nts[i] + MAX_THREADS - 1) / MAX_THREADS;
    gpu_ctx->grid[i].y = ctx->Nds[i];
    gpu_ctx->grid[i].z = ctx->Ncc;

    // Calculate number of threads per block
    gpu_ctx->nthreads[i] = MIN(ctx->Nts[i], MAX_THREADS);

    // Host buffer needs to accommodate the number of integrations that will be
    // dumped at one time (Nd).
    ctx->h_pwrbuf_size[i] = abs(ctx->Npolout[i]) *
                            ctx->Nds[i]*ctx->Nts[i]*ctx->Nc*sizeof(float);
    #ifdef VERBOSE_ALLOC
      printf("FFT Host dump buffer[%d] size == %lu\n", i, ctx->h_pwrbuf_size[i]);
    #endif
    cuda_rc = cudaHostAlloc(&ctx->h_pwrbuf[i], ctx->h_pwrbuf_size[i],
                       cudaHostAllocDefault);

    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
    if(ctx->incoherently_sum == 1){// TODO validate that Nant > 1
      cuda_rc = cudaHostAlloc(&ctx->h_icsbuf[i], ctx->h_pwrbuf_size[i]/ctx->Nant,
                        cudaHostAllocDefault);

      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        rawspec_cleanup(ctx);
        return 1;
      }
    }
  }

  gpu_ctx->guppi_channel_stride = (ctx->Ntpb * ctx->Np * 2 /*complex*/ * ctx->Nbps)/8;

  // Allocate buffers

  // FFT input buffer
  // The input buffer is padded to the next multiple of 1<<LOAD_TEXTURE_WIDTH_POWER
  // to facilitate 2D texture lookups by treating the input buffer as a 2D array
  // that is 1<<LOAD_TEXTURE_WIDTH_POWER wide.
  buf_size = ctx->Nb*ctx->Nc*gpu_ctx->guppi_channel_stride;
  if((buf_size & LOAD_TEXTURE_WIDTH_MASK) != 0) {
    // Round up to next multiple of 64KB
    buf_size = (buf_size & ~LOAD_TEXTURE_WIDTH_MASK) + 1<<LOAD_TEXTURE_WIDTH_POWER;
  }

#ifdef VERBOSE_ALLOC
  printf("FFT input buffer size == %lu\n", buf_size);
#endif
  cuda_rc = cudaMalloc(&gpu_ctx->d_fft_in, buf_size);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  
  cudaDeviceGetAttribute(&texture_attribute_maximum, cudaDevAttrMaxTexture2DLinearWidth, ctx->gpu_index);
  if(texture_attribute_maximum < 1<<LOAD_TEXTURE_WIDTH_POWER){
    fprintf(stderr, "Maximum 2D texture width: %d.\n", texture_attribute_maximum);
    fprintf(stderr, "\tThe static load-texture-width of 1<<LOAD_TEXTURE_WIDTH_POWER exceeds this: %d\n", 1<<LOAD_TEXTURE_WIDTH_POWER);
    fprintf(stderr, "\tExpect a CUDA raised failure!\n");
  }
  cudaDeviceGetAttribute(&texture_attribute_maximum, cudaDevAttrMaxTexture2DLinearHeight, ctx->gpu_index);
  if(texture_attribute_maximum < buf_size>>LOAD_TEXTURE_WIDTH_POWER){
    fprintf(stderr, "Maximum 2D texture height: %d.\n", texture_attribute_maximum);
    fprintf(stderr, "\tThe load-texture-height of `buf_size (%lu)>>(%d) LOAD_TEXTURE_WIDTH_POWER` exceeds this: %lu\n", buf_size, LOAD_TEXTURE_WIDTH_POWER, buf_size>>LOAD_TEXTURE_WIDTH_POWER);
    fprintf(stderr, "\tExpect a CUDA raised failure!\n");

    cudaDeviceGetAttribute(&texture_attribute_maximum, cudaDevAttrMaxTexture2DLinearWidth, ctx->gpu_index);
    if(texture_attribute_maximum > 1<<LOAD_TEXTURE_WIDTH_POWER){
      fprintf(stderr, "\tLOAD_TEXTURE_WIDTH_POWER could be increased to %d (at most) to possibly circumvent this issued\n", 31 - __builtin_clz(texture_attribute_maximum));
    }
  }
  cudaDeviceGetAttribute(&texture_attribute_maximum, cudaDevAttrMaxTexture2DLinearPitch, ctx->gpu_index);
  if(texture_attribute_maximum < (1<<LOAD_TEXTURE_WIDTH_POWER) * (ctx->Nbps/8)){
    fprintf(stderr, "Maximum 2D texture pitch: %d.\n", texture_attribute_maximum);
    fprintf(stderr, "\tThe load-texture-pitch of (1<<LOAD_TEXTURE_WIDTH_POWER) * (ctx->Nbps/8) exceeds this: %d\n", (1<<LOAD_TEXTURE_WIDTH_POWER) * (ctx->Nbps/8));
    fprintf(stderr, "\tExpect a CUDA raised failure!\n");
  }
  fflush(stderr);

  // Create texture object for device input buffer
  // res_desc describes input resource
  // Width is 32K elements, height is buf_size/32K elements, pitch is 32K elements
  memset(&res_desc, 0, sizeof(res_desc));
  res_desc.resType = cudaResourceTypePitch2D;
  res_desc.res.pitch2D.devPtr = gpu_ctx->d_fft_in;
  res_desc.res.pitch2D.desc.f = cudaChannelFormatKindSigned;
  res_desc.res.pitch2D.desc.x = ctx->Nbps; // bits per sample
  res_desc.res.pitch2D.width = 1<<LOAD_TEXTURE_WIDTH_POWER;         // elements
  res_desc.res.pitch2D.height = buf_size>>LOAD_TEXTURE_WIDTH_POWER; // elements
  res_desc.res.pitch2D.pitchInBytes = (1<<LOAD_TEXTURE_WIDTH_POWER) * (ctx->Nbps/8);  // bytes!
  // tex_desc describes texture mapping
  memset(&tex_desc, 0, sizeof(tex_desc));
#if 0 // These settings are not used in online examples involved cudaReadModeNormalizedFloat
  // Not sure whether address_mode matters for cudaReadModeNormalizedFloat
  tex_desc.address_mode[0] = cudaAddressModeClamp;
  tex_desc.address_mode[1] = cudaAddressModeClamp;
  tex_desc.address_mode[2] = cudaAddressModeClamp;
  // Not sure whether filter_mode matters for cudaReadModeNormalizedFloat
  tex_desc.filter_mode = cudaFilterModePoint;
#endif // 0
  tex_desc.readMode = cudaReadModeNormalizedFloat;

  cuda_rc = cudaCreateTextureObject(&gpu_ctx->tex_obj,
                                    &res_desc, &tex_desc, NULL);

  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  // Copy texture object to device
  cuda_rc = cudaMemcpyToSymbol(d_tex_obj,
                               &gpu_ctx->tex_obj,
                               sizeof(cudaTextureObject_t));

  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  if(NbpsIsExpanded){
#ifdef VERBOSE_ALLOC
    printf("NBITS expansion buffer size == %lu\n", buf_size/2);
#endif
    cuda_rc = cudaMalloc(&gpu_ctx->d_blk_expansion_buf, buf_size/2);
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }

#ifdef VERBOSE_ALLOC
    printf("Complex4 expansion LUT size == %lu\n", 256*sizeof(char2));
#endif
    cuda_rc = cudaMalloc(&gpu_ctx->d_comp4_exp_LUT, 256*sizeof(char2));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
    complex4_expansion<<<256,1>>>(gpu_ctx->d_comp4_exp_LUT);

    memset(&res_desc, 0, sizeof(res_desc));
    res_desc.resType = cudaResourceTypeLinear;
    res_desc.res.linear.devPtr = gpu_ctx->d_comp4_exp_LUT;
    res_desc.res.linear.desc.f = cudaChannelFormatKindSigned;
    res_desc.res.linear.desc.x = 8; // bits per channel
    res_desc.res.linear.desc.y = 8; // bits per channel
    res_desc.res.linear.sizeInBytes = 256*sizeof(char2);

    memset(&tex_desc, 0, sizeof(tex_desc));
    tex_desc.readMode = cudaReadModeElementType;
  
    cuda_rc = cudaCreateTextureObject(&gpu_ctx->comp4_exp_tex_obj,
                                      &res_desc, &tex_desc, NULL);
  
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
  
    cuda_rc = cudaMemcpyToSymbol(d_comp4_exp_tex_obj,
                                &gpu_ctx->comp4_exp_tex_obj,
                                sizeof(cudaTextureObject_t));

    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
  }

  // FFT output buffer
  buf_size = ctx->Nb*ctx->Ntpb*ctx->Ncc*sizeof(cufftComplex);
  // If any output product is full-pol then we need to double output buffer
  for(i=0; i < ctx->No; i++) {
    if(abs(ctx->Npolout[i]) == 4) {
      buf_size *= 2;
      break;
    }
  }
#ifdef VERBOSE_ALLOC
  printf("FFT output buffer size == %lu\n", buf_size);
#endif
  cuda_rc = cudaMalloc(&gpu_ctx->d_fft_out, buf_size);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  // For each output product
  for(i=0; i < ctx->No; i++) {
    // Power output buffer
#ifdef VERBOSE_ALLOC
    printf("Power output buffer size == %u * %lu == %lu\n",
        abs(ctx->Npolout[i]),  ctx->Nb*ctx->Ntpb*ctx->Ncc*sizeof(float),
        abs(ctx->Npolout[i]) * ctx->Nb*ctx->Ntpb*ctx->Ncc*sizeof(float));
#endif
    cuda_rc = cudaMalloc(&gpu_ctx->d_pwr_out[i],
        abs(ctx->Npolout[i]) * ctx->Nb*ctx->Ntpb*ctx->Ncc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }
    // Clear power output buffer
    cuda_rc = cudaMemset(gpu_ctx->d_pwr_out[i], 0,
        abs(ctx->Npolout[i]) * ctx->Nb*ctx->Ntpb*ctx->Ncc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }

    if(ctx->incoherently_sum){
#ifdef VERBOSE_ALLOC
      printf("ICS output buffer size == %u * %lu / %u == %lu\n",
          abs(ctx->Npolout[i]),  ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float), ctx->Nant,
          abs(ctx->Npolout[i]) * ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float)/ctx->Nant);
#endif
      cuda_rc = cudaMalloc(&gpu_ctx->d_ics_out[i],
          abs(ctx->Npolout[i]) * ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float)/ctx->Nant);
      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        rawspec_cleanup(ctx);
        return 1;
      }
      // Clear incoherent-sum output buffer
      cuda_rc = cudaMemset(gpu_ctx->d_ics_out[i], 0,
          abs(ctx->Npolout[i]) * ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float)/ctx->Nant);
      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        rawspec_cleanup(ctx);
        return 1;
      }

      // Setup device antenna-weight buffer
#ifdef VERBOSE_ALLOC
      printf("ICS antenna-weight buffer size == %lu\n", ctx->Nant*sizeof(float));
#endif
      cuda_rc = cudaMalloc(&gpu_ctx->d_Aws, ctx->Nant*sizeof(float));
      if(cuda_rc != cudaSuccess) {
        PRINT_ERRMSG(cuda_rc);
        rawspec_cleanup(ctx);
        return 1;
      }

      if(ctx->Naws == 1 && ctx->Naws < ctx->Nant){
        printf("Using the single antenna-weight (%f) for all antennas in the incoherent-sum.\n", ctx->Aws[0]);
        for(int w = 0; w < ctx->Nant; w++){
          cudaMemcpy(gpu_ctx->d_Aws+w, ctx->Aws, sizeof(float), cudaMemcpyHostToDevice);
        }
      }
      else if(ctx->Naws == ctx->Nant){
        for(int w = 0; w < ctx->Nant; w++){
          cudaMemcpy(gpu_ctx->d_Aws+w, ctx->Aws + w, sizeof(float), cudaMemcpyHostToDevice);
        }
      }
      else{
        fprintf(stderr, "Not enough antenna-weights provided for the %d antennas: only provided %d.\n", ctx->Nant, ctx->Naws);
        rawspec_cleanup(ctx);
        return 1;
      }
    }
    // Save pointer to FFT output buffer in store_cb_data
    h_scb_data.fft_out_pol0 = gpu_ctx->d_fft_out;
    // Save pointers into power ouput buffer
    h_scb_data.pwr_buf_p00_i = gpu_ctx->d_pwr_out[i];
    // These next fields are only used if abs(Npolout) == 4,
    // so we can initialize them that way even if Npolout == 1
    // (because they will never be used). It might be slightly
    // safer to init them to the same as pwr_buf_p00_i if Npolout == 1.
    h_scb_data.pwr_buf_p11_q =
        gpu_ctx->d_pwr_out[i] + 1*ctx->Nb*ctx->Ntpb*ctx->Ncc;
    h_scb_data.pwr_buf_p01_re_u =
        gpu_ctx->d_pwr_out[i] + 2*ctx->Nb*ctx->Ntpb*ctx->Ncc;
    h_scb_data.pwr_buf_p01_im_v =
        gpu_ctx->d_pwr_out[i] + 3*ctx->Nb*ctx->Ntpb*ctx->Ncc;

    // Allocate device memory for store_cb_data_t array
    cuda_rc = cudaMalloc(&gpu_ctx->d_scb_data[i], sizeof(store_cb_data_t));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      rawspec_cleanup(ctx);
      return 1;
    }

    // Copy store_cb_data_t arary from host to device
    cuda_rc = cudaMemcpy(gpu_ctx->d_scb_data[i],
                         &h_scb_data,
                         sizeof(store_cb_data_t),
                         cudaMemcpyHostToDevice);
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

  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_store_callback_pols[0],
                                 d_cufft_store_callback_pol0,
                                 sizeof(h_cufft_store_callback_pols[0]));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_store_callback_pols[1],
                                 ctx->input_conjugated ? d_cufft_store_callback_pol1_conj
                                                       : d_cufft_store_callback_pol1,
                                 sizeof(h_cufft_store_callback_pols[1]));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_store_callback_iquv[0],
                                 d_cufft_store_callback_pol0_iquv,
                                 sizeof(h_cufft_store_callback_iquv[0]));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  cuda_rc = cudaMemcpyFromSymbol(&h_cufft_store_callback_iquv[1],
                                 ctx->input_conjugated ? d_cufft_store_callback_pol1_iquv_conj
                                                       : d_cufft_store_callback_pol1_iquv,
                                 sizeof(h_cufft_store_callback_iquv[1]));
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  // Create the "compute stream"
  cuda_rc = cudaStreamCreateWithFlags(&gpu_ctx->compute_stream,
                                      cudaStreamNonBlocking);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }

  // Generate FFT plans and associate callbacks and stream
  for(i=0; i < ctx->No; i++) {
    for(p=0; p<2; p++) {
      // Create plan handle (does not "make the plan", that happens later)
      cufft_rc = cufftCreate(&gpu_ctx->plan[i][p]);
      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }

      // Prevent auto-allocation of work area for plan
      cufft_rc = cufftSetAutoAllocation(gpu_ctx->plan[i][p], 0);
      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }

#ifdef VERBOSE_ALLOC
      printf("cufftMakePlanMany for output product %d...", i);
#endif
      // Make the plan
      // TODO Are sizes here in units of elements or bytes?  Assume elements for now...
      cufft_rc = cufftMakePlanMany(
                      gpu_ctx->plan[i][p],     // plan handle
                      1,                       // rank
                      (int *)&ctx->Nts[i],     // *n
                      (int *)&ctx->Nts[i],     // *inembed (unused for 1d)
                      ctx->Np,                 // istride
                      ctx->Nts[i]*ctx->Np,     // idist
                      (int *)&ctx->Nts[i],     // *onembed (unused for 1d)
                      1,                       // ostride
                      ctx->Nts[i],             // odist
                      CUFFT_C2C,               // type
                      gpu_ctx->Nss[i]*ctx->Ncc,// batch
                      &work_size               // work area size
                 );

      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }
#ifdef VERBOSE_ALLOC
      printf("ok\n");
#endif

      // Now associate the callbacks with the plan.
      // Load callback
      cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i][p],
                                    (void **)&h_cufft_load_callback,
                                    CUFFT_CB_LD_COMPLEX,
                                    (void **)&gpu_ctx->d_fft_in);
      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }
      // Store callback(s)
      if(ctx->Npolout[i] == 1) {
        cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i][p],
                                      (void **)&h_cufft_store_callback,
                                      CUFFT_CB_ST_COMPLEX,
                                      (void **)&gpu_ctx->d_pwr_out[i]);
      } else if(ctx->Npolout[i] == 4) {
        cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i][p],
                                      (void **)&h_cufft_store_callback_pols[p],
                                      CUFFT_CB_ST_COMPLEX,
                                      (void **)&gpu_ctx->d_scb_data[i]);
      } else if(ctx->Npolout[i] == -4) {
        cufft_rc = cufftXtSetCallback(gpu_ctx->plan[i][p],
                                      (void **)&h_cufft_store_callback_iquv[p],
                                      CUFFT_CB_ST_COMPLEX,
                                      (void **)&gpu_ctx->d_scb_data[i]);
      } else {
        fprintf(stderr, "invalid Npolout[%d]: %d\n", i, ctx->Npolout[i]);
        fflush(stderr);
        return 1;
      }
      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }

      // Associate compute stream with plan
      cufft_rc = cufftSetStream(gpu_ctx->plan[i][p], gpu_ctx->compute_stream);
      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }

      // Get work size for this plan
      cufft_rc = cufftGetSize(gpu_ctx->plan[i][p], &work_size);
      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }

      // Save size if it's largest one so far
      if(gpu_ctx->work_size < work_size) {
        gpu_ctx->work_size = work_size;
      }
    }
  }

  // Allocate work area
#ifdef VERBOSE_ALLOC
  printf("allocating work area %lu bytes...", work_size);
#endif
  cuda_rc = cudaMalloc(&gpu_ctx->d_work_area, gpu_ctx->work_size);
  if(cuda_rc != cudaSuccess) {
    PRINT_ERRMSG(cuda_rc);
    rawspec_cleanup(ctx);
    return 1;
  }
#ifdef VERBOSE_ALLOC
  printf("ok\n");
#endif

  // Associate work area with plans
  for(i=0; i < ctx->No; i++) {
    for(p=0; p<2; p++) {
      cufft_rc = cufftSetWorkArea(gpu_ctx->plan[i][p], gpu_ctx->d_work_area);
      if(cufft_rc != CUFFT_SUCCESS) {
        PRINT_ERRMSG(cufft_rc);
        rawspec_cleanup(ctx);
        return 1;
      }
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
  int p;
  rawspec_gpu_context * gpu_ctx;

  for(i=0; i<MAX_OUTPUTS; i++) {
    if(ctx->h_pwrbuf[i]) {
      cudaFreeHost(ctx->h_pwrbuf[i]);
      ctx->h_pwrbuf[i] = NULL;
    }
    if(ctx->h_icsbuf[i]) {
      cudaFreeHost(ctx->h_icsbuf[i]);
      ctx->h_icsbuf[i] = NULL;
    }
  }

  if(ctx->gpu_ctx) {
    gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

    if(gpu_ctx->caller_managed) {
      for(i=0; i < ctx->Nb_host; i++) {
        cudaHostUnregister(ctx->h_blkbufs[i]);
      }
    } else {
      if(ctx->h_blkbufs) {
        for(i=0; i < ctx->Nb_host; i++) {
          cudaFreeHost(ctx->h_blkbufs[i]);
        }
        free(ctx->h_blkbufs);
        ctx->h_blkbufs = NULL;
      }
    }

    // Destroy texture object before freeing referenced memory
    cudaDestroyTextureObject(gpu_ctx->tex_obj);

    if(gpu_ctx->d_fft_in) {
      cudaFree(gpu_ctx->d_fft_in);
    }

    if(gpu_ctx->d_blk_expansion_buf) {
      cudaFree(gpu_ctx->d_blk_expansion_buf);
    }
    
    if(gpu_ctx->d_comp4_exp_LUT) {
      cudaFree(gpu_ctx->d_comp4_exp_LUT);
      cudaDestroyTextureObject(gpu_ctx->comp4_exp_tex_obj);
    }

    if(gpu_ctx->d_work_area) {
      cudaFree(gpu_ctx->d_work_area);
    }

    if(gpu_ctx->compute_stream != NO_STREAM) {
      cudaStreamDestroy(gpu_ctx->compute_stream);
    }

    if(gpu_ctx->d_fft_out) {
      cudaFree(gpu_ctx->d_fft_out);
    }

    for(i=0; i<MAX_OUTPUTS; i++) {
      if(gpu_ctx->d_pwr_out[i]) {
        cudaFree(gpu_ctx->d_pwr_out[i]);
      }
      if(gpu_ctx->d_ics_out[i]) {
        cudaFree(gpu_ctx->d_ics_out[i]);
      }
      for(p=0; p<2; p++) {
        if(gpu_ctx->plan[i][p] != NO_PLAN) {
          cufftDestroy(gpu_ctx->plan[i][p]);
        }
      }
    }

    if(ctx->incoherently_sum){
      if(ctx->Aws){
        cudaFreeHost(ctx->Aws);
      }
      if(gpu_ctx->d_Aws){
        cudaFree(gpu_ctx->d_Aws);
      }
    }

    free(ctx->gpu_ctx);
    ctx->gpu_ctx = NULL;
  }
}

// Copy `ctx->h_blkbufs` to GPU input buffer.
// Returns 0 on success, non-zero on error.
int rawspec_copy_blocks_to_gpu_expanding_complex4(rawspec_context * ctx,
  off_t src_idx, off_t dst_idx, size_t num_blocks)
{
  if(num_blocks > ctx->Nb){
    fprintf(stderr, "%s: num_blocks (%lu) > Nb (%u)\n", __FUNCTION__, num_blocks, ctx->Nb);
    return 1;
  }

  int b;
  off_t sblk;
  off_t dblk;
  dim3 grid;
  cudaError_t rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  // Calculated for complex4 samples
  const size_t block_size = (gpu_ctx->guppi_channel_stride * ctx->Nc)/2;

  for(b=0; b < num_blocks; b++) {
    sblk = (src_idx + b) % ctx->Nb_host;
    dblk = (dst_idx + b) % ctx->Nb;
    rc = cudaMemcpyAsync(gpu_ctx->d_blk_expansion_buf + (dblk * block_size), ctx->h_blkbufs[sblk],
                          block_size, cudaMemcpyHostToDevice, gpu_ctx->compute_stream);

    if(rc != cudaSuccess) {
      PRINT_ERRMSG(rc);
      return 1;
    }
  }
  
  // Calculate grid dimensions, fastest to slowest
  const unsigned int thread_count = ctx->Np;

  grid.x = ctx->Ntpb;
  grid.y = ctx->Nc;
  grid.z = num_blocks;
  
  copy_expand_complex4<<<grid, thread_count, 0, gpu_ctx->compute_stream>>>(
                                              gpu_ctx->d_fft_in, gpu_ctx->d_blk_expansion_buf, 
                                              num_blocks, block_size, gpu_ctx->guppi_channel_stride/2);

  return 0;
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

  for(b=0; b < num_blocks; b++) {
    sblk = (src_idx + b) % ctx->Nb_host;
    dblk = (dst_idx + b) % ctx->Nb;

    rc = cudaMemcpy2D(gpu_ctx->d_fft_in + dblk * gpu_ctx->guppi_channel_stride,
                      ctx->Nb * gpu_ctx->guppi_channel_stride,  // dpitch
                      ctx->h_blkbufs[sblk],                     // *src
                      gpu_ctx->guppi_channel_stride,            // spitch
                      gpu_ctx->guppi_channel_stride,            // width
                      ctx->Nc,                                  // height
                      cudaMemcpyHostToDevice);

    if(rc != cudaSuccess) {
      PRINT_ERRMSG(rc);
      return 1;
    }
  }

  return 0;
}

// Sets `num_blocks` blocks to zero in GPU input buffer, starting with block at
// `dst_idx`.  If `dst_idx + num_blocks > cts->Nb`, the zeroed blocks will wrap
// to the beginning of the input buffer, but no processing will occur.  Callers
// should avoid this case as it will likely not give the desired results.
// Returns 0 on success, non-zero on error.
int rawspec_zero_blocks_to_gpu(rawspec_context * ctx,
    off_t dst_idx, size_t num_blocks)
{
  int b;
  off_t dblk;
  cudaError_t rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  for(b=0; b < num_blocks; b++) {
    dblk = (dst_idx + b) % ctx->Nb;

    rc = cudaMemset2D(gpu_ctx->d_fft_in + dblk * gpu_ctx->guppi_channel_stride,
                      ctx->Nb * gpu_ctx->guppi_channel_stride,  // pitch
                      0,                                        // value
                      gpu_ctx->guppi_channel_stride,            // width
                      ctx->Nc);                                 // height

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
  size_t c;
  size_t spitch;
  size_t width;
  size_t height;
  cufftHandle plan;
  cudaError_t cuda_rc;
  cufftResult cufft_rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;
  size_t fft_outbuf_length;
  dim3 grid_ics;
  const size_t Nchan_per_antenna = ctx->Nc/ctx->Nant;
  const size_t Nantenna_per_chunk = ctx->Ncc/Nchan_per_antenna;

  // Increment inbuf_count
  gpu_ctx->inbuf_count++;

  // For each output product
  for(i=0; i < ctx->No; i++) {
    // Length of an FFT output buffer when abs(Npotout)==4, must be 0 when
    // Npolout==1
    fft_outbuf_length = ctx->Npolout[i] == 1 ? 0 : ctx->Nb*ctx->Ntpb*ctx->Ncc;
    for(c=0; c < ctx->Nc; c += ctx->Ncc) {

      // For each input polarization
      for(p=0; p < ctx->Np; p++) {
        // Get plan
        plan = gpu_ctx->plan[i][p];

        // Add FFT to stream
        cufft_rc = cufftExecC2C(plan,
                                ((cufftComplex *)gpu_ctx->d_fft_in) + p + (c * ctx->Nb * ctx->Ntpb * ctx->Np),
                                gpu_ctx->d_fft_out + p * fft_outbuf_length,
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
        if(ctx->Nds[i] < gpu_ctx->Nss[i]) {
          for(p=0; p < abs(ctx->Npolout[i]); p++) {
            accumulate<<<gpu_ctx->grid[i],
                        gpu_ctx->nthreads[i],
                        0, gpu_ctx->compute_stream>>>
                          (
                            gpu_ctx->d_pwr_out[i] + p*ctx->Nb*ctx->Ntpb*ctx->Ncc,
                            MIN(ctx->Nas[i], gpu_ctx->Nss[i]), // Na
                            ctx->Nts[i],                       // xpitch
                            ctx->Nas[i]*ctx->Nts[i],           // ypitch
                            ctx->Nb*ctx->Ntpb                  // zpitch
                          );
          }
        }

        if(ctx->incoherently_sum){
          grid_ics.x = (ctx->Nts[i] * ctx->Ncc)/gpu_ctx->nthreads[i];
          grid_ics.y = abs(ctx->Npolout[i]);
          grid_ics.z = ctx->Nds[i];
          
          incoherent_sum<<<grid_ics, gpu_ctx->nthreads[i], 0, gpu_ctx->compute_stream>>>(
                                        gpu_ctx->d_pwr_out[i], gpu_ctx->d_ics_out[i] + (c%Nchan_per_antenna)*ctx->Nts[i], gpu_ctx->d_Aws + c/Nchan_per_antenna,
                                        Nantenna_per_chunk, ctx->Nts[i],
                                        ctx->Nb*ctx->Ntpb*ctx->Nc/ctx->Nant, // Antenna pitch
                                        ctx->Nb*ctx->Ntpb, // Coarse Channel pitch
                                        ctx->Nb*ctx->Ntpb*ctx->Ncc, // Polarisation pitch
                                        ctx->Nts[i]*ctx->Nas[i], // Spectra pitch
                                        
                                        ctx->Nts[i], // Coarse Channel pitch for ics
                                        ctx->Nts[i]*ctx->Nc/ctx->Nant, // Polarisation pitch for ics
                                        abs(ctx->Npolout[i]) * ctx->Nts[i] * ctx->Nc/ctx->Nant // Spectra pitch for ics
                                        );
        
          if(c + ctx->Ncc >= ctx->Nc){
            // Copy store_cb_data_t array from host to device
            cuda_rc = cudaMemcpyAsync(ctx->h_icsbuf[i],
              gpu_ctx->d_ics_out[i],
              ctx->h_pwrbuf_size[i]/ctx->Nant,
              cudaMemcpyDeviceToHost,
              gpu_ctx->compute_stream);
            if(cuda_rc != cudaSuccess) {
              PRINT_ERRMSG(cuda_rc);
              return 1;
            }
          }
        }

        if(c + ctx->Ncc >= ctx->Nc){
          // Add pre-dump stream callback
          cuda_rc = cudaStreamAddCallback(gpu_ctx->compute_stream,
                                          pre_dump_stream_callback,
                                          (void *)&gpu_ctx->dump_cb_data[i], 0);

          if(cuda_rc != cudaSuccess) {
            PRINT_ERRMSG(cuda_rc);
            return 1;
          }
        }

        for(p=0; p < abs(ctx->Npolout[i]); p++) {
          // Copy integrated power spectra (or spectrum) to host.  This is done as
          // two 2D copies to get channel 0 in the center of the spectrum.  Special
          // care is taken in the unlikely event that Nt is odd.
          src    = gpu_ctx->d_pwr_out[i] + p*ctx->Nb*ctx->Ntpb*ctx->Ncc;
          dst    = ctx->h_pwrbuf[i] + (p*ctx->Nts[i]*ctx->Nc) + (c*ctx->Nts[i]);
          spitch = gpu_ctx->Nss[i] * ctx->Nts[i] * sizeof(float);
          dpitch = ctx->Nts[i] * sizeof(float);
          height = ctx->Ncc;

          for(d=0; d < ctx->Nds[i]; d++) {

            // Lo to hi
            width  = ((ctx->Nts[i]+1) / 2) * sizeof(float);
            cuda_rc = cudaMemcpy2DAsync(dst + ctx->Nts[i]/2,
                                        dpitch,
                                        src,
                                        spitch,
                                        width,
                                        height,
                                        cudaMemcpyDeviceToHost,
                                        gpu_ctx->compute_stream);

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
                                        gpu_ctx->compute_stream);

            if(cuda_rc != cudaSuccess) {
              PRINT_ERRMSG(cuda_rc);
              rawspec_cleanup(ctx);
              return 1;
            }

            // Increment src and dst pointers
            src += ctx->Nts[i] * ctx->Nas[i];
            dst += abs(ctx->Npolout[i]) * ctx->Nts[i] * ctx->Nc;
          }
        }

        if(c + ctx->Ncc >= ctx->Nc){
          // Add post-dump stream callback
          cuda_rc = cudaStreamAddCallback(gpu_ctx->compute_stream,
                                          post_dump_stream_callback,
                                          (void *)&gpu_ctx->dump_cb_data[i], 0);

          if(cuda_rc != cudaSuccess) {
            PRINT_ERRMSG(cuda_rc);
            return 1;
          }
        }

        // Add power buffer clearing cudaMemset call to stream
        cuda_rc = cudaMemsetAsync(gpu_ctx->d_pwr_out[i], 0,
                                  abs(ctx->Npolout[i])*ctx->Nb*ctx->Ntpb*ctx->Ncc*sizeof(float),
                                  gpu_ctx->compute_stream);

        if(cuda_rc != cudaSuccess) {
          PRINT_ERRMSG(cuda_rc);
          return 1;
        }
        if(c + ctx->Ncc >= ctx->Nc && ctx->incoherently_sum){
          // Add ics buffer clearing cudaMemset call to stream
          cuda_rc = cudaMemsetAsync(gpu_ctx->d_ics_out[i], 0,
                                    abs(ctx->Npolout[i])*ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float)/ctx->Nant,
                                    gpu_ctx->compute_stream);
    
          if(cuda_rc != cudaSuccess) {
            PRINT_ERRMSG(cuda_rc);
            return 1;
          }
        }

      } // If time to dump
    } // For each chunk of channels
  } // For each output product

  return 0;
}

int rawspec_copy_blocks_to_gpu_and_start_processing(rawspec_context * ctx, size_t num_blocks, char expand4bps_to8bps, int fft_dir)
{
  if(expand4bps_to8bps){
    rawspec_copy_blocks_to_gpu_expanding_complex4(ctx, 0, 0, num_blocks);
  }
  else{
    rawspec_copy_blocks_to_gpu(ctx, 0, 0, num_blocks);
  }
  return rawspec_start_processing(ctx, fft_dir);
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
    cuda_rc = cudaMemset(gpu_ctx->d_pwr_out[i], 0,
        abs(ctx->Npolout[i])*ctx->Nb*ctx->Ntpb*ctx->Nc*sizeof(float));
    if(cuda_rc != cudaSuccess) {
      PRINT_ERRMSG(cuda_rc);
      return 0;
    }
  }

  // Reset inbuf_count
  gpu_ctx->inbuf_count = 0;

  return 0;
}

// Returns true if the "compute stream" is done processing.
unsigned int rawspec_check_for_completion(rawspec_context * ctx)
{
  int complete = 0;
  cudaError_t rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  rc = cudaStreamQuery(gpu_ctx->compute_stream);
  if(rc == cudaSuccess) {
    complete++;
  }

  return complete;
}

// Waits for any pending output products to be compete processing the current
// input buffer.  Returns zero when complete, non-zero on error.
int rawspec_wait_for_completion(rawspec_context * ctx)
{
  int i = 0;
  cudaError_t rc;
  rawspec_gpu_context * gpu_ctx = (rawspec_gpu_context *)ctx->gpu_ctx;

  for(i=0; i < ctx->No; i++) {
    // Add one final pre-dump stream callback to ensure final output thread can
    // be joined.
    rc = cudaStreamAddCallback(gpu_ctx->compute_stream, pre_dump_stream_callback,
                                    (void *)&gpu_ctx->dump_cb_data[i], 0);
    if(rc != cudaSuccess) {
      PRINT_ERRMSG(rc);
      return 1;
    }
  }

  rc = cudaStreamSynchronize(gpu_ctx->compute_stream);
  if(rc != cudaSuccess) {
    return 1;
  }

  return 0;
}
