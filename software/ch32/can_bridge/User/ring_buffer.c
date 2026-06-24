/*
 * ring_buffer.c - lock-free circular buffer implementation
 */

#include "ring_buffer.h"

void ring_buffer_init(ring_buffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

void ring_buffer_write_isr(ring_buffer_t *rb, uint8_t byte)
{
    uint16_t next = (uint16_t)(rb->head + 1);

    if (next >= RING_BUFFER_SIZE) {
        next = 0;
    }

    /* Buffer full — silently drop the oldest byte */
    if (next == rb->tail) {
        rb->tail++;
        if (rb->tail >= RING_BUFFER_SIZE) {
            rb->tail = 0;
        }
    }

    rb->buf[rb->head] = byte;
    rb->head = next;
}

bool ring_buffer_read(ring_buffer_t *rb, uint8_t *byte)
{
    if (rb->tail == rb->head) {
        return false;  /* empty */
    }

    *byte = rb->buf[rb->tail];
    rb->tail++;
    if (rb->tail >= RING_BUFFER_SIZE) {
        rb->tail = 0;
    }
    return true;
}

uint16_t ring_buffer_available(ring_buffer_t *rb)
{
    if (rb->head >= rb->tail) {
        return (uint16_t)(rb->head - rb->tail);
    }
    return (uint16_t)(RING_BUFFER_SIZE - rb->tail + rb->head);
}
