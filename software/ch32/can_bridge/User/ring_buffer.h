/*
 * ring_buffer.h - lock-free circular buffer
 *
 * Single-producer single-consumer design:
 *   ISR writes (head), main loop reads (tail).
 *   No locking needed — only head is modified by ISR, only tail by main loop.
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define RING_BUFFER_SIZE 4096

typedef struct {
    uint8_t          buf[RING_BUFFER_SIZE];
    volatile uint16_t head;   /* ISR writes */
    uint16_t          tail;   /* main loop reads */
} ring_buffer_t;

void     ring_buffer_init(ring_buffer_t *rb);
void     ring_buffer_write_isr(ring_buffer_t *rb, uint8_t byte);
bool     ring_buffer_read(ring_buffer_t *rb, uint8_t *byte);
uint16_t ring_buffer_available(ring_buffer_t *rb);

#endif
