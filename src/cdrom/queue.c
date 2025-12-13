#include "queue.h"

#include <stdlib.h>
#include <string.h>

struct queue* queue_create(uint32_t size) {
	struct queue* q = malloc(sizeof(*q) + size);
	q->size = size;
	queue_clear(q);
	return q;
}

void queue_push(struct queue* q, uint8_t val) {
	if(queue_full(q)) {
		return;
	}

	q->buf[q->write_off++] = val;
}

void queue_push_buf(struct queue* q, void* buf, uint32_t size) {
	uint32_t bytes_available = q->size - q->write_off;
	if(bytes_available == 0) {
		return;
	}
	if(size > bytes_available) {
		size = bytes_available;
	}

	memcpy(&q->buf[q->write_off], buf, size);
	q->write_off += size;
}

uint8_t queue_pop(struct queue* q) {
	if(queue_empty(q)) {
		return 0;
	}

	return q->buf[q->read_off++];
}

uint8_t queue_peek(struct queue* q) {
	if(queue_empty(q)) {
		return 0;
	}

	return q->buf[q->read_off];
}

uint8_t* queue_get_item(struct queue* q, uint32_t item) {
	if(item > queue_items(q)) {
		return 0;
	}

	return &q->buf[q->read_off + item];
}

uint32_t queue_items(struct queue* q) {
	return q->write_off - q->read_off;
}

bool queue_full(struct queue* q) {
	return q->read_off == q->size;
}

bool queue_empty(struct queue* q) {
	return q->write_off == q->read_off;
}

void queue_clear(struct queue* q) {
	memset(q->buf, 0, q->size);
	q->read_off = 0;
	q->write_off = 0;
}

