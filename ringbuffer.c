#include "ringbuffer.h"

#include <malloc.h>
#include <string.h>
#include <assert.h>

#include "debug.h"

ringbuffer *restrict ringbuffer_create(size_t size,
										 size_t id_len,
										 free_data_cb fd_cb) {
	assert(size != 0);
	assert(id_len != 0);
	ringbuffer *restrict ring =
		(ringbuffer*)malloc(sizeof(ringbuffer) + sizeof(ringbuffer_node)*size);
	assert(ring != NULL);
	ring->size = size;
	ring->known_full = false;
	ring->id_len = id_len;
	ring->fd_cb = fd_cb;
	for(size_t i=0; i<size; i++) {
		ring->buf[i] = (ringbuffer_node) { NULL, NULL, 0, NULL, 0, 1 };
	}
	
	assert(pthread_rwlock_init(&ring->lock, NULL) == 0);
	
	return ring;
}

static void ringbuffer_node_clear(ringbuffer *restrict const ring,
								   size_t index) {
	assert(ring != NULL);
	assert(index < ring->size);
	ringbuffer_node *const node = &ring->buf[index];
	if(node->id != NULL) {
		free(node->id);
		node->id = NULL;
		if(ring->fd_cb != NULL && node->data != NULL) {
			ring->fd_cb(node->data, node->free_data_extra_args);
		}
		node->data = NULL;
		va_end(node->free_data_extra_args);
	}
	else
		assert(node->data == NULL);
}

void ringbuffer_free(ringbuffer *restrict ring) {
	assert(ring != NULL);
	pthread_rwlock_wrlock(&ring->lock);
	for(size_t i=0; i<ring->size; i++) {
		ringbuffer_node_clear(ring, i);
	}
	pthread_rwlock_destroy(&ring->lock);
	free(ring);
}

// Returns true if we overwrote something
bool ringbuffer_insert(ringbuffer *restrict const ring,
						uint8_t const *restrict const id,
						void const *restrict const data,
						size_t data_len, ...) {
	assert(ring != NULL);
	assert(id != NULL);
	
	pthread_rwlock_wrlock(&ring->lock);
	// Walk until we find an empty node or the node we're looking for; increment age of everyone
	bool match = false;
	size_t vacancy_at = SIZE_MAX;
	for(size_t i=0; i<ring->size; i++) {
		ringbuffer_node *node = &ring->buf[i];
		node->age++;
		if(!match && vacancy_at == SIZE_MAX && node->id == NULL)
			vacancy_at = i;
		if(node->id != NULL && memcmp(id, node->id, ring->id_len) == 0) {
			//We found a match; refresh the node
			match = true;
			node->refcnt++;
			node->age = 0;
		}
	}
	if(match) goto end;

	uint8_t *id_copy = (uint8_t*)malloc(ring->id_len);
	assert(id_copy != NULL);
	memcpy(id_copy, id, ring->id_len);
	ringbuffer_node to_insert = (ringbuffer_node) { id_copy, data, data_len, NULL, 0, 1 };
	va_list ap;
	va_start(ap, data_len);
	va_copy(to_insert.free_data_extra_args, ap);
	va_end(ap);
	
	if(vacancy_at != SIZE_MAX) {
		assert(ring->buf[vacancy_at].data == NULL);
		ring->buf[vacancy_at] = to_insert;
	}
	else {
		typeof(to_insert.age) max_age = 0;
		size_t oldest_index = ring->size-1;
		for(size_t i=0; i<ring->size; i++) {
			ringbuffer_node *node = &ring->buf[i];
			if(node->age > max_age) {
				max_age = node->age;
				oldest_index = i;
			}
			node->age++;
		}
		ringbuffer_node_clear(ring, oldest_index);
		ring->buf[oldest_index] = to_insert;
		ring->known_full = true;
	}
end:
	pthread_rwlock_unlock(&ring->lock);
	return vacancy_at == SIZE_MAX;
}

// Returns true if we found something to remove
bool ringbuffer_remove(ringbuffer *restrict const ring,
						uint8_t const *restrict const id) {
	assert(ring != NULL);
	assert(id != NULL);
	size_t to_remove = ringbuffer_find(ring, id);
	if(to_remove == SIZE_MAX)
		return false;
	pthread_rwlock_wrlock(&ring->lock);
	if(--ring->buf[to_remove].refcnt <= 0)
		ringbuffer_node_clear(ring, to_remove);
	ring->known_full = false;
	pthread_rwlock_unlock(&ring->lock);
	return true;
}

size_t ringbuffer_find(ringbuffer const *restrict const ring,
						uint8_t const *restrict const id) {
	assert(ring != NULL);
	assert(id != NULL);
	pthread_rwlock_rdlock(&ring->lock);
 	size_t found_at = SIZE_MAX;
	for(size_t i=0; i<ring->size; i++) {
		ringbuffer_node *node = &ring->buf[i];
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