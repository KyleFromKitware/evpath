#include "ringbuffer.h"

#include <malloc.h>
#include <string.h>
#include <assert.h>

#include "debug.h"

ring_buffer *restrict ring_buffer_create(size_t size,
										 size_t id_len,
										 free_data_cb fd_cb)
                                         __attribute__((pure)) {
	assert(size != 0);
	assert(id_len != 0);
	ring_buffer *restrict ring =
		(ring_buffer*)malloc(sizeof(ring_buffer) + sizeof(ring_buffer_node)*size);
	assert(ring != NULL);
	ring->size = size;
	ring->known_full = false;
	ring->id_len = id_len;
	ring>fd_cb = fd_cb;
	for(size_t i=0; i<size; i++) {
		ring_buffer_node[i] = (ring_buffer_node) { NULL, NULL, 0, NULL, 0, 1 };
	}
	
	// There may eventually be attributes
	phtread_rwlockattr_t rwlock_attr;
	pthread_rwlockattr_init(&rwlock_attr);
	
	assert(pthread_rwlock_init(&ring->lock, &rwlock_attr));
	
	pthread_rwlockattr_destroy(&rwlock_attr);
	
	return ring;
}

static void ring_buffer_node_clear(ring_buffer *restrict const ring,
								   size_t index)
							       __attribute__((nonnull)) {
	assert(ring != NULL);
	assert(index < ring->size);
	ring_buffer_node *const node = &ring[index];
	if(node->id != NULL) {
		free(node->id);
		node->id = NULL;
		if(ring->fd_cb != NULL && node->data != NULL) {
			ring->fd_cb(node->data, node->free_data_extra_args);
			node->data = NULL;
		}
		va_end(node->free_data_extra_args);
	}
	else
		assert(node->data == NULL);
}

void ring_buffer_free(ring_buffer *resrict ring) {
	assert(ring != NULL);
	pthread_rwlock_wrlock(&ring->lock);
	for(size_t i=0; i<ring->size; i++) {
		ring_buffer_node_clear(ring, i);
	}
	pthread_rwlock_destroy(&ring->lock);
	free(ring);
}

// Returns true if we overwrote something
bool ring_buffer_insert(ring_buffer *restrict const ring,
						uint8_t const *restrict const id,
						void const *restrict const data,
						size_t data_len, ...)
						__attribute__((nonnull (1,2))) {
	assert(ring_buffer != NULL);
	assert(id != NULL);
	
	pthread_rwlock_wrlock(&ring->lock);
	// Walk until we find an empty node or the node we're looking for; increment age of everyone
	bool match = false;
	size_t vacancy_at = SIZE_MAX;
	for(size_t i=0; i<ring->size; i++) {
		ring_buffer_node *node = &ring->buf[i];
		node->age++;
		if(!match && vacancy_at == SIZE_MAX && node->id == NULL)
			vacancy_at = i;
		if(memcmp(id, node->id, ring->id_len) == 0) {
			//We found a match; refresh the node
			match = true;
			node->refcnt++;
			node->age = 0;
		}
	}
	if(match) goto end;
	
	va_start(ap, data_len);
	va_copy(free_data_extra_args, ap);
	va_end(ap);
	uint8_t *id_copy = (uint8_t*)malloc(ring->id_len);
	assert(id_copy != NULL);
	memcpy(id_copy, id, ring->id_len);
	ring_buffer_node to_insert = (ring_buffer_node) { id_copy, data, data_len, free_data_extra_args, 0, 1 };
	
	if(vacancy_at != SIZE_MAX) {
		assert(ring->buf[vacancy_at].data == NULL);
		ring->buf[vacancy_at] = to_insert;
	}
	else {
		typeof(ring_buffer_node.age) max_age = 0;
		size_t oldest_index = ring->size-1;
		for(size_t i=0; i<ring->size; i++) {
			ring_buffer_node *node = &ring->buf[i];
			if(node->age > max_age) {
				max_age = node->age;
				oldest_index = i;
			}
			node->age++;
		}
		ring_buffer_node_clear(ring, oldest_index);
		ring->buf[oldest_index] = to_insert;
		ring->known_full = true;
	}
end:
	pthread_rwlock_unlock(&ring->lock);
	return vacancy_at != SIZE_MAX;
}

// Returns true if we found something to remove
bool ring_buffer_remove(ring_buffer *restrict const ring,
						uint8_t const *restrict const id)
						__attribute__((nonnull)) {
	assert(ring_buffer != NULL);
	assert(id != NULL);
	size_t to_remove = ring_buffer_find(ring, id);
	if(to_remove == SIZE_MAX)
		return false;
	pthread_rwlock_wrlock(&ring->lock);
	if(--ring->buf[to_remove].refcnt <= 0)
		ring_buffer_node_clear(ring, to_remove);
	ring->known_full = false;
	pthread_rwlock_unlock(&ring->lock);
	return true;
}

size_t ring_buffer_find(ring_buffer const *restrict const ring,
						uint8_t const *restrict const id)
						__attribute__((nonnull, pure)) {
	assert(ring_buffer != NULL);
	assert(id != NULL);
	pthread_rwlock_rdlock(&ring->lock);
 	size_t found_at = SIZE_MAX;
	for(size_t i=0; i<ring->size; i++) {
		ring_buffer_node *node = &ring->buf[i];
		if(found_at == SIZE_MAX && node->id != NULL) {
			if(memcmp(id, node->id, ring->id_len) == 0) {
				found_at = i;
				break;
			}
		}
	}
	pthread_rwlock_unlock(&ring->lock);
	return found_at;
}