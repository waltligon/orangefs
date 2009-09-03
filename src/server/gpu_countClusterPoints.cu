#include <stdio.h>
#include <stdlib.h>

//#include <cutil.h>
#include "kernel_countClusterPoints.cu"

#include "gpu_device.cuh"

void gpuCountClusterPoints(
			   int* d_membership,
			   int numDataPoints,
			   int numClusters,
			   int* d_newClusterHistogram
			   )
{
  /* Variables */
  int num_threads		= GPU_THREADS_PER_BLOCK;
  int num_blocks_x	= GPU_XBLOCKS_PER_GRID > (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) ? (1 + ((numDataPoints - 1) / GPU_THREADS_PER_BLOCK)) : GPU_XBLOCKS_PER_GRID;

  /* Kernel Execution Parameters */
  dim3 grid_count(num_blocks_x, 1);
  dim3 block_count(num_threads, 1, 1);
  unsigned int sharedMem_count = sizeof(int) * numClusters * num_threads;

  /* During reduction each x-block takes a cluster */
  //int num_threads_reduce = GPU_THREADS_PER_BLOCK > num_blocks_x ? num_blocks_x : GPU_THREADS_PER_BLOCK;
  dim3 grid_countReduce(numClusters, 1);
  dim3 block_countReduce(num_threads, 1, 1);
  unsigned int sharedMem_countReduce = sizeof(int) * num_threads;

  //printf("Count Cluster Points ... [START]\n");
  //printf("	[INFO] GRID Config Count	: (%d, 1)\n", num_blocks_x);
  //printf("	[INFO] BLOCK Config Count	:	(%d, 1, 1)\n", num_threads);
  //printf("	[INFO] GRID Config Count Reduce	: (%d, 1)\n", numClusters);
  //printf("	[INFO] BLOCK Config Count Reduce:	(%d, 1, 1)\n", num_threads);

  /**/
  // Get an array with size num_blocks_x * numClusters
  int* d_clusterPointsCount = NULL;
  cudaMalloc( (void**) &d_clusterPointsCount, sizeof(int) * num_blocks_x * numClusters);

  /* Kernel Invokation */
  switch(num_threads){
  case 512 :
    kernelCount<512> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<512> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 256 :
    kernelCount<256> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<256> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 128 :
    kernelCount<128> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<128> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 64 :
    kernelCount<64> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<64> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 32 :
    kernelCount<32> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<32> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 16 :
    kernelCount<16> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<16> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 8 :
    kernelCount<8> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<8> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 4 :
    kernelCount<4> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<4> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;

  case 2 :
    kernelCount<2> <<< grid_count, block_count, sharedMem_count>>> (d_membership, numDataPoints, numClusters, d_clusterPointsCount);
    kernelCountReduce<2> <<< grid_countReduce, block_countReduce, sharedMem_countReduce>>> (d_clusterPointsCount, num_blocks_x, d_newClusterHistogram);
    break;
  }
				
  //int* test_out = (int*)malloc(sizeof(int) * num_blocks_x * numClusters);
  //CUDA_SAFE_CALL( cudaMemcpy(test_out, d_clusterPointsCount, sizeof(int) * num_blocks_x * numClusters, cudaMemcpyDeviceToHost) );
  //printf("Test Out: %d\n", test_out[0]);
  //for(int i = 0; i < num_blocks_x; i++){
  //				for(int j = 0; j < numClusters; j++){
  //								printf("%d, ", test_out[num_blocks_x * i + j]);
  //				}
  //				printf("\n");
  //}
  //
  //test_out = (int*)malloc(sizeof(int) * numClusters);
  //CUDA_SAFE_CALL( cudaMemcpy(test_out, d_newClusterHistogram, sizeof(int) * numClusters, cudaMemcpyDeviceToHost) );
  //for(int i = 0; i < numClusters; i++) printf("%d, ", test_out[i]);
  //printf("\n");

  //int* abc = (int*)malloc(sizeof(int) * numDataPoints);
  //CUDA_SAFE_CALL( cudaMemcpy(abc, d_membership, sizeof(int) * numDataPoints, cudaMemcpyDeviceToHost) );
  //int sum = 0;
  //for(int i = 0; i < numDataPoints; i++) sum += (abc[i] + 1);
  //printf("Sum : %d\n", sum);

  //printf("Count Cluster Points ... [DONE]\n");

}
