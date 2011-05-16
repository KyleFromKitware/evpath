#ifndef EVP_RELIABILITY_H
#define EVP_RELIABILITY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "atl.h"

atom_t hash_value_atom,
       hash_func_atom;

attr_value_type hash_value_type,
                hash_func_type;

typedef enum {
	PD5_CUDA,
	PD5_CPU,
	NUM_HASH_FUNCS
} hash_func_id;

typedef void (*hash_func)(void const* const data,
						  size_t size,
						  uint8_t* const hash)
                          __attribute__((pure));

hash_func known_hash_funcs[NUM_HASH_FUNCS];
int known_hash_sizes[NUM_HASH_FUNCS];

void init_hash_functions(void);

typedef enum {
	RESULT_OK,
	RESULT_IGNORE,
	RESULT_UNABLE,
	RESULT_FAIL
} hash_result;

/*
 * base64_hash is in case you want to use it (can just be NULL)
 */
hash_result create_event_hash(void const *restrict const encoded_data,
							  size_t data_length,
							  attr_list attrs,
							  char **base64_hash)
                              __attribute__((nonnull(1), pure));
/*
 * base64_hash is in case you want to use it (can just be NULL)
 */
hash_result check_event_hash(void const *restrict const encoded_data,
							 size_t data_length,
							 attr_list attrs,
							 char **base64_hash)
                             __attribute__((nonnull(1), pure));

char* add_hash_value(attr_list attrs,
					 const uint8_t *const hash,
					 hash_func_id func_id);
void add_hash_func_id(attr_list attrs, hash_func_id func_id);

bool is_valid_hash(uint8_t *hash, int size);
uint8_t* get_hash_value(attr_list attrs, hash_func_id func_id, char **base64_hash) __attribute__((pure));
hash_func_id get_hash_func_id(attr_list attrs);
hash_func get_hash_func(attr_list attrs);

#endif