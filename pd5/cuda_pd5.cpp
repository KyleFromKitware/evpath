/*
 * cuda_pd5.cpp
 *
 *  Contains code for CUDA and CPU versions of PD5.
 *
 *  Good portion of optimization code used with permission of Mario Juric: http://majuric.org/
 *
 */



#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include <iostream>
#include <vector>
#include <valarray>

#include "cuda_pd5.hpp"

#include <cuda_runtime_api.h>


using namespace std;



///////////////////////////////////////////////////////////
// CUDA helpers

//
// Find the dimensions (bx,by) of a 2D grid of blocks that 
// has as close to nblocks blocks as possible
//
void find_best_factorization(int &bx, int &by, int nblocks)
{
	bx = -1;
	int best_r = 100000;
	for(int bytmp = 1; bytmp != 65536; bytmp++)
	{
		int r  = nblocks % bytmp;
		if(r < best_r && nblocks / bytmp < 65535)
		{
			by = bytmp;
			bx = nblocks / bytmp;
			best_r = r;
			
			if(r == 0) { break; }
			bx++;
		}
	}
	if(bx == -1) { std::cerr << "Unfactorizable?!\n"; exit(-1); }
}

//
// Given a total number of threads, their memory requirements, and the
// number of threadsPerBlock, compute the optimal allowable grid dimensions.
// Returns false if the requested number of threads are impossible to fit to
// shared memory.
//
bool calculate_grid_parameters(int gridDim[3], int threadsPerBlock, int neededthreads, int dynShmemPerThread, int staticShmemPerBlock)
{
	const int shmemPerMP =  16384;

	int dyn_shared_mem_required = dynShmemPerThread*threadsPerBlock;
	int shared_mem_required = staticShmemPerBlock + dyn_shared_mem_required;
	if(shared_mem_required > shmemPerMP) { return false; }

	// calculate the total number of threads
	int nthreads = neededthreads;
	int over = neededthreads % threadsPerBlock;
	if(over) { nthreads += threadsPerBlock - over; } // round up to multiple of threadsPerBlock

	// calculate the number of blocks
	int nblocks = nthreads / threadsPerBlock;
	if(nthreads % threadsPerBlock) { nblocks++; }

	// calculate block dimensions so that there are as close to nblocks blocks as possible
	find_best_factorization(gridDim[0], gridDim[1], nblocks);
	gridDim[2] = 1;

	return true;
}

//
///////////////////////////////////////////////////////////


/*
 * Do the actual GPU calculation, if benchmark = true, run block calculations
 *
 */
md5hash cuda_compute_pd5( void** pBlocks, int lNum, int lNumOrig, int lThreadOffset, bool benchmark=false)
{
	int i = 0;
	std::vector<md5hash> vHashes;

	md5hash RetHash;

	init_constants();

	unsigned int *gpuWords, *gpuHash = NULL;

	/* 64 bytes per block */
	cudaMalloc((void **)&gpuWords, lNum * 64 );
	

	/* Copy memory over, 64 bytes per block (16 4 byte words), last 2 blocks may not be not contiguous */
	cudaMemcpy( gpuWords, pBlocks[0], lNumOrig*64, cudaMemcpyHostToDevice );

	for( i = 0; i < lNum - lNumOrig; i++ )
	{
		cudaMemcpy( gpuWords + ((i + lNumOrig)*16), pBlocks[i+1], 64, cudaMemcpyHostToDevice );
		//printf("%i, %i\n", (i + lNumOrig)*16, lNumOrig );
	}


    // allocate GPU memory for computed hashes
    cudaMalloc((void **)&gpuHash, 4*sizeof(unsigned int)*lNum );


	//
	// The icky part: compute the optimal number of threads per block,
	// and the number of blocks
	//
	int dynShmemPerThread = 64;	// built in the algorithm
	int staticShmemPerBlock = 32;	// read from .cubin file

	double bestTime = 1e10, bestRate = 0.;
	int bestThreadsPerBlock;
	int nthreads = lNum;

	int tpb = 193;   /* Empirically determined optimal threads per block */

	if( benchmark == true )
	{
	    tpb = 1; /* iteratively search threads per block values */
	}


	do
	{
		int gridDim[3];
		if(!calculate_grid_parameters(gridDim, tpb, nthreads, dynShmemPerThread, staticShmemPerBlock))
		{
		    printf("Warning, file does not fit\n");
		    continue;
		}

		double gpuTime;

		gpuTime = execute_kernel(gridDim[0], gridDim[1], tpb, tpb*dynShmemPerThread, lNum, lThreadOffset, gpuWords, gpuHash );

		double rate = lNum / gpuTime;

		if(bestRate < rate)
		{
			bestTime = gpuTime;
			bestRate = rate;
			bestThreadsPerBlock = tpb;
		}

		if(benchmark)
		{

		    std::cout << "words=" << lNum
				<< " tpb=" << tpb
				<< " nthreads=" << gridDim[0]*gridDim[1]*tpb << " nblocks=" << gridDim[0]*gridDim[1]
				<< " gridDim[0]=" << gridDim[0] << " gridDim[1]=" << gridDim[1]
				<< " padding=" << gridDim[0]*gridDim[1]*tpb - lNum
				<< " dynshmem=" << dynShmemPerThread*tpb
				<< " shmem=" << staticShmemPerBlock + dynShmemPerThread*tpb
				<< " gpuTime=" << gpuTime
				<< " rate=" << (int)(rate)
				<< std::endl;

		   // printf("%i, %.02lf\n", tpb, gpuTime );
		}

	} while(benchmark && tpb++ <= 512);

	if(benchmark)
	{
		std::cerr << "\nBest case: threadsPerBlock=" << bestThreadsPerBlock << "\n";
	}

	//std::cerr << "GPU MD5 time : " <<  bestTime << " ms (" << std::fixed << 1000. * lNum / bestTime << " blocks/second)\n";


	/* If lNum > lUseCPU, we will have recombined the numbers down to that automatically */
	if( lNum > USE_CPU )
	    lNum = USE_CPU;

    // Download the computed hashes
	vHashes.resize(lNum);
	cudaMemcpy( &vHashes.front(), gpuHash, sizeof(unsigned int)*4*lNum, cudaMemcpyDeviceToHost);

	/* Add up all of the remaining hashes */
	for( int i = 0; i < lNum-1; i++ )
	{

	    vHashes[0].ui[0] += vHashes[i+1].ui[0];
        vHashes[0].ui[1] += vHashes[i+1].ui[1];
        vHashes[0].ui[2] += vHashes[i+1].ui[2];
        vHashes[0].ui[3] += vHashes[i+1].ui[3];
	}


	// Shutdown
	cudaFree(gpuWords);
	cudaFree(gpuHash);

	return vHashes[0];
}

/* Generate separate 512 bit block pointers from a single chunk of data 
 * Requires very specific valid memory passed in 
 *
 * pBlocks - pointer to at least ullLen / 64 + 2 valid pointer array
 * pData - pointer to data
 * ullLen - length of data in bytes
 * pPadBlocks - pointer to 128 byte array
 */
unsigned int generateBlocks( void** pBlocks, unsigned char* pData, unsigned long long ullLen, unsigned char* pPadBlocks )
{
	unsigned int ulBlockNum = 0;
	unsigned long long ullBytesLeft = ullLen;

	int i = 0;
	
	if( NULL == pBlocks || NULL == pData || NULL == pPadBlocks )
	{
		printf( "Null pointer\n" );
		return -1;
	}

	pBlocks[0] = pData;
	pBlocks[1] = pPadBlocks;
	pBlocks[2] = pPadBlocks+64;

	ulBlockNum = (unsigned int)(ullLen / 64);
	ullBytesLeft = ullLen % ulBlockNum;


	/* Now we have a partial block, copy data to two padding blocks (could be 0) */
	memcpy( pPadBlocks, pData + (ulBlockNum*64), ullBytesLeft );

	/* add a 1 bit to the end of the message */
	pPadBlocks[ullBytesLeft] = 0x80;

	ulBlockNum++;

	int lOffset = 0;
	if( ullBytesLeft <= 55 )
	{
		/* Only one padding block will do, metadata fits on end of last < 64 byte block */
	}
	else
	{
		/* We need two blocks for padding, one for last bit of data and one for metadata since there was not enough room */
		lOffset = 64;
		ulBlockNum++;
	}


	/* fill out bytes with 0 values, 64bit size starts at byte 56(or 56+64) */
	memset( pPadBlocks + ullBytesLeft + 1, 0, (56 + lOffset) - (ullBytesLeft + 1 ) );

	/* Add 64 bit bitlength onto the end */
	*(unsigned long long*) (pPadBlocks + 56 + lOffset)  = (ullLen * 8);

	
	return ulBlockNum;
}

/*
 * Calculate md5sum
 *
 * pData -> Pointer to data to hash
 * ulSize -> Size of the data in bytes
 *
 */
extern "C" void cpuPd5( void* pData, size_t ulSize, uint8_t* hash )
{
    char cIn;
    int lRead;
    int i;
    unsigned int a,b,c,d;
    unsigned int reta, retb, retc, retd;

    /* Memory for data pointers */
    void* pBlocks[3];

    if( NULL == pData )
    {
        printf( "Null Pointer %s\n", __func__ );
        return;
    }


    /* Allocate memory for last two of padding, otherwise use other memory directly */
    unsigned char pPadBlocks[128];

    int numblocks = generateBlocks( pBlocks, (unsigned char*)pData, ulSize, pPadBlocks );

    //printf( "%i blocks\n\n", numblocks );

    /* Run the pd5 algorithm on all contiguous blocks */
    for(i = 0; i < ulSize / 64; i++ )
    {
    	pd5_cpu_v2( ((unsigned int*)pBlocks[0])+(i*16), a, b, c, d, i);

        if( i == 0 )
        {
			reta = a;
			retb = b;
			retc = c;
			retd = d;
        }
        else
        {
			reta += a;
			retb += b;
			retc += c;
			retd += d;
        }

        //printf( "%08X  %08X  %08X  %08X\n", reta, retb, retc, retd );
    }

    /* Finish off last "padded" blocks */
    for(int j = 1; i < numblocks; i++,j++ )
    {
    	pd5_cpu_v2( (unsigned int*)pBlocks[j], a, b, c, d, i);

        reta += a;
        retb += b;
        retc += c;
        retd += d;

       // printf( "%08X  %08X  %08X  %08X\n", reta, retb, retc, retd );
    }

	md5hash tmp = {0};
    md5hash.ui[0] = reta;
    md5hash.ui[1] = retb;
    md5hash.ui[2] = retc;
    md5hash.ui[3] = retd;

	for(int i=0; i<16; i++)
		hash[i] = md5hash.ch[i];
	return;
}


/*
 * Calculate md5sum
 *
 * pData -> Pointer to data to hash
 * ulSize -> Size of the data in bytes
 *
 */
extern "C" void cudaPd5( void* pData, size_t ulSize, uint8_t* hash  )
{
	md5hash Result = {0};

	for(int i=0; i<16; i++)
		hash[i] = 0;

	char cIn;
	int lRead;
	int i;

	/* Memory for data pointers */
    void* pBlocks[3];

    if( NULL == pData || ulSize == 0 )
		return;


	/* Allocate memory for last two of padding, otherwise use other memory directly */
	unsigned char pPadBlocks[128];

	int numblocks = generateBlocks( pBlocks, (unsigned char*)pData, ulSize, pPadBlocks );

	//printf( "%i blocks\n\n", numblocks );


	/* Number of full 64 byte blocks */
	int lFullBlocks = ulSize / 64;
	int lThreadOffset = 0;
	/* Run in sections of 8388608 (2^23) blocks (512 megs of 64 byte blocks) */
	/* This could be scaled for larger GPU's, but this is safe for a GPU with 1G */
	while( 1 )
	{
	    if( lFullBlocks > 8388608 )
	    {
	        Result = cuda_compute_pd5( pBlocks, 8388608, 8388608, lThreadOffset );

			for(int i=0; i<16; i++)
				hash[i] += Result.ch[i];

	        ((unsigned int**) pBlocks)[0] += 16*8388608;

	        numblocks   -= 8388608;
	        lFullBlocks -= 8388608;
            lThreadOffset += 8388608;
	    }
	    else
	    {
	        Result = cuda_compute_pd5( pBlocks, numblocks, lFullBlocks, lThreadOffset );
            
			for(int i=0; i<16; i++)
				hash[i] += Result.ch[i];

	        break;
	    }
	}

    return;
}
