#include <stddef.h>
#include <stdint.h>

void cpuPd5( void const *restrict const pData,
			 size_t size,
			 uint8_t* const hash  )
             __attribute__((pure));
void cudaPd5( void const *restrict const pData,
			  size_t size,
			  uint8_t* const hash  )
              __attribute__((pure));