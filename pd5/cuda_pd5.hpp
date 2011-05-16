/*
 * cuda_pd5.hpp
 *
 *  Created on: Apr 1, 2011
 *      Author: John Cornwell
 *
 *      Contains cuda pd5 (parallelizable function similar to md5) functions.
 *
 */

#ifndef CUDA_MD5_HPP_
#define CUDA_MD5_HPP_

/* Empirically determined optimal value to switch CPU for adding */
#define USE_CPU 0x2000

/* Union to represent a hash, be careful of little endian issues */
union md5hash
{
    unsigned int ui[4];
    unsigned char ch[16];
};


extern "C" void cpuPd5( void* pData, unsigned int ulSize, unsigned int* pulRetHash  );
extern "C" void cudaPd5( void* pData, unsigned int ulSize, unsigned int* pulRetHash  );


double execute_kernel(int blocks_x, int blocks_y, int threads_per_block, int shared_mem_required, int realthreads, int lThreadOffset, unsigned int *gpuWords, unsigned int *gpuHash );
void init_constants();

void pd5_cpu_v2(const unsigned int *in, unsigned int &a, unsigned int &b, unsigned int &c, unsigned int &d, unsigned int lThread );
int deviceQuery();

#endif /* CUDA_MD5_HPP_ */
