/*
 * ws2812.c - WS2812B LED driver via SPI+DMA
 *
 * WS2812 timing (SPI 3MHz, one WS2812 bit = 4 SPI bits):
 *   Logic 1: 0xE (1110b) → ~1.17μs high, ~0.33μs low
 *   Logic 0: 0x8 (1000b) → ~0.33μs high, ~0.83μs low
 *
 * Pixel data: 24 bits per pixel, transmitted MSB-first.
 * Color order: G[7:0], R[7:0], B[7:0].
 *
 * Buffer: 1 pixel × 12 SPI bytes + 25 reset bytes = 37 bytes.
 */

#include "ws2812.h"
#include "ch32v20x.h"

#define SPI_BYTES_PER_PIXEL  12    /* 3 colors × 4 bytes (8 bits × 4 SPI bits) */
#define RESET_BYTES           25    /* >50μs low = reset, 25 bytes @ 3MHz ≈ 67μs */

/* Block encoding:
 *   bit 1 → nibble 0xE,  bit 0 → nibble 0x8
 *   Two WS2812 bits → one SPI byte (MSB first within each nibble)
 */
static const uint8_t ENC_BIT_1 = 0xE;
static const uint8_t ENC_BIT_0 = 0x8;

/* DMA buffer: 12 data + 25 reset = 37 bytes */
static uint8_t g_buf[SPI_BYTES_PER_PIXEL + RESET_BYTES];

/* ── Helpers ── */

static void ws2812_encode_byte(uint8_t byte, uint8_t *out)
{
    for (int bit = 0; bit < 8; bit++) {
        uint8_t val = (byte >> (7 - bit)) & 1;
        uint8_t nibble = val ? ENC_BIT_1 : ENC_BIT_0;
        uint8_t shift = (bit & 1) ? 0 : 4;
        out[bit / 2] |= (nibble << shift);
    }
}

/* ── Public API ── */

void ws2812_init(void)
{
    GPIO_InitTypeDef  gpio = {0};
    SPI_InitTypeDef   spi  = {0};

    /* Clocks */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PA7 = SPI1_MOSI (alternate function push-pull, 50MHz) */
    gpio.GPIO_Pin   = GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* SPI1: half-duplex TX-only master, CPOL=1 CPHA=1, 3MHz */
    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode      = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize  = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL      = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA      = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS       = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
    SPI_InitStructure.SPI_FirstBit  = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    /* DMA1 Channel 3: memory → SPI1 TX, normal mode */
    DMA_DeInit(DMA1_Channel3);
    DMA_InitTypeDef dma = {0};
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
    dma.DMA_MemoryBaseAddr     = (uint32_t)g_buf;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = sizeof(g_buf);
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_Low;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);

    /* Enable SPI TX DMA request */
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    /* Clear buffer */
    for (int i = 0; i < sizeof(g_buf); i++) g_buf[i] = 0;
}

void ws2812_set_color(uint8_t g, uint8_t r, uint8_t b)
{
    /* Clear and rebuild buffer */
    for (int i = 0; i < sizeof(g_buf); i++) g_buf[i] = 0;

    /* Order: G, R, B */
    ws2812_encode_byte(g, &g_buf[0]);   /* G → bytes [0..3] */
    ws2812_encode_byte(r, &g_buf[4]);   /* R → bytes [4..7] */
    ws2812_encode_byte(b, &g_buf[8]);   /* B → bytes [8..11] */
    /* bytes [12..36] stay 0 → reset pulse */
}

void ws2812_set_packed(uint32_t grb)
{
    uint8_t g = (grb >> 16) & 0xFF;
    uint8_t r = (grb >> 8)  & 0xFF;
    uint8_t b =  grb        & 0xFF;
    ws2812_set_color(g, r, b);
}

void ws2812_update(void)
{
    /* Wait for previous DMA to complete */
    while (DMA_GetCurrDataCounter(DMA1_Channel3) != 0) { }

    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA1_Channel3->MADDR = (uint32_t)g_buf;
    DMA1_Channel3->CNTR  = sizeof(g_buf);
    DMA_Cmd(DMA1_Channel3, ENABLE);
}
