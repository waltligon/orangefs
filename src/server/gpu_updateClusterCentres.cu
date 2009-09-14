#include <stdio.h>
#include <stdlib.h>

#include <cutil.h>
#include "kernel_updateClusterCentres.cu"

#include "gpu_device.cuh"

void gpuUpdateClusterCentres(
			     int* d_membership,
			     float* d_dataPoints,
			     int numDataPoints,
			     int numDimensions,
			     int numClusters,
			     int* d_clusterHistogram,
			     float* d_newClusterCentres
			     )
{
  /* Variables */
  int num_threads	= GPU_THREADS_PER_BLOCK;
  int num_blocks_x	= GPU_XBLOCKS_PER_GRID > (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) ? (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) : GPU_XBLOCKS_PER_GRID;
  int num_blocks_y	= numDimensions;

  /* Kernel Execution Parameters */
  dim3 grid_cluster(num_blocks_x, num_blocks_y); /* (128, 18) */
  dim3 block_cluster(num_threads, 1, 1); /* (128, 1, 1) */
  unsigned int sharedMem_cluster = sizeof(float) * numClusters * num_threads;

  /* During reduction each x-block takes a cluster and each y-block takes a dimension */
  //int num_threads_reduce = GPU_THREADS_PER_BLOCK > num_blocks_x ? num_blocks_x : GPU_THREADS_PER_BLOCK;
  dim3 grid_clusterReduce(numClusters, numDimensions);
  dim3 block_clusterReduce(num_threads, 1, 1);

  unsigned int sharedMem_clusterReduce = sizeof(float) * num_threads;

#if DEBUG
  fprintf(stderr, "Update Cluster Centres ... [START]\n");
  fprintf(stderr, "	[INFO] GRID Config Cluster	: (%d, %d)\n", num_blocks_x, num_blocks_y);
  fprintf(stderr, "	[INFO] BLOCK Config Cluster	:	(%d, 1, 1)\n", num_threads);
  fprintf(stderr, "	[INFO] GRID Config Cluster Reduce	: (%d, %d)\n", numClusters, numDimensions);
  fprintf(stderr, "	[INFO] BLOCK Config Cluster Reduce:	(%d, 1, 1)\n", num_threads);
#endif

  /**/
  // Get an array with size num_blocks_x * numClusters * numDimensions
  float* d_clusterCentresSum = NULL;
  float* h_clusterCentresSum = (float *)malloc(sizeof(float)*num_blocks_x*numClusters*numDimensions);
  cudaMalloc( (void**) &d_clusterCentresSum, sizeof(float)*num_blocks_x*numClusters*numDimensions );
  cudaMemset(d_clusterCentresSum, 0.0f, sizeof(float)*num_blocks_x*numClusters*numDimensions);

  /* Kernel Invokation */
  switch(num_threads){
  case 512 :
    kernelCluster<512> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<512> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 256 :
    kernelCluster<256> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<256> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 128 :
    kernelCluster<128> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<128> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 64 :
    kernelCluster<64> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<64> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 32 :
    kernelCluster<32> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<32> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 16 :
    kernelCluster<16> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<16> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 8 :
    kernelCluster<8> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<8> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 4 :
    kernelCluster<4> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<4> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  case 2 :
    kernelCluster<2> <<< grid_cluster, block_cluster, sharedMem_cluster>>> (d_dataPoints, numDataPoints, numDimensions, d_clusterCentresSum, numClusters, d_membership);
    kernelClusterReduce<2> <<< grid_clusterReduce, block_clusterReduce, sharedMem_clusterReduce>>> (d_clusterCentresSum, num_blocks_x, d_clusterHistogram, d_newClusterCentres);
    break;

  }

#if DEBUG				
  float* test_out = (float*)malloc(sizeof(float) * num_blocks_x * numClusters * numDimensions);
  cudaMemcpy(test_out, d_clusterCentresSum, sizeof(float) * num_blocks_x * numClusters * numDimensions, cudaMemcpyDeviceToHost);

  fprintf(stderr, "Test Out : ");
  for(int i = 0; i < numDimensions; i++) fprintf(stderr, "%f, ", test_out[i]);
  fprintf(stderr, "\n");
#endif	

}
