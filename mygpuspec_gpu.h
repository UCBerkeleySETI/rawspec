#ifndef _MYGPUSPEC_GPU_H_
#define _MYGPUSPEC_GPU_H_

#include <cufft.h>

typedef struct {
  char2 * d_fft_in; // Device pointer to FFT input buffer
  cufftComplex * d_fft_out[4]; // Array of device pointers to FFT output buffers
  float * d_pwr_out[4]; // Array of device pointers to power buffers
  cufftHandle plan[4]; // Array of handles to FFT plans
} mygpuspec_gpu_context;

#endif // _MYGPUSPEC_GPU_H_

