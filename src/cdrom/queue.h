#ifndef PSX_CDROM_QUEUE_H
#define PSX_CDROM_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

struct queue {
	uint32_t size;
	uint32_t read_off;
	uint32_t write_off;
	uint8_t buf[];
};

struct queue* queue_create(uint32_t size);
void queue_clear(struct queue* q);

void queue_push(struct queue* q, uint8_t val);
void queue_push_buf(struct queue* q, void* buf, uint32_t size);
uint8_t queue_pop(struct queue* q);
uint8_t queue_peek(struct queue* q);
uint8_t* queue_get_item(struct queue* q, uint32_t pos);

uint32_t queue_items(struct queue* q);
bool queue_full(struct queue* q);
bool queue_empty(struct queue* q);

#endif // #ifndef PSX_CDROM_QUEUE_H
