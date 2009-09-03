#include <stdio.h>

#include <cutil.h>

void gpuInitializeMemory(
			 float** h_dataPoints,
			 float** h_clusterCentres,
			 int* h_membership,
			 int numDataPoints,
			 int numClusters,
			 int numDimensions,
			 float* d_dataPoints,
			 float* d_clusterCentres,
			 int* d_membership,
			 size_t d_pitch_dataPoints,
			 size_t d_pitch_clusterCentres
			 )
{

  /* Define the variables */
  int widthBytes_dataPoints			= sizeof(float) * numDimensions;
  int widthBytes_clusterCentres	= sizeof(float) * numDimensions;
  int size_membership						= sizeof(int) * numDataPoints;

  /* Copy 2D host memory to device memory*/
  CUDA_SAFE_CALL( cudaMemcpy2D(d_dataPoints, d_pitch_dataPoints, h_dataPoints, widthBytes_dataPoints, widthBytes_dataPoints, numDataPoints, cudaMemcpyHostToDevice) );
  CUDA_SAFE_CALL( cudaMemcpy2D(d_clusterCentres, d_pitch_clusterCentres, h_clusterCentres, widthBytes_clusterCentres, widthBytes_clusterCentres, numClusters, cudaMemcpyHostToDevice) );

  /* Copy 1D host memory to the device memory*/
  CUDA_SAFE_CALL( cudaMalloc(d_membership, h_membership, size_membership, cudaMemcpyHostToDevice) );
}
