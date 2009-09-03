/*
  For the Time being just use a single dimensional array.
*/
#include <stdlib.h>
#include <stdio.h>

#include <cutil_inline.h>
#include "gpu_kernels.cuh"

#define MAX_ITERATIONS 400

/* Used by computeDist.c */
float distanceEuclidean(float* refPoint, float* testPoint, int numDimensions){

  float sum = 0.0;
  int dim;

  for(dim = 0; dim < numDimensions; dim++)
    sum += (refPoint[dim] - testPoint[dim]) * (refPoint[dim] - testPoint[dim]);

  return sqrt(sum);
}

/* Used by computeError.c */
int errorAbsDifference(int newVal, int oldVal){

  return abs(newVal - oldVal);
}

int errorCheckEquality(int newVal, int oldVal){
  return newVal == oldVal ? 0 : 1;
}

extern "C" void singleGPU(float* objects, int numDataPoints, 
	       int numDimensions, float* clusters,
	       int numClusters, int *membership,
			  int *newClusterSize,
	       float* newClusters, float *error)
{
  float* d_dataPoints;
  float* d_clusterCentres;
  int* d_membership;
  int* d_clusterHistogram;
  int* h_clusterHistogram = (int*)malloc(sizeof(int)*numClusters);
  int i;

  //int* h_clusterHistogram = (int*)malloc(sizeof(int)*numClusters);

  /* Initialize the GPU device */
  CUT_DEVICE_INIT(1, "pvfs2-server");
  
  /* Print out Kmeans parameters */
  fprintf(stderr, "[INFO] Records: %d, Dimensions: %d, Clusters: %d\n", numDataPoints, numDimensions, numClusters);

  for(i=0; i<numDimensions; i++)
    fprintf(stderr, "object[%d]=%f\n", i, objects[i]);

  for(i=0; i<numDimensions; i++)
    fprintf(stderr, "clusters[%d]=%f\n", i, clusters[i]);

  /* Allocate memory on the device and copy the data */
  fprintf(stderr, "Memory Initialization ... [START]\n");
  
  cudaMalloc((void**) &d_dataPoints, sizeof(float)*numDataPoints*numDimensions);
  cudaMalloc((void**) &d_clusterCentres, sizeof(float)*numClusters*numDimensions);
  cudaMalloc((void**) &d_membership, sizeof(int)*numDataPoints);
  cudaMalloc((void**) &d_clusterHistogram, sizeof(int)*numClusters);

  /* Memory Initialization */
  cudaMemcpy(d_dataPoints, objects, sizeof(float)*numDataPoints*numDimensions, cudaMemcpyHostToDevice);
  cudaMemcpy(d_clusterCentres, clusters, sizeof(float)*numClusters*numDimensions, cudaMemcpyHostToDevice);
  cudaMemcpy(d_membership, membership, sizeof(int)*numDataPoints, cudaMemcpyHostToDevice);

  fprintf(stderr, "Memory Initialization ... [DONE]\n");

  /* Compute the distance */
  gpuComputeDistance(d_dataPoints, numDataPoints, numDimensions, d_clusterCentres, numClusters, d_membership, distanceEuclidean);
  fprintf(stderr, "gpuComputeDistance ... [DONE]\n");
  cudaMemcpy(membership, d_membership, sizeof(int)*numDataPoints, cudaMemcpyDeviceToHost);
  for(i=0; i<22; i++)
    fprintf(stderr, "d_membership[%d]=%d\n", i, membership[i]);

  /* Compute the Histogram */
  gpuCountClusterPoints(d_membership, numDataPoints, numClusters, d_clusterHistogram);
  cudaMemcpy(h_clusterHistogram, d_clusterHistogram, sizeof(int)*numClusters, cudaMemcpyDeviceToHost);
  for(i=0; i<numClusters; i++)
    fprintf(stderr, "d_clusterHistogram[%d]=%d\n", i, h_clusterHistogram[i]);
  fprintf(stderr, "gpuCountClusterPoints ... [DONE]\n");

  /* Update the Cluster Centres, using the d_clusterHistogram */
  //[MODIFY] I am for the time being updating the update cluster to give out the average value rather than summation
  gpuUpdateClusterCentres(d_membership, d_dataPoints, numDataPoints, numDimensions, numClusters, d_clusterHistogram, d_clusterCentres);
  fprintf(stderr, "gpuUpdateClusterCentres ... [DONE]\n");

  /* Compute the Error */
  //gpuComputeError(d_membership, d_oldMembership, numDataPoints, error);
  //fprintf(stderr, "gpuComputeError ... [DONE]\n");

  /* Copy back the result to the host */
  cudaMemcpy(newClusters, d_clusterCentres, sizeof(float)*numClusters*numDimensions, cudaMemcpyDeviceToHost);
  cudaMemcpy(newClusterSize, d_clusterHistogram, sizeof(int)*numClusters, cudaMemcpyDeviceToHost);
  for(i=0; i<numDimensions; i++)
    fprintf(stderr, "newClusters[%d]=%f\n", i, newClusters[i]);
  cudaMemcpy(membership, d_membership, sizeof(int)*numDataPoints, cudaMemcpyDeviceToHost);

  /* Free the device memory */
  cudaFree(d_dataPoints);
  cudaFree(d_clusterCentres);
  cudaFree(d_membership);
  cudaFree(d_clusterHistogram);

  cudaThreadExit();

  //fprintf(stderr, "[RESULT] Total Execution time = %f ms\n", cutGetTimerValue(timer));

  /* End Program Execution */
}
