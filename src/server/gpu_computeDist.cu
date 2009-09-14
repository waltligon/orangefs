#include <stdio.h>
#include <stdlib.h>

#include "kernel_computeDist.cu"

#include "gpu_device.cuh"

/*
  Each thread works on 1 data point and all the corrensponding dimensions
*/
void gpuComputeDistance(
			float* d_dataPoints,
			int numDataPoints,
			int numDimensions,
			float* d_clusterCentres,
			int numClusters,
			int* d_membership,
			float(*func_distance)(float*, float*, int)
			)
{
  /* Variables */
  int num_threads	= GPU_THREADS_PER_BLOCK;
  int num_blocks_x	= GPU_XBLOCKS_PER_GRID > (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) ? (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) : GPU_XBLOCKS_PER_GRID;

  /* Kernel Execution Parameters */
  dim3 grid(num_blocks_x, 1);
  dim3 block(num_threads, 1, 1);
  unsigned int sharedMem = sizeof(float) * numClusters * numDimensions;

#if DEBUG
  fprintf(stderr, "Distance Computation ... [START]\n");
  fprintf(stderr, "	[INFO] GRID Config	: (%d, 1)\n", num_blocks_x);
  fprintf(stderr, "	[INFO] BLOCK Config	: (%d, 1, 1)\n", num_threads);
#endif

  /* Instantiate the kernels */
  switch(num_threads){
  case 512:
    kernelDistance<512> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 256:
    kernelDistance<256> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 128:
    kernelDistance<128> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 64:
    kernelDistance<64> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 32:
    kernelDistance<32> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 16:
    kernelDistance<16> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 8:
    kernelDistance<8> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 4:
    kernelDistance<4> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  case 2:
    kernelDistance<2> <<< grid, block, sharedMem >>> (d_dataPoints, d_clusterCentres, numDataPoints, numClusters, numDimensions, d_membership);
    break;

  }
}
