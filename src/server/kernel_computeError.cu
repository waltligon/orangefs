#include <stdio.h>
#include <math.h>

template <unsigned int blockSize>
__global__ void kernelError(
			    int* d_newMembership,
			    int* d_oldMembership,
			    int numDataPoints,
			    float* d_partialError
			    )
{
  /* Shared Memory size = blockSize */
  extern __shared__ float s_partialError[];

  /* Threads IDs*/
  unsigned int tidx = threadIdx.x;
  unsigned int block_col = blockIdx.x;
  unsigned int gridSize = gridDim.x * blockSize;

  /* Initialize the shared memory */
  s_partialError[tidx] = 0;

  /* Compute the error */
  unsigned int ep = (blockSize * block_col) + tidx;
  while(ep < numDataPoints){
    // You can use a ternary operator too!!!
    s_partialError[tidx] += (d_newMembership[ep] == d_oldMembership[ep] ? 0 : 1);
    //s_partialError[tidx] += abs(d_newMembership[ep] - d_oldMembership[ep]);
    ep += gridSize;
  }
  __syncthreads();

  /* Do the partial reduction in the shared memory */
  if(blockSize >= 512){
    if(tidx < 256) s_partialError[tidx] += s_partialError[tidx + 256];
    __syncthreads();
  }
				
  if(blockSize >= 256){
    if(tidx < 128) s_partialError[tidx] += s_partialError[tidx + 128];
    __syncthreads();
  }
				
  if(blockSize >= 128){
    if(tidx < 64) s_partialError[tidx] += s_partialError[tidx + 64];
    __syncthreads();
  }

  if(tidx < 32){
    if(blockSize >= 64) s_partialError[tidx] += s_partialError[tidx + 32];
    if(blockSize >= 32) s_partialError[tidx] += s_partialError[tidx + 16];
    if(blockSize >= 16) s_partialError[tidx] += s_partialError[tidx + 8];
    if(blockSize >= 8) s_partialError[tidx] += s_partialError[tidx + 4];
    if(blockSize >= 4) s_partialError[tidx] += s_partialError[tidx + 2];
    if(blockSize >= 2) s_partialError[tidx] += s_partialError[tidx + 1];
  }

  /* Write the results to a remperoray array for further reduction */
  if(tidx == 0){
    d_partialError[block_col] = s_partialError[0];
  }
}

template <unsigned int blockSize>
__global__ void kernelErrorReduce(
				  float* d_partialError,
				  int numPartialErrorPoints,
				  float* d_error
				  )
{
  extern __shared__ float s_error[];

  /* Thread IDs*/
  unsigned int tidx = threadIdx.x;
				
  /* Initialize the shared data */
  s_error[tidx] = 0;

  /* Copy data to the shared memory */
  for(unsigned int i = tidx; i < numPartialErrorPoints; i += blockSize)
    s_error[tidx] += d_partialError[i];

  __syncthreads();

  /* Do the complete reduction in the shared memory */
  if(blockSize >= 512){
    if(tidx < 256) s_error[tidx] += s_error[tidx + 256];
    __syncthreads();
  }
				
  if(blockSize >= 256){
    if(tidx < 128) s_error[tidx] += s_error[tidx + 128];
    __syncthreads();
  }
				
  if(blockSize >= 128){
    if(tidx < 64) s_error[tidx] += s_error[tidx + 64];
    __syncthreads();
  }

  if(tidx < 32){
    if(blockSize >= 64) s_error[tidx] += s_error[tidx + 32];
    if(blockSize >= 32) s_error[tidx] += s_error[tidx + 16];
    if(blockSize >= 16) s_error[tidx] += s_error[tidx + 8];
    if(blockSize >= 8) s_error[tidx] += s_error[tidx + 4];
    if(blockSize >= 4) s_error[tidx] += s_error[tidx + 2];
    if(blockSize >= 2) s_error[tidx] += s_error[tidx + 1];
  }

  /* Final result to the output */
  if(tidx == 0) d_error[0] = s_error[0];

}
