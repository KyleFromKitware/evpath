#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdarg.h>

typedef void (*free_data_cb)(void* data, va_list extra_args);

typedef struct ring_buffer_node {
	// ptr to id can change, but not id itself
	uint8_t const *id;
	// same as above
	void const *data;
	// Max size is actually one less than SIZE_MAX, which is used as sentinel value
	size_t data_len;
	va_list free_data_extra_args;
	uint_fast32_t age;
	uint_fast8_t refcnt;
} ring_buffer_node;

typedef struct {
	// We don't anticipate much contention, so it's a global lock
	pthread_rwlock_t lock;
	size_t size;
	bool known_full;
	size_t id_len;
	// Can be NULL
	free_data_cb fd_cb;
	// Yes, the entirety of buf is flat here
	ring_buffer_node buf[];
} ring_buffer;

ring_buffer *restrict ring_buffer_create(size_t size,
										 size_t id_len,
										 free_data_cb fd_cb)
                                         __attribute__((pure));
//TODO: Unsure whether we could use const here...
void ring_buffer_free(ring_buffer *resrict ring);

// Returns true if we overwrote something
bool ring_buffer_insert(ring_buffer *restrict const ring,
						uint8_t const *restrict const id,
						void const *restrict const data,
						size_t data_len, ...)
						__attribute__((nonnull (1,2)));
// Returns true if we found something to remove
bool ring_buffer_remove(ring_buffer *restrict const ring,
						uint8_t const *restrict const id)
						__attribute__((nonnull));

// Returns SIZE_MAX if not found
size_t ring_buffer_find(ring_buffer const *restrict const ring,
						uint8_t const *restrict const id)
						__attribute__((nonnull, pure));

#endif