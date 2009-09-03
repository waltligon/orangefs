#ifndef _KERNEL_COUNTCLUSTERPOINTS_H_
#define _KERNEL_COUNTCLUSTERPOINTS_H_

#include <stdio.h>

/* We have a 1D grid of blocks
   Each thread in each column of the grid takes care of each data point

   Notice that in the for loop, we let each cluster to be computed in each iteration
   We can also let multiple clusters to work in one iteration or all clusters work in one iteration
   I guess it depends on the size of the shared memory which can be allocated!!!
*/

/* Unrolling helps in improving the performance
   If you know at compile time how many iterations you need
   then you can unroll using the templates!!!
*/
template<unsigned int blockSize>
__global__ void kernelCount(
			    int* d_membership,					// Cluster membership info for each data point
			    int numDataPoints,
			    int numClusters,
			    int* d_clusterPointsCount	// Cluster Histogram information 
			    )
{
  /* Shared memory size = numclusters * blockSize */
  extern __shared__ int s_clusterPoints[];

  /* Thread/Block/Grid IDs*/
  unsigned int tidx = threadIdx.x;
  unsigned int block_col = blockIdx.x;							// x-block Id of the blocks in the grid
  unsigned int gridSize = gridDim.x * blockSize;		// Total number of threads in x-dim on the grid

  // Initialize s_clusterPoints to 0
  for(unsigned int i = 0; i < numClusters; i++) s_clusterPoints[(blockSize * i) + tidx] = 0;

  /* Analyse each cluster at a time */
  // [MODIFY] You can analyse all clusters at the same time too
  for(unsigned int cluster = 0; cluster < numClusters; cluster++){
    __syncthreads();

    // Each thread gets each data point
    unsigned int thread_dp = (block_col * blockSize) + tidx;  // I doubt is this is exactly correct !!!
    unsigned int base_index = cluster * blockSize;

    // Each thread computes (numDataPoints/gridSize) elements
    while(thread_dp < numDataPoints){
      // Get the cluster id for the data point
      unsigned int clusterIndex = d_membership[thread_dp];

      // Add in the s_clusterCentres
      /* This assumes that dataPoints are numDataPoints * numDimensions
	 i.e., cols = dimensions and rows = data points
	 d_dataPoints[(thread_dp * numDimensions) + gridRow] = d_dataPoints[thread_dp][gridRow]
      */
      if(clusterIndex == cluster)
	s_clusterPoints[base_index + tidx] += 1;
												
      thread_dp += gridSize;
    }//End while
    __syncthreads();

    //Now reduce the particular row of the s_clusterCentres, i.e., 'cluster' row
    if(blockSize >= 512){
      if(tidx < 256) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 256];
      __syncthreads();
    }
								
    if(blockSize >= 256){
      if(tidx < 128) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 128];
      __syncthreads();
    }
								
    if(blockSize >= 128){
      if(tidx < 64) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 64];
      __syncthreads();
    }

    if(tidx < 32){
      if(blockSize >= 64) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 32];
      if(blockSize >= 32) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 16];
      if(blockSize >= 16) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 8];
      if(blockSize >= 8) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 4];
      if(blockSize >= 4) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 2];
      if(blockSize >= 2) s_clusterPoints[base_index + tidx] += s_clusterPoints[base_index + tidx + 1];
    }//End if tidx<32

    // Commit the cluster centres, you still have lots of data to add as you are not adding across blocks, just within blocks
    if(tidx == 0){
      // You should have an array with size (numClusters * gridDim.x) * numDimensions
      /* Each y-Block copies in a different dimension
	 For each cluster all the x-blocks copies at consecutive locations
      */
      d_clusterPointsCount[(cluster * gridDim.x) + block_col] = s_clusterPoints[(cluster * blockSize)];
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
__global__ void kernelCountReduce(
				  int* d_clusterPointsCount,
				  int pitch_clusterPoints,
				  int* d_clusterHistogram
				  )
{
  /* Shared Memory size = blockSize */
  extern __shared__ int s_clusterPoints[];

  /* Thread IDs*/
  unsigned int tidx = threadIdx.x;
  unsigned int block_col = blockIdx.x;						// x-block ID of the blocks in the grid
  unsigned int gridSize = gridDim.x * blockSize;	// Total number of threads in x-dim in the grid

  /* Each thread takes care of one cluster, element combo */
  // Load the shared data, cp = cluster point
  // each thread gets gridDim.x(in previoud invocation) / blockSize
  unsigned int start	= (block_col * pitch_clusterPoints) + tidx;
  unsigned int end		= (block_col + 1) * pitch_clusterPoints;

  s_clusterPoints[tidx] = 0;

  for(unsigned int cluster_point_count = start; cluster_point_count < end; cluster_point_count += blockSize){
    s_clusterPoints[tidx] += d_clusterPointsCount[cluster_point_count];
  }
  __syncthreads();

  // Do the reduction in the shared memory
  if(blockSize >= 512){
    if(tidx < 256) s_clusterPoints[tidx] += s_clusterPoints[tidx + 256];
    __syncthreads();
  }
				
  if(blockSize >= 256){
    if(tidx < 128) s_clusterPoints[tidx] += s_clusterPoints[tidx + 128];
    __syncthreads();
  }
				
  if(blockSize >= 128){
    if(tidx < 64) s_clusterPoints[tidx] += s_clusterPoints[tidx + 64];
    __syncthreads();
  }

  if(tidx < 32){
    if(blockSize >= 64) s_clusterPoints[tidx] += s_clusterPoints[tidx + 32];
    if(blockSize >= 32) s_clusterPoints[tidx] += s_clusterPoints[tidx + 16];
    if(blockSize >= 16) s_clusterPoints[tidx] += s_clusterPoints[tidx + 8];
    if(blockSize >= 8) s_clusterPoints[tidx] += s_clusterPoints[tidx + 4];
    if(blockSize >= 4) s_clusterPoints[tidx] += s_clusterPoints[tidx + 2];
    if(blockSize >= 2) s_clusterPoints[tidx] += s_clusterPoints[tidx + 1];
  }

  //Commit the cluster assignments
  // [FIX!!!] Remember we are still not diving the summation with the number of clusters
  if(tidx == 0){
    d_clusterHistogram[block_col] = s_clusterPoints[0];
  }

}

#endif // ifndef _KERNEL_COUNTCLUSTERPOINTS_H_
