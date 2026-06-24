/*
 * ws2812.h - WS2812/WS2812B LED driver via SPI+DMA (PA7)
 *
 * Uses SPI1 half-duplex TX mode at 3MHz.
 * Color order: G-R-B (WS2812 native encoding).
 *
 * Hardware: PA7 = SPI1_MOSI, single pixel.
 */

#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>

/* Function-like convenience macros (GRB order) */
#define WS2812_OFF   0x000000  /*  G=0, R=0, B=0  */
#define WS2812_BLUE  0x000014  /*  G=0, R=0, B=20 (dim)  */
#define WS2812_GREEN 0x140000  /*  G=20,R=0, B=0 (dim)  */
#define WS2812_RED   0x001400  /*  G=0, R=20,B=0 (dim)  */

void ws2812_init(void);

/* Set color. g, r, b are 0-255. GRB order, not RGB! */
void ws2812_set_color(uint8_t g, uint8_t r, uint8_t b);

/* Set color from a packed 24-bit value (0xGGRRBB) */
void ws2812_set_packed(uint32_t grb);

/* Push buffer to LED via DMA (blocking until previous transfer done) */
void ws2812_update(void);

#endif
