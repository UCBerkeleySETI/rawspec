#ifndef _MYGPUSPEC_H_
#define _MYGPUSPEC_H_

// Structure for holding the context.
typedef struct {
  int No; // Number of output products (max 4)
  int Np; // Number of polarizations
  int Nc; // Number of coarse channels
  int Nts[4]; // Array of Nt values
  int Nas[4]; // Array of Na values
  int Ntmax; // Maximum Nt value
  char * h_blkbuf; // Host pointer to host block buffer
  float * h_pwrbuf[4]; // Host pointer to array of host output buffers
  void * mygpuspec_gpu_ctx; // Host pointer to GPU specific context
} mygpuspec_context;

#endif // _MYGPUSPEC_H_
