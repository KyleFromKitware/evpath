#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdarg.h>

typedef void (*free_data_cb)(void* data, va_list extra_args);

typedef struct {
	// ptr to id can change, but not id itself
	uint8_t const *id;
	uint_fast32_t age;
	uint_fast8_t refcnt;
	// Of length num_data
	va_list *fd_extra_args;
	// same situation as ID
	void const **data;
} ringbuffer_node;

typedef struct {
	// We don't anticipate much contention, so it's a global lock
	pthread_rwlock_t lock;
	bool known_full;
	// Max size is actually one less than SIZE_MAX, which is used as sentinel value
	size_t size;
	size_t id_len;
	// Cannot be 0
	size_t num_data;
	/* Array of free callbacks for data; each can be NULL, or the whole thing can be NULL
	 * in which case fd_extra_args for every node is ignored
	 */
	free_data_cb *fd_cbs;
	// Yes, the entirety of buf is flat here
	ringbuffer_node buf[];
} ringbuffer;

ringbuffer *restrict ringbuffer_create(size_t size,
                                        size_t id_len,
                                        size_t num_data,
                                        free_data_cb fd_cbs[num_data])
                      __attribute__((pure));
//TODO: Unsure whether we could use const here...
void ringbuffer_free(ringbuffer *ring);

/*! Returns true if we overwrote something
 *  Note that the var-args are ONLY for the FIRST datum's free_data_extra_args
 */
bool ringbuffer_insert(ringbuffer *restrict const ring,
                        uint8_t const *restrict const id,
                        void const *restrict const data[], ...)
      __attribute__((nonnull));
bool ringbuffer_insert_v(ringbuffer *restrict const ring,
                          uint8_t const *restrict const id,
                          void const *restrict const data[],
                          va_list fd_extra_args[])
      __attribute__((nonnull));
// Returns true if we found something to remove
bool ringbuffer_remove(ringbuffer *restrict const ring,
                        uint8_t const *restrict const id)
      __attribute__((nonnull));

// Returns SIZE_MAX if not found
size_t ringbuffer_find(ringbuffer const *restrict const ring,
						uint8_t const *restrict const id)
        __attribute__((nonnull, pure));

#endif