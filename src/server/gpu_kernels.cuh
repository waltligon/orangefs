/* GPU kernel for distance computation */
void gpuComputeDistance(
			float* d_dataPoints,
			int numDataPoints,
			int numDimensions,
			float* d_clusterCentres,
			int numClusters,
			int* d_membership,
			float(*func_distance)(float*, float*, int)
			);

/* GPU Kernel for cluster Update */
void gpuUpdateClusterCentres(
			     int* d_membership,
			     float* d_dataPoints,
			     int numDataPoints,
			     int numDimensions,
			     int numClusters,
			     int* d_clusterHistogram,
			     float* d_newClusterCentres
			     );

/* GPU Kernel for Cluster Histogram */
void gpuCountClusterPoints(
			   int* d_membership,
			   int numDataPoints,
			   int numClusters,
			   int* d_newClusterHistogram
			   );

/* GPU Kernel to Compute Error */
void gpuComputeError(
		     int* d_newMembership,
		     int* d_oldMembership,
		     int numDataPoints,
		     float* error
		     );
