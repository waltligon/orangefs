#ifndef _KERNEL_UPDATECLUSTER_H_
#define _KERNEL_UPDATECLUSTER_H_

#include <stdio.h>

/* We have a 2D grid of blocks
   Each row of the grid takes care of each dimension
   Each thread in each column of the grid takes care of each data point
   i.e., one block works only with one dimension, different dimensions are on different blocks

   Notice that in the for loop, we let each cluster to be computed in each iteration
   We can also let multiple clusters to work in one iteration or all clusters work in one iteration
   I guess it depends on the size of the shared memory which can be allocated!!!
*/
template<unsigned int blockSize>
__global__ void kernelCluster(
			      float* d_dataPoints,
			      int numDataPoints,
			      int numDimensions,
			      float* d_clusterCentresSum,
			      int numClusters,
			      int* d_membership
			      )
{
  /* Shared memory size = numclusters * blockSize */
  extern __shared__ float s_clusterCentres[];

  /* Thread/Block/Grid IDs*/
  unsigned int tidx = threadIdx.x;
  unsigned int block_row = blockIdx.y;
  unsigned int block_col = blockIdx.x;
  unsigned int gridSize = gridDim.x * blockSize;	// Total number of threads in the x-dim in the grid

  // Initialize s_clusterCentres to 0
  // 0 - (blockSize-1) is cluster 0, blockSize to (2*blockSize-1) is cluster 1 and so on
  //[FIX] Does this lead to memory conflicts!!!
  for(unsigned int i = 0; i < numClusters; i++) s_clusterCentres[(blockSize * i) + tidx] = 0.0f;

  /* Analyse each cluster at a time */
  // [MODIFY] You can analyse all the clusters at the same time too 
  for(unsigned int cluster = 0; cluster < numClusters; cluster++) {
    __syncthreads();

    // Get thread gets each data point
    unsigned int thread_dp = (block_col * blockSize) + tidx;  // I doubt is this is exactly correct !!!
    unsigned int base_index = cluster * blockSize;

    // Each thread computes (numDataPoints/gridSize) elements
    while(thread_dp < numDataPoints){
      // Get the cluster id for the data point
      unsigned int clusterIndex = d_membership[thread_dp];

      // Add in the s_clusterCentres
      /* This assumes that dataPoints are numDataPoints * numDimensions
	 i.e., cols = dimensions and rows = data points
	 d_dataPoints[(thread_dp * numDimensions) + block_row] = d_dataPoints[thread_dp][block_row]
      */

      // [MODIFY] This is not an optimized access, there will be too many memory conflicts
      // It wud be better if you can tranpose the dataPoints array, i.e., col as data points, row as dimensions
      if(clusterIndex == cluster)
	s_clusterCentres[base_index + tidx] += d_dataPoints[(thread_dp * numDimensions) + block_row];
												
      thread_dp += gridSize;
    }//End while
    __syncthreads();

    // Now reduce the particular row of the s_clusterCentres, i.e., 'cluster' row
    if(blockSize >= 512){
      if(tidx < 256) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 256];
      __syncthreads();
    }
								
    if(blockSize >= 256){
      if(tidx < 128) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 128];
      __syncthreads();
    }
								
    if(blockSize >= 128){
      if(tidx < 64) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 64];
      __syncthreads();
    }

    if(tidx < 32){
      if(blockSize >= 64) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 32];
      if(blockSize >= 32) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 16];
      if(blockSize >= 16) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 8];
      if(blockSize >= 8) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 4];
      if(blockSize >= 4) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 2];
      if(blockSize >= 2) s_clusterCentres[base_index + tidx] += s_clusterCentres[base_index + tidx + 1];
    } //End if tidx<32

    // Commit the cluster centres, you still have lots of data to add as you are not adding across blocks, just within blocks
    if(tidx == 0){
      // You should have an array with size (numClusters * gridDim.x) * numDimensions
      /* Each y-Block copies in a different dimension
	 For each cluster all the x-blocks copies at consecutive locations
      */
      d_clusterCentresSum[(gridDim.x * numClusters * block_row) + ((cluster * gridDim.x) + block_col)] = s_clusterCentres[(cluster * blockSize)];
    }//End if tidx
  }//End for cluster

}

/*
  We can have grid as (numClusters, numDimensions)
  So each x-block handles one cluster which has a size = gridDim.x in the previous invokation
  and each y-block handles one dimension
  [FUTURE] You can also make multiple blocks take care of one cluster, but that will be for future work
*/
template<unsigned int blockSize>
__global__ void kernelClusterReduce(
				    float* d_clusterCentresSum,
				    int pitch_clusterPoints,
				    int* d_clusterHistogram,
				    float* d_clusterCentresReduced
				    )
{
  /* Shared Memory size = blockSize */
  extern __shared__ float s_clusterCentres[];

  /* Thread IDs*/
  unsigned int tidx = threadIdx.x;
  unsigned int block_row = blockIdx.y;
  unsigned int block_col = blockIdx.x;
  unsigned int gridSize = gridDim.x * blockSize;

  /* Each thread takes care of one cluster, element combo */
  // Load the shared data, cp = cluster point
  // each thread gets gridDim.x(in previoud invocation) / blockSize
  unsigned int base	= gridDim.x * pitch_clusterPoints * block_row;
  unsigned int start	= base + (block_col * pitch_clusterPoints) + tidx;
  unsigned int end	= base + ((block_col + 1) * pitch_clusterPoints);

  /* Initialize the shared memory to 0 */
  s_clusterCentres[tidx] = 0.0f;

  for(unsigned int cp = start; cp < end; cp += blockSize){
    s_clusterCentres[tidx] += d_clusterCentresSum[cp];
  }
  __syncthreads();

  // Do the reduction in the shared memory
  if(blockSize >= 512){
    if(tidx < 256) s_clusterCentres[tidx] += s_clusterCentres[tidx + 256];
    __syncthreads();
  }
				
  if(blockSize >= 256){
    if(tidx < 128) s_clusterCentres[tidx] += s_clusterCentres[tidx + 128];
    __syncthreads();
  }
				
  if(blockSize >= 128){
    if(tidx < 64) s_clusterCentres[tidx] += s_clusterCentres[tidx + 64];
    __syncthreads();
  }

  if(tidx < 32){
    if(blockSize >= 64) s_clusterCentres[tidx] += s_clusterCentres[tidx + 32];
    if(blockSize >= 32) s_clusterCentres[tidx] += s_clusterCentres[tidx + 16];
    if(blockSize >= 16) s_clusterCentres[tidx] += s_clusterCentres[tidx + 8];
    if(blockSize >= 8) s_clusterCentres[tidx] += s_clusterCentres[tidx + 4];
    if(blockSize >= 4) s_clusterCentres[tidx] += s_clusterCentres[tidx + 2];
    if(blockSize >= 2) s_clusterCentres[tidx] += s_clusterCentres[tidx + 1];
  }

  // Commit the cluster assignments
  // [FIX!!!] Remember we are still not dividing the summation with the number of clusters
  if(tidx == 0)
    d_clusterCentresReduced[(gridDim.y * block_col) + block_row] = s_clusterCentres[0];///d_clusterHistogram[block_col];

}

#endif // ifndef _KERNEL_UPDATECLUSTER_H_
