#ifndef PSX_CDROM_QUEUE_H
#define PSX_CDROM_QUEUE_H

#include <psx/cdrom.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct __psx_cdr_queue queue_t;

#define QUEUE_BUF(q) ((uint8_t*)((q) + 1))

static inline uint32_t queue_items(queue_t* q) {
	return q->write_off - q->read_off;
}

static inline bool queue_full(queue_t* q) {
	return q->write_off == q->size;
}

static inline bool queue_empty(queue_t* q) {
	return q->write_off == q->read_off;
}

static inline void queue_clear(queue_t* q) {
	uint8_t* data = QUEUE_BUF(q);
	memset(data, 0, q->size);
	q->read_off = 0;
	q->write_off = 0;
}

static inline void queue_push(queue_t* q, uint8_t val) {
	if(queue_full(q)) {
		return;
	}

	uint8_t* data = QUEUE_BUF(q);
	data[q->write_off++] = val;
}

static inline void queue_push_buf(queue_t* q, void* buf, uint32_t size) {
	uint32_t bytes_available = q->size - q->write_off;
	if(bytes_available == 0) {
		return;
	}
	if(size > bytes_available) {
		size = bytes_available;
	}

	uint8_t* data = QUEUE_BUF(q);
	memcpy(&data[q->write_off], buf, size);
	q->write_off += size;
}

static inline uint8_t queue_pop(queue_t* q) {
	if(queue_empty(q)) {
		return 0;
	}

	uint8_t* data = QUEUE_BUF(q);
	return data[q->read_off++];
}

static inline uint8_t queue_peek(queue_t* q) {
	if(queue_empty(q)) {
		return 0;
	}

	uint8_t* data = QUEUE_BUF(q);
	return data[q->read_off];
}

#endif // #ifndef PSX_CDROM_QUEUE_H
