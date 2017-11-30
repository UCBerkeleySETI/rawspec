#include "mygpuspec_gpu.h"

#if 0
#include <cuda_runtime.h>
#include <cufft.h>
#include <cufftXt.h>
//#include <helper_functions.h>
#include <helper_cuda.h>

// Texture declarations
texture<char, 1, cudaReadModeNormalizedFloat> char_tex;

__device__ cufftComplex load_callback(void *p_v_in, 
                                      size_t offset, 
                                      void *p_v_user,
                                      void *p_v_shared)
{
  cufftComplex c;
  offset += (cufftComplex *)p_v_in - (cufftComplex *)p_v_user;
  c.x = tex1Dfetch(char_tex, 2*offset  );
  c.y = tex1Dfetch(char_tex, 2*offset+1);
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

__device__ cufftCallbackLoadC d_pcb_load_callback = load_callback;
__device__ cufftCallbackStoreC d_pcb_store_callback = store_callback;

int runTest(int argc, char **argv)
{
  int i, j;

  // Pointers to host memory buffers
  char2 * h_pc2_in;
  float * h_pf_out;

  // Pointers to device memory buffers
  cufftComplex * d_pc2_in;  // FFT input buffer  (char2 really)
  cufftComplex * d_pf2_out; // FFT output buffer (must be full sized and can't integrate in it)
  float        * d_pf_out;  // Power output buffer (integrate power here)

  // FFT plan related variables
  const int Nt   = 512; // FFT size
  const int Nb   = 3;   // batch size 
  const int Ni   = 2;   // interleave size
  const int Nti  = Nt * Ni;
  const int Ntb  = Nt * Nb;
  const int Ntbi = Nt * Nb * Ni;
  int Nt_ = Nt; // So we can make a non-const (int *) from it
  cufftHandle plan;
  size_t work_size = 0;

  // Host copies of callback pointers
  cufftCallbackLoadC h_pcb_load_callback;
  cufftCallbackStoreC h_pcb_store_callback;

  // Allocate host memory
  h_pc2_in  = (char2 *)malloc(Ntbi*sizeof(char2));
  h_pf_out  = (float *)malloc(Ntb *sizeof(float));
  if(!h_pc2_in || !h_pf_out) {
    fprintf(stderr, "could not allocate host memory\n");
    return 1;
  }

  // Allocate device memory
  checkCudaErrors(cudaMalloc((void **)&d_pc2_in,  Ntbi*sizeof(char2)));
  checkCudaErrors(cudaMalloc((void **)&d_pf2_out, Ntb *sizeof(float2)));
  checkCudaErrors(cudaMalloc((void **)&d_pf_out,  Ntb *sizeof(float)));
  // Clear power output buffer
  checkCudaErrors(cudaMemset(d_pf_out, 0, Ntb*sizeof(float)));

  // Bind texture to device input buffer
  checkCudaErrors(cudaBindTexture(NULL, char_tex, d_pc2_in, Ntbi*sizeof(char2)));

  // Allocate plan memory
  checkCudaErrors(cufftCreate(&plan));
  // Make the plan
  checkCudaErrors(cufftMakePlanMany(plan,      // plan handle
                                    1,         // rank
                                    &Nt_,      // *n
                                    &Nt_,      // *inembed (unused for 1d)
                                    Ni,        // istride
                                    Nti,       // idist
                                    &Nt_,      // *onembed (unused for 1d)
                                    1,         // ostride
                                    Nt,        // odist
                                    CUFFT_C2C, // type
                                    Nb,        // batch
                                    &work_size // worksize
                                   ));

  printf("Temporary buffer size %li bytes\n", work_size);

  // Setup the callbacks
  cudaMemcpyFromSymbol(&h_pcb_load_callback, 
                       d_pcb_load_callback, 
                       sizeof(h_pcb_load_callback));

  cudaMemcpyFromSymbol(&h_pcb_store_callback, 
                       d_pcb_store_callback, 
                       sizeof(h_pcb_store_callback));

  // Now associate the callbacks with the plan.
  cufftResult status = cufftXtSetCallback(plan,
                                          (void **)&h_pcb_load_callback,
                                          CUFFT_CB_LD_COMPLEX,
                                          (void **)&d_pc2_in);
  if (status == CUFFT_LICENSE_ERROR)
  {
      printf("Apparently, using CUFFT callbacks requires a valid license file.\n");
      printf("The file was either not found, out of date, or otherwise invalid.\n");
      return 1;
  }
  checkCudaErrors(cufftXtSetCallback(plan,
                                     (void **)&h_pcb_load_callback,
                                     CUFFT_CB_LD_COMPLEX,
                                     (void **)&d_pc2_in));

  checkCudaErrors(cufftXtSetCallback(plan,
                                     (void **)&h_pcb_store_callback,
                                     CUFFT_CB_ST_COMPLEX,
                                     (void **)&d_pf_out));

  // Populate input data
  memset(h_pc2_in, 0, Ntbi * sizeof(char2));
  for(i=0; i<Nt; i++) {
    // Even samples, odd bins
    h_pc2_in[2*(i       )].x = round(127*cos(2*M_PI*1*i/Nt)); // Bin 1
    h_pc2_in[2*(i       )].y = round(127*sin(2*M_PI*1*i/Nt)); // Bin 1
    h_pc2_in[2*(i +   Nt)].x = round(127*cos(2*M_PI*3*i/Nt)); // Bin 3
    h_pc2_in[2*(i +   Nt)].y = round(127*sin(2*M_PI*3*i/Nt)); // Bin 3
    h_pc2_in[2*(i + 2*Nt)].x = round(127*cos(2*M_PI*5*i/Nt)); // Bin 5;
    h_pc2_in[2*(i + 2*Nt)].y = round(127*sin(2*M_PI*5*i/Nt)); // Bin 5;

    // Odd samples, even bind, half power relative to even samples
    h_pc2_in[2*(i       )+1].x = round(89.8*cos(2*M_PI*2*i/Nt)); // Bin 2
    h_pc2_in[2*(i       )+1].y = round(89.8*sin(2*M_PI*2*i/Nt)); // Bin 2
    h_pc2_in[2*(i +   Nt)+1].x = round(89.8*cos(2*M_PI*4*i/Nt)); // Bin 4
    h_pc2_in[2*(i +   Nt)+1].y = round(89.8*sin(2*M_PI*4*i/Nt)); // Bin 4
    h_pc2_in[2*(i + 2*Nt)+1].x = round(89.8*cos(2*M_PI*6*i/Nt)); // Bin 6;
    h_pc2_in[2*(i + 2*Nt)+1].y = round(89.8*sin(2*M_PI*6*i/Nt)); // Bin 6;
  }

  // Copy data to GPU
  checkCudaErrors(cudaMemcpy(d_pc2_in, h_pc2_in, Ntbi*sizeof(char2),
                             cudaMemcpyHostToDevice));

  for(j=0; j<Ni; j++) {
    // Do FFT, integrating all interleaved outputs together
    checkCudaErrors(cufftExecC2C(plan, d_pc2_in+j, d_pf2_out, CUFFT_FORWARD));
  }

  // Copy data back from the GPU
  checkCudaErrors(cudaMemcpy(h_pf_out, d_pf_out, Ntb*sizeof(float),
                             cudaMemcpyDeviceToHost));

  // Show output
  for(i=0; i<17; i++) {
    printf("%3d", i);
    printf("  %+11.8f",   h_pf_out[i     ]/(512*512));
    printf("  %+11.8f",   h_pf_out[i+  Nt]/(512*512));
    printf("  %+11.8f\n", h_pf_out[i+2*Nt]/(512*512));
  }
  printf("\n");

  //Destroy CUFFT plan
  checkCudaErrors(cufftDestroy(plan));

  // Ub-bind texture from device input buffer
  cudaUnbindTexture(char_tex);

  // Free device memory
  checkCudaErrors(cudaFree(d_pf2_out));
  checkCudaErrors(cudaFree(d_pc2_in));
  free(h_pf_out);
  free(h_pc2_in);

  return 0;
}
#endif // 0
