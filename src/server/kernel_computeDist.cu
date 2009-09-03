#ifndef _KERNEL_DISTANCE_H_
#define _KERNEL_DISTANCE_H_

#include <stdio.h>
#include <float.h>

template<unsigned int blockSize>
__global__ void kernelDistance(
			       float* g_dataPoints,
			       float* g_clusterCentres,
			       int numDataPoints,
			       int numClusters,
			       int numDimension,
			       int* g_membership
			       )
{
  extern __shared__ float s_clusterCentres[];

  /* Load the shared memory for each Block */
  unsigned int tidx = threadIdx.x;
  //unsigned int i = tidx * numDimension;
  unsigned int dp = blockSize * blockIdx.x + tidx;
  unsigned int gridSize = gridDim.x * blockSize;

  /* Copy the Cluster centres in the shared memory */
  // [FIX] you can make this more optimized!!!
  // Each thread copies one dimension of all the clusters 
  if(tidx < numDimension)
    for(unsigned int clus = 0; clus < numClusters; clus++){
      unsigned int base = numDimension * clus;
      s_clusterCentres[base + tidx] = g_clusterCentres[base + tidx];
    }
  __syncthreads();

  /* Compute Distance */
  while(dp < numDataPoints){
    float min_dist = FLT_MAX;
    for(unsigned int cc = 0; cc < numClusters; cc++){
      float dist = 0.0;
      int cc_offset = cc * numDimension;

      for(unsigned int dim = 0; dim < numDimension; dim++){
	float temp = g_dataPoints[numDimension * dp + dim] - s_clusterCentres[cc_offset + dim];
	dist += temp * temp;
      } //End for dim

      if(dist < min_dist){
	min_dist = dist;
	g_membership[dp] = cc;
      } //End if dist
    } //End for cc
    dp += gridSize;
  }
  __syncthreads();

}

#endif // #ifndef _KERNEL_DISTANCE_H_
