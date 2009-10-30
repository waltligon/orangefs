/*
 * Copyright 1993-2007 NVIDIA Corporation.  All rights reserved.
 *
 * NOTICE TO USER:
 *
 * This source code is subject to NVIDIA ownership rights under U.S. and
 * international Copyright laws.
 *
 * NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE
 * CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR
 * IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
 * IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL,
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
 * OR PERFORMANCE OF THIS SOURCE CODE.
 *
 * U.S. Government End Users.  This source code is a "commercial item" as
 * that term is defined at 48 C.F.R. 2.101 (OCT 1995), consisting  of
 * "commercial computer software" and "commercial computer software
 * documentation" as such terms are used in 48 C.F.R. 12.212 (SEPT 1995)
 * and is provided to the U.S. Government only as a commercial end item.
 * Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through
 * 227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the
 * source code with only those rights set forth herein.
 */

/*
  Parallel reduction

  This sample shows how to perform a reduction operation on an array of values
  to produce a single value.

  Reductions are a very common computation in parallel algorithms.  Any time
  an array of values needs to be reduced to a single value using a binary 
  associative operator, a reduction can be used.  Example applications include
  statistics computaions such as mean and standard deviation, and image 
  processing applications such as finding the total luminance of an
  image.

  This code performs sum reductions, but any associative operator such as
  min() or max() could also be used.

  It assumes the input size is a power of 2.

  COMMAND LINE ARGUMENTS

  "--shmoo":         Test performance for 1 to 32M elements with each of the 7 different kernels
  "--n=<N>":         Specify the number of elements to reduce (default 1048576)
  "--threads=<N>":   Specify the number of threads per block (default 128)
  "--kernel=<N>":    Specify which kernel to run (0-6, default 6)
  "--maxblocks=<N>": Specify the maximum number of thread blocks to launch (kernel 6 only, default 64)
  "--cpufinal":      Read back the per-block results and do final sum of block sums on CPU (default false)
  "--cputhresh=<N>": The threshold of number of blocks sums below which to perform a CPU final reduction (default 1)
    
*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// includes, project
#include <cutil_inline.h>

#include <reduction_kernel.cu>
int operation = 1;

////////////////////////////////////////////////////////////////////////////////
// declaration, forward
void runTest( int argc, char** argv);

extern "C"
float reduceGold(float *data, int size, int operation);

////////////////////////////////////////////////////////////////////////////////
// Wrapper function for kernel launch
////////////////////////////////////////////////////////////////////////////////
void reduce(int size, int threads, int blocks, int whichKernel, float *d_idata, float *d_odata, int value)
{
  dim3 dimBlock(threads, 1, 1);
  dim3 dimGrid(blocks, 1, 1);
  int smemSize = threads * sizeof(int);

  // choose which of the optimized versions of reduction to launch
  switch (whichKernel)
    {
    case 0:
      reduce0<<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata);
      break;
    case 1:
      reduce1<<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata);
      break;
    case 2:
      reduce2<<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata);
      break;
    case 3:
      reduce3<<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata);
      break;
    case 4:
      reduce4<<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata);
      break;
    case 5:
    default:
      switch (threads)
        {
        case 512:
	  reduce5<512><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case 256:
	  reduce5<256><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case 128:
	  reduce5<128><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case 64:
	  reduce5< 64><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case 32:
	  reduce5< 32><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case 16:
	  reduce5< 16><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case  8:
	  reduce5<  8><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case  4:
	  reduce5<  4><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case  2:
	  reduce5<  2><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        case  1:
	  reduce5<  1><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata); break;
        }
      break;       
    case 6:
      switch (threads)
        {
        case 512:
	  reduce6<512><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case 256:
	  reduce6<256><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case 128:
	  reduce6<128><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case 64:
	  reduce6< 64><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case 32:
	  reduce6< 32><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case 16:
	  reduce6< 16><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case  8:
	  reduce6<  8><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case  4:
	  reduce6<  4><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case  2:
	  reduce6<  2><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        case  1:
	  reduce6<  1><<< dimGrid, dimBlock, smemSize >>>(d_idata, d_odata, size, operation, value); break;
        }
      break;       
    }
}

////////////////////////////////////////////////////////////////////////////////
// Compute the number of threads and blocks to use for the given reduction kernel
// For the kernels >= 3, we set threads / block to the minimum of maxThreads and
// n/2. For kernels < 3, we set to the minimum of maxThreads and n.  For kernel 
// 6, we observe the maximum specified number of blocks, because each thread in 
// that kernel can process a variable number of elements.
////////////////////////////////////////////////////////////////////////////////
void getNumBlocksAndThreads(int whichKernel, int n, int maxBlocks, int maxThreads, int &blocks, int &threads)
{
  if (whichKernel < 3)
    {
      threads = (n < maxThreads) ? n : maxThreads;
      blocks = n / threads;
    }
  else
    {
      if (n == 1) 
	threads = 1;
      else
	threads = (n < maxThreads*2) ? n / 2 : maxThreads;
      blocks = n / (threads * 2);

      if (whichKernel == 6)
	blocks = min(maxBlocks, blocks);
    }
}

////////////////////////////////////////////////////////////////////////////////
// This function performs a reduction of the input data multiple times and 
// measures the average reduction time.
////////////////////////////////////////////////////////////////////////////////
float benchmarkReduce(int  n, 
		      int  numThreads,
		      int  numBlocks,
		      int  maxThreads,
		      int  maxBlocks,
		      int  whichKernel, 
		      int  testIterations,
		      bool cpuFinalReduction,
		      int  cpuFinalThreshold,
		      unsigned int timer,
		      float* h_odata,
		      float* d_idata, 
		      float* d_odata)
{
  float gpu_result = 0.0f;
  bool needReadBack = true;
  float value=0.0f;

  for (int i = 0; i < testIterations; ++i)
    {
      gpu_result = 0.0f;

      cudaThreadSynchronize();
      //CUT_SAFE_CALL( cutStartTimer( timer));

      // execute the kernel
      reduce(n, numThreads, numBlocks, whichKernel, d_idata, d_odata, value);

      // check if kernel execution generated an error
      CUT_CHECK_ERROR("Kernel execution failed");

      if (cpuFinalReduction)
        {
	  // sum partial sums from each block on CPU        
	  // copy result from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );

	  for(int i=0; i<numBlocks; i++) 
            {
	      gpu_result += h_odata[i];
            }

	  needReadBack = false;
        }
      else
        {
	  // sum partial block sums on GPU
	  int s=numBlocks;
	  int kernel = (whichKernel == 6) ? 5 : whichKernel;
	  while(s > cpuFinalThreshold) 
            {
	      int threads = 0, blocks = 0;
	      getNumBlocksAndThreads(kernel, s, maxBlocks, maxThreads, blocks, threads);
	      reduce(s, threads, blocks, kernel, d_odata, d_odata, value);
	      if (kernel < 3)
		s = s / threads;
	      else
		s = s / (threads*2);
            }
            
	  if (s > 1)
            {
	      // copy result from device to host
	      CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, s * sizeof(float), cudaMemcpyDeviceToHost) );

	      for(int i=0; i < s; i++) 
                {
		  gpu_result += h_odata[i];
                }

	      needReadBack = false;
            }
        }

      cudaThreadSynchronize();
      CUT_SAFE_CALL( cutStopTimer(timer) );      
    }

  if (needReadBack)
    {
      // copy final sum from device to host
      CUDA_SAFE_CALL( cudaMemcpy( &gpu_result, d_odata, sizeof(float), cudaMemcpyDeviceToHost) );
    }

  return gpu_result;
}

#if 0
////////////////////////////////////////////////////////////////////////////////
// This function calls benchmarkReduce multple times for a range of array sizes
// and prints a report in CSV (comma-separated value) format that can be used for
// generating a "shmoo" plot showing the performance for each kernel variation
// over a wide range of input sizes.
////////////////////////////////////////////////////////////////////////////////
void shmoo(int minN, int maxN, int maxThreads, int maxBlocks)
{ 
  // create random input data on CPU
  unsigned int bytes = maxN * sizeof(int);
  float value=0.0f;

  float *h_idata = (float *) malloc(bytes);

  for(int i = 0; i < maxN; i++) {
    h_idata[i] = rand() & 0xff;
  }

  int maxNumBlocks = maxN / maxThreads;

  // allocate mem for the result on host side
  float* h_odata = (float*) malloc(maxNumBlocks*sizeof(float));

  // allocate device memory and data
  float* d_idata = NULL;
  float* d_odata = NULL;

  CUDA_SAFE_CALL( cudaMalloc((void**) &d_idata, bytes) );
  CUDA_SAFE_CALL( cudaMalloc((void**) &d_odata, maxNumBlocks*sizeof(float)) );

  // copy data directly to device memory
  CUDA_SAFE_CALL( cudaMemcpy(d_idata, h_idata, bytes, cudaMemcpyHostToDevice) );
  CUDA_SAFE_CALL( cudaMemcpy(d_odata, h_idata, maxNumBlocks*sizeof(float), cudaMemcpyHostToDevice) );

  // warm-up
#ifndef __DEVICE_EMULATION__
  for (int kernel = 0; kernel < 7; kernel++)
    {
      reduce(maxN, maxThreads, maxNumBlocks, kernel, d_idata, d_odata, value);
    }
  int testIterations = 100;
#else
  int testIterations = 1;
#endif

  unsigned int timer = 0;
  //CUT_SAFE_CALL( cutCreateTimer( &timer));
    
  // print headers
  printf("Time in milliseconds for various numbers of elements for each kernel\n");
  printf("\n\n");
  printf("Kernel");
  for (int i = minN; i <= maxN; i *= 2)
    {
      printf(", %d", i);
    }
   
  for (int kernel = 0; kernel < 7; kernel++)
    {
      printf("\n");
      printf("%d", kernel);
      for (int i = minN; i <= maxN; i *= 2)
        {
	  //cutResetTimer(timer);
	  int numBlocks = 0;
	  int numThreads = 0;
	  getNumBlocksAndThreads(kernel, i, maxBlocks, maxThreads, numBlocks, numThreads);
            
            
	  benchmarkReduce(i, numThreads, numBlocks, maxThreads, maxBlocks, kernel, 
			  testIterations, false, 1, timer, h_odata, d_idata, d_odata);

	  float reduceTime = cutGetAverageTimerValue(timer);
	  printf(", %f", reduceTime);
        }
        
    }

  // cleanup
  CUT_SAFE_CALL(cutDeleteTimer(timer));
  free(h_idata);
  free(h_odata);

  CUDA_SAFE_CALL(cudaFree(d_idata));
  CUDA_SAFE_CALL(cudaFree(d_odata));    
}
#endif

////////////////////////////////////////////////////////////////////////////////
// The main function which runs the reduction test.
////////////////////////////////////////////////////////////////////////////////
void
//reductionGPU( int argc, char** argv) 
reductionGPU(int size, int operation)
{
  CUT_DEVICE_INIT(1, "pvfs2-server");

  //int size = 1<<20;    // number of elements to reduce
  int maxThreads = 128;  // number of threads per block
  int whichKernel = 6;
  int maxBlocks = 64;
  bool cpuFinalReduction = false;
  int cpuFinalThreshold = 1;
  float value=0.0f;

  //cutGetCmdLineArgumenti( argc, (const char**) argv, "operation", &operation);
  //cutGetCmdLineArgumenti( argc, (const char**) argv, "n", &size);
  //cutGetCmdLineArgumenti( argc, (const char**) argv, "threads", &maxThreads);
  //cutGetCmdLineArgumenti( argc, (const char**) argv, "kernel", &whichKernel);
  //cutGetCmdLineArgumenti( argc, (const char**) argv, "maxblocks", &maxBlocks);
  printf("%d elements\n", size);
  printf("%d threads (max)\n", maxThreads);

  //cpuFinalReduction = cutCheckCmdLineFlag( argc, (const char**) argv, "cpufinal");
  //cutGetCmdLineArgumenti( argc, (const char**) argv, "cputhresh", &cpuFinalThreshold);

  //bool runShmoo = cutCheckCmdLineFlag(argc, (const char**) argv, "shmoo");
  bool runShmoo;

  if (runShmoo)
    {
      //shmoo(1, 33554432, maxThreads, maxBlocks);
    }
  else
    {
      // create random input data on CPU
      unsigned int bytes = size * sizeof(float);

      float *h_idata = (float *) malloc(bytes);

      for(int i=0; i<size; i++) {
	h_idata[i] = size-i;//i+1;//rand() & 1;
      }

      int numBlocks = 0;
      int numThreads = 0;
      getNumBlocksAndThreads(whichKernel, size, maxBlocks, maxThreads, numBlocks, numThreads);
      if (numBlocks == 1) cpuFinalThreshold = 1;

      // allocate mem for the result on host side
      float* h_odata = (float*) malloc(numBlocks*sizeof(float));

      printf("%d blocks\n", numBlocks);

      // allocate device memory and data
      float* d_idata = NULL;
      float* d_odata = NULL;

      CUDA_SAFE_CALL( cudaMalloc((void**) &d_idata, bytes) );
      CUDA_SAFE_CALL( cudaMalloc((void**) &d_odata, numBlocks*sizeof(float)) );

      // copy data directly to device memory
      CUDA_SAFE_CALL( cudaMemcpy(d_idata, h_idata, bytes, cudaMemcpyHostToDevice) );
      CUDA_SAFE_CALL( cudaMemcpy(d_odata, h_idata, numBlocks*sizeof(float), cudaMemcpyHostToDevice) );

      // warm-up
#ifndef __DEVICE_EMULATION__
      reduce(size, numThreads, numBlocks, whichKernel, d_idata, d_odata, value);
      int testIterations = 100;
#else
      int testIterations = 1;
#endif

      unsigned int timer = 0;
      CUT_SAFE_CALL( cutCreateTimer( &timer));
      unsigned int timerCPU = 0;
      CUT_SAFE_CALL( cutCreateTimer( &timerCPU));
        
      float gpu_result = 0.0f;

      cudaThreadSynchronize();
      CUT_SAFE_CALL( cutStartTimer( timer));
      switch (operation)
	{
	case 4:
	  operation=1;
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, value);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  for(int i=0; i<numBlocks; i++) 
	    {
	      gpu_result += h_odata[i];
	    }
	  gpu_result/=size;
	  operation=4;
	  break;
	case 5:	
	  operation=1;
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, value);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  for(int i=0; i<numBlocks; i++) 
	    {
	      gpu_result += h_odata[i];
	    }
	  //averagi hesapla
	  gpu_result/=size;
	  operation=5;
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, gpu_result);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  gpu_result=0;
	  for(int i=0; i<numBlocks; i++) 
	    {
	      gpu_result += h_odata[i];
	    }			
	  gpu_result/=size;
	  break;
	case 6:
	  operation=1;
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, value);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  for(int i=0; i<numBlocks; i++)
	    {
	      gpu_result += h_odata[i];
	    }
	  //averagi hesapla
	  gpu_result/=size;
	  operation=5;
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, gpu_result);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  gpu_result=0;
	  for(int i=0; i<numBlocks; i++)
	    {
	      gpu_result += h_odata[i];
	    }
	  gpu_result/=size;
	  gpu_result=sqrt(gpu_result);
	  break;
	case 1: 
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, value);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  for(int i=0; i<numBlocks; i++) 
	    {
	      // printf("%d h_odatap[%d]\n",h_odata[i],i);
	      gpu_result += h_odata[i];
	    }
	  break;
	case 2: 
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, value);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  gpu_result=h_odata[0];
	  //printf("%d h_odatap[0]\n",gpu_result);
	  for(int i=1; i<numBlocks; i++) 
	    {
	      // printf("%d h_odatap[%d]\n",h_odata[i],i);
	      if (gpu_result > h_odata[i]) gpu_result = h_odata[i];
	    }
	  break;
	case 3: 
	  reduce(size, numThreads, numBlocks, 6,d_idata,d_odata, value);
	  // copy final sum from device to host
	  CUDA_SAFE_CALL( cudaMemcpy( h_odata, d_odata, numBlocks*sizeof(float), cudaMemcpyDeviceToHost) );
	  gpu_result=h_odata[0];
	  //printf("%d h_odatap[0]\n",gpu_result);
	  for(int i=1; i<numBlocks; i++) 
	    {
	      // 				printf("%d h_odatap[%d]\n",h_odata[i],i);
	      if (gpu_result < h_odata[i]) gpu_result = h_odata[i];
	    }
	  break;
	}
      cudaThreadSynchronize();
      //CUT_SAFE_CALL( cutStopTimer(timer) );

      //float reduceTime = cutGetAverageTimerValue(timer);
      //printf("Average time: %f ms\n", reduceTime);
      //printf("Bandwidth:    %f GB/s\n\n", (size * sizeof(float)) / (reduceTime * 1.0e6));

      //printf("GPU result = %f\n", gpu_result);


      //CUT_SAFE_CALL( cutStartTimer( timerCPU));
      // compute reference solution
      //long cpu_result = reduceGold(h_idata, size,operation);
      //printf("CPU result = %f\n", cpu_result);
      //CUT_SAFE_CALL( cutStopTimer(timerCPU) );
      //float reduceTimeCPU = cutGetAverageTimerValue(timerCPU);
      //printf("Average time: %f ms\n", reduceTimeCPU);

      //printf("TEST %s\n", (gpu_result == cpu_result) ? "PASSED" : "FAILED");

      // cleanup
      CUT_SAFE_CALL( cutDeleteTimer(timer) );
      free(h_idata);
      free(h_odata);

      CUDA_SAFE_CALL(cudaFree(d_idata));
      CUDA_SAFE_CALL(cudaFree(d_odata));
    }
}
