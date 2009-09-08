#include <stdio.h>
#include <stdlib.h>

//#include <cutil.h>
#include "kernel_computeError.cu"

#include "gpu_device.cuh"

void gpuComputeError(int* d_newMembership,
		     int* d_oldMembership,
		     int numDataPoints,
		     float* error)
{
  /* Variables */
  int num_threads	= GPU_THREADS_PER_BLOCK;
  int num_blocks_x	= GPU_XBLOCKS_PER_GRID > (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) ? (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) : GPU_XBLOCKS_PER_GRID;

  /* Kernel Execution parameters */
  dim3 grid_partialError(num_blocks_x, 1);
  dim3 block_partialError(num_threads, 1, 1);
  unsigned int sharedMem_partialError = sizeof(int) * num_threads;

  dim3 grid_error(1, 1);
  dim3 block_error(num_threads, 1, 1);
  unsigned int sharedMem_error = sizeof(int) * num_threads;

  //printf("Error Computation ... [START]\n");
  //printf("	[INFO] GRID Config Error	: (%d, 1)\n", num_blocks_x);
  //printf("	[INFO] BLOCK Config Error	:	(%d, 1, 1)\n", num_threads);
  //printf("	[INFO] GRID Config Error Reduce	: (1, 1)\n");
  //printf("	[INFO] BLOCK Config Error Reduce:	(%d, 1, 1)\n", num_threads);

  float* d_error = NULL;
  float* d_partialError = NULL;
  cudaMalloc( (void**) &d_error, sizeof(float) );
  cudaMalloc( (void**) &d_partialError, sizeof(float) * num_blocks_x);

  /* Kernel Execution */
  switch(num_threads)
    {
    case 512: 
      kernelError<512> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<512> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 256: 
      kernelError<256> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<256> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 128: 
      kernelError<128> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<128> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 64: 
      kernelError<64> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<64> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 32: 
      kernelError<32> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<32> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 16: 
      kernelError<16> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<16> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 8: 
      kernelError<8> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<8> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 4: 
      kernelError<4> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<4> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    case 2: 
      kernelError<2> <<< grid_partialError, block_partialError, sharedMem_partialError >>> (d_newMembership, d_oldMembership, numDataPoints, d_partialError);
      kernelErrorReduce<2> <<< grid_error, block_error, sharedMem_error >>> (d_partialError, num_blocks_x, d_error);
      break;

    }

  cudaMemcpy(error, d_error, sizeof(float), cudaMemcpyDeviceToHost);
				
  //printf("Error Computation ... [DONE]\n");
}
