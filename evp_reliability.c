#include "evp_reliability.h"

#include <stdlib.h>
#include <assert.h>

#include "atl.h"
#include "debug.h"

/* Hash headers */
#include "pd5hashing.h"

atom_t hash_value_atom = attr_atom_from_string("hash_value");
atom_t hash_type_atom = attr_atom_from_string("hash_type");

attr_value_type hash_value_type = Attr_String,
                hash_func_type = Attr_Int4;

static void init_hash_func(hash_func_id id, hash_func func, int hash_size) {
	assert(id > 0 && id < NUM_HASH_FUNCS);
	assert(func != NULL);
	assert(hash_size > 0);
	
	known_hash_funcs[id] = func;
	known_hash_sizes[id] = hash_size;
}

void init_hash_functions(void) {
	for(int i=0; i<NUM_HASH_FUNCS; i++) {
		known_hash_funcs[i] = NULL;
	}
#ifdef WANT_CUDA
#warning "Using CUDA; if it does not work on this machine, set NO_CUDA"
	if(!getenv("NO_CUDA")) {
		init_hash_func(PD5_CUDA, cudaPd5, 16));
	}
#endif
	init_hash_func(PD5_CPU, cpuPd5, 16);
}

hash_result create_event_hash(void const *restrict const encoded_data,
							  size_t data_length,
							  attr_list attrs,
							  char **base64_hash)
                              __attribute__((nonnull(1), pure)) {
	assert(encoded_data != NULl);
	assert(data_length > 0);
	
	hash_func_id hasher_id = get_hash_func_id(attrs);
	if(hasher_id == -1) {
		INFO("No need to hash event\n");
		return RESULT_IGNORE;
	}
	if(hasher_id < 0 || hasher_id >= NUM_HASH_FUNCS) {
		INFO("Invalid hasher_id provided\n");
		return RESULT_UNABLE;
	}
	INFO("Hashing event for remote stone using hash algorithm %d\n", hasher_id);
	hash_func hasher = known_hash_funcs[hasher_id];
	if(hasher == NULL) {
		INFO("Requested hashing function not available\n");
		return RESULT_UNABLE;
	}
	uint8_t hash[known_hash_sizes[hasher_id]];
	hasher(encoded_data, data_length, &hash);
	if(!is_valid_hash(hash, known_hash_sizes[hasher_id])) {
		WARN("Hash failed\n");
		return RESULT_UNABLE;
	}
end:
	//TODO Does setting the base64_hash this way work?
	char *tmp_hash = add_hash_value(attrs, hash, hasher_id);
	if(base64_hash != NULL)
		*base64_hash = tmp_hash;
	else free(tmp_hash);
	return RESULT_OK;
}

/*
 * 1 if check went fine
 * 0 if no need to check
 * -1 if unable to check
 * -2 if check failed
 */
hash_result check_event_hash(void const *restrict const encoded_data,
							 size_t data_length,
							 attr_list attrs,
							 char **base64_hash)
                             __attribute__((nonnull(1), pure)) {
	hash_result status;
	uint8_t *original_hash = NULL;
	hash_func_id hasher_id = get_hash_func_id(attrs);
	if(hasher_id == -1) {
		INFO("No need to hash event\n");
		status = RESULT_IGNORE;
		goto cleanup;
	}
	if(hasher_id < 0 || hasher_id >= NUM_HASH_FUNCS) {
		INFO("Invalid hasher_id provided\n");
		status = RESULT_UNABLE;
		goto cleanup;
	}
	
	int hash_size = known_hash_sizes[hasher_id];
	original_hash = get_hash_value(attrs, hasher_id, base64_hash);
	if(original_hash == NULL) {
		INFO("No encoded hash value, so no need to hash\n");
		status = RESULT_IGNORE;
		goto cleanup;
	}
	hash_func hasher = known_hash_funcs[hasher_id];
	if(hasher == NULL) {
		INFO("Requested hashing function not available\n");
		status = RESULT_UNABLE;
	}
	uint8_t hash[hash_size];
	hasher(encoded_data, data_length, &hash);
	if(!is_valid_hash(hash, hash_size)) {
		WARN("Hash failed\n");
		status = RESULT_UNABLE;
		goto cleanup;
	}
	for(int i=0; i<hash_size; i++) {
		if(hash[i] != original_hash[i]) {
			status = RESULT_FAIL;
			goto cleanup;
		}
	}
	status = RESULT_OK;
cleanup:
	if(original_hash != NULL)
		free(original_hash);
	return status;
}

char* add_hash_value(attr_list attrs, uint8_t *hash, hash_func_id func_id) {
	assert(hash != NULL);
	assert(attrs != NULL);
	char *base64_hash = atl_base64_encode(hash, known_hash_sizes[func_id]);
	add_string_attr(attrs, hash_value_atom, base64_hash);
	return base64_hash;
}

void add_hash_func_id(attr_list attrs, hash_func_id func_id) {
	assert(attrs != NULL);
	add_attr(attrs, hash_func_atom, hash_func_type, hash_func_id);
}

/*
 * Hash is invalid if it's all 0's
 */
bool is_valid_hash(uint8_t *hash, int size) {
	assert(hash != NULL);
	assert(size > 0);
	for(int i=0; i<size; i++)
		if(hash[i] != 0)
			return true;
	return false;
}

uint8_t* get_hash_value(attr_list attrs, hash_func_id func_id, char **base64_hash) {
	uint8_t *hash_value = NULL;
	int expected_size = known_hash_sizes[func_id];
	// Heuristic static size from base64_encode in attr.c
	char *encoded_hash = (char*)malloc(expected_size*4/3+4);
	assert(encoded_hash != NULL);
	if(get_string_attr(attrs, hash_value_atom, &encoded_hash)) {
		hash_value = (uint8_t*)malloc(expected_size);
		assert(hash_value != NULL);
		int decoded_len = atl_base64_decode(encoded_hash, hash_value);
		if(decoded_len != expected_size) {
			WARN("Decoded hash size was not what was expected (%d vs. %d); "
			     "treating as invalid; this has also caused a buffer overrun\n",
		         decoded_len, expected_size);
			free(hash_value);
			free(encoded_hash);
			return NULL;
		}
	}
	// Does the caller want the base64 hash, and if so did we even decode it?
	if(base64_hash != NULL && hash_value != NULL)
		*base64_hash = encoded_hash;
	else free(base64_hash);
	return hash_value;
}

hash_func_id get_hash_func_id(attr_list attrs) {
	hash_func_id id;
	if(get_int_attr(attrs, hash_func_atom, &id))
		return id;
	else return -1;
}

hash_func get_hash_func(attr_list attrs) {
	hash_func_id id = get_hash_func_id(attrs);
	if(id > 0 && id < NUM_HASH_FUNCS)
		return known_hash_funcs[id];
	else return NULL;
}