/*
 * can_driver.c - CAN1 driver implementation for CH32V203
 *
 * Uses WCH HAL: CAN_Init / CAN_Transmit / CAN_Receive / CAN_MessagePending
 * Baudrate computed for APB1=72MHz: tq = (prescaler) / 72MHz
 * Bit time = (1 + BS1 + BS2) * tq
 */

#include "can_driver.h"
#include <string.h>

/* ── ESR register bits (STM32-compatible layout) ── */
#define CAN_ESR_EWGF  ((uint32_t)0x00000001)
#define CAN_ESR_EPVF  ((uint32_t)0x00000002)
#define CAN_ESR_BOFF  ((uint32_t)0x00000004)

/* ── Internal state ── */

static can_state_t g_can_state = CAN_STATE_STOPPED;
static uint16_t    g_can_stb_pin;    /* GPIO_Pin_X bitmask */
static GPIO_TypeDef *g_can_stb_port; /* GPIOB or GPIOA etc. */

/* ── GPIO + CAN Init ── */

void can_driver_init(uint16_t tx_pin, uint16_t rx_pin, uint16_t stb_pin,
                     uint32_t bitrate)
{
    GPIO_InitTypeDef  gpio = {0};
    CAN_InitTypeDef   can  = {0};
    CAN_FilterInitTypeDef filt = {0};

    g_can_stb_pin  = stb_pin;
    g_can_stb_port = GPIOB;

    /* 1. Clocks */
    /* GPIOB (TX/RX/STB) and AFIO (CAN remap) on APB2 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB, ENABLE);
    /* CAN1 on APB1 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    /* CAN1 Remap1: PB8=RX, PB9=TX */
    GPIO_PinRemapConfig(GPIO_Remap1_CAN1, ENABLE);

    /* PB9 = CAN_TX (alternate function push-pull, 50MHz) */
    gpio.GPIO_Pin   = tx_pin;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    /* PB8 = CAN_RX (input pull-up) */
    gpio.GPIO_Pin  = rx_pin;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    /* STB pin (controls TJA1051 S pin: LOW=normal, HIGH=standby) */
    gpio.GPIO_Pin   = stb_pin;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, stb_pin);   /* LOW = 正常模式 (在停止状态下) */

    /* 2. CAN Controller Init (starts in init mode → stopped) */
    CAN_DeInit(CAN1);

    can.CAN_TTCM = DISABLE;   /* time-triggered: off */
    can.CAN_ABOM = ENABLE;    /* auto bus-off management */
    can.CAN_AWUM = DISABLE;   /* auto wake-up: off */
    can.CAN_NART = DISABLE;   /* no auto retransmit → retry on error */
    can.CAN_RFLM = DISABLE;   /* RX FIFO locked mode: off (overwrite oldest) */
    can.CAN_TXFP = DISABLE;   /* TX FIFO priority: by ID */
    can.CAN_Mode = CAN_Mode_Normal;
    can.CAN_SJW  = CAN_SJW_1tq;
    can.CAN_BS1  = CAN_BS1_12tq;  /* default 500k */
    can.CAN_BS2  = CAN_BS2_3tq;
    can.CAN_Prescaler = 9;         /* 72MHz/(9×16) = 500k */
    CAN_Init(CAN1, &can);

    /* 3. Filter: pass all (accept every frame) */
    filt.CAN_FilterNumber         = 0;
    filt.CAN_FilterMode           = CAN_FilterMode_IdMask;
    filt.CAN_FilterScale          = CAN_FilterScale_32bit;
    filt.CAN_FilterIdHigh         = 0;
    filt.CAN_FilterIdLow          = 0;
    filt.CAN_FilterMaskIdHigh     = 0;
    filt.CAN_FilterMaskIdLow      = 0;
    filt.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    filt.CAN_FilterActivation     = ENABLE;
    CAN_FilterInit(&filt);

    g_can_state = CAN_STATE_STOPPED;
}

/* ── Start / Stop ── */

void can_driver_start(void)
{
    /* Request leave init mode */
    CAN1->CTLR &= ~CAN_CTLR_INRQ;
    /* Wait for hardware to leave init mode */
    uint32_t timeout = 1000000;
    while ((CAN1->MSR & CAN_MSR_INAK) && --timeout) { }

    /* Take TJA1051 out of standby */
    GPIO_ResetBits(GPIOB, g_can_stb_pin);
    g_can_state = CAN_STATE_RUNNING;
}

void can_driver_stop(void)
{
    /* Request enter init mode */
    CAN1->CTLR |= CAN_CTLR_INRQ;
    uint32_t timeout = 1000000;
    while (!(CAN1->MSR & CAN_MSR_INAK) && --timeout) { }

    /* Put TJA1051 in standby */
    GPIO_SetBits(GPIOB, g_can_stb_pin);
    g_can_state = CAN_STATE_STOPPED;
}

/* ── Send ── */

bool can_driver_send(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data)
{
    if (g_can_state == CAN_STATE_BUS_OFF) return false;

    CanTxMsg tx;
    memset(&tx, 0, sizeof(tx));

    if (ext) {
        tx.ExtId = id;
        tx.IDE   = CAN_Id_Extended;
    } else {
        tx.StdId = id;
        tx.IDE   = CAN_Id_Standard;
    }
    tx.RTR = CAN_RTR_Data;
    tx.DLC = (dlc > 8) ? 8 : dlc;
    if (dlc > 0) {
        memcpy(tx.Data, data, tx.DLC);
    }

    uint8_t mbox = CAN_Transmit(CAN1, &tx);

    /* Wait for completion with timeout */
    uint32_t timeout = 0xFFFFF;
    while ((CAN_TransmitStatus(CAN1, mbox) != CAN_TxStatus_Ok) && --timeout) { }

    return (timeout > 0);
}

/* ── Poll Receive ── */

bool can_driver_poll_receive(uint32_t *id, bool *ext, uint8_t *dlc,
                              uint8_t *data, uint32_t *timestamp_us)
{
    if (CAN_MessagePending(CAN1, CAN_FIFO0) == 0) {
        return false;
    }

    CanRxMsg rx;
    CAN_Receive(CAN1, CAN_FIFO0, &rx);

    if (rx.IDE == CAN_Id_Extended) {
        *id  = rx.ExtId;
        *ext = true;
    } else {
        *id  = rx.StdId;
        *ext = false;
    }
    *dlc = rx.DLC;
    if (rx.DLC > 0) {
        memcpy(data, rx.Data, rx.DLC);
    }

    /* TODO: wire a hardware timer for accurate timestamps */
    *timestamp_us = 0;

    return true;
}

/* ── Bitrate ── */

void can_driver_set_bitrate(uint32_t bitrate)
{
    uint16_t prescaler;
    uint8_t  bs1, bs2;

    /* APB1 = 72MHz. Bit time = (1 + BS1_tq + BS2_tq) × prescaler / 72MHz
     * Target sample point ≈ 80% (BS1 / (1 + BS1 + BS2) ≈ 0.8) */
    switch (bitrate) {
    case 125000:
        prescaler = 36;       /* tq = 36/72M = 500ns, 16tq = 8μs → 125k */
        bs1 = CAN_BS1_12tq;   /* 12 tq (6000ns) */
        bs2 = CAN_BS2_3tq;    /* 3 tq (1500ns), sample at 81% */
        break;
    case 250000:
        prescaler = 18;       /* tq = 250ns, 16tq = 4μs → 250k */
        bs1 = CAN_BS1_12tq;
        bs2 = CAN_BS2_3tq;
        break;
    case 500000:
        prescaler = 9;        /* tq = 125ns, 16tq = 2μs → 500k */
        bs1 = CAN_BS1_12tq;
        bs2 = CAN_BS2_3tq;
        break;
    case 1000000:
        prescaler = 9;        /* tq = 125ns, 8tq = 1μs → 1M */
        bs1 = CAN_BS1_5tq;    /* 5 tq (625ns) */
        bs2 = CAN_BS2_2tq;    /* 2 tq (250ns), sample at 75% */
        break;
    default:
        return;
    }

    /* Enter init mode */
    CAN1->CTLR |= CAN_CTLR_INRQ;
    uint32_t timeout = 1000000;
    while (!(CAN1->MSR & CAN_MSR_INAK) && --timeout) { }

    /* Write BTR register directly */
    CAN1->BTR = ((uint32_t)CAN_Mode_Normal << 30) |
                ((uint32_t)(bs2 & 0x07) << 20) |
                ((uint32_t)(bs1 & 0x0F) << 16) |
                ((uint32_t)CAN_SJW_1tq << 24) |
                ((uint32_t)(prescaler & 0x3FF));

    /* Leave init mode */
    CAN1->CTLR &= ~CAN_CTLR_INRQ;
    timeout = 1000000;
    while ((CAN1->MSR & CAN_MSR_INAK) && --timeout) { }
}

/* ── Filter ── */

void can_driver_set_filter(const can_filter_entry_t *filters, uint8_t count)
{
    /* Enter init mode for filter reconfiguration */
    CAN1->CTLR |= CAN_CTLR_INRQ;
    uint32_t timeout = 1000000;
    while (!(CAN1->MSR & CAN_MSR_INAK) && --timeout) { }

    if (count == 0 || filters == NULL) {
        /* Set pass-all */
        CAN_FilterInitTypeDef f = {0};
        f.CAN_FilterNumber = 0;
        f.CAN_FilterMode   = CAN_FilterMode_IdMask;
        f.CAN_FilterScale  = CAN_FilterScale_32bit;
        f.CAN_FilterIdHigh = 0;
        f.CAN_FilterIdLow  = 0;
        f.CAN_FilterMaskIdHigh = 0;
        f.CAN_FilterMaskIdLow  = 0;
        f.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
        f.CAN_FilterActivation = ENABLE;
        CAN_FilterInit(&f);
    } else {
        if (count > 14) count = 14;

        for (uint8_t i = 0; i < count; i++) {
            CAN_FilterInitTypeDef f = {0};
            f.CAN_FilterNumber = i;
            f.CAN_FilterMode   = CAN_FilterMode_IdMask;
            f.CAN_FilterScale  = CAN_FilterScale_32bit;
            f.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
            f.CAN_FilterActivation = ENABLE;

            if (filters[i].ext) {
                /* Extended ID: 29 bits packed in WCH format */
                f.CAN_FilterIdHigh = (uint16_t)((filters[i].id >> 13) & 0xFFFF);
                f.CAN_FilterIdLow  = (uint16_t)(((filters[i].id & 0x1FFF) << 3) | 0x04);
                f.CAN_FilterMaskIdHigh = (uint16_t)((filters[i].mask >> 13) & 0xFFFF);
                f.CAN_FilterMaskIdLow  = (uint16_t)(((filters[i].mask & 0x1FFF) << 3) | 0x04);
            } else {
                /* Standard ID: 11 bits in upper part of 16-bit field */
                f.CAN_FilterIdHigh = (uint16_t)((filters[i].id & 0x7FF) << 5);
                f.CAN_FilterIdLow  = 0;
                f.CAN_FilterMaskIdHigh = (uint16_t)((filters[i].mask & 0x7FF) << 5);
                f.CAN_FilterMaskIdLow  = 0;
            }

            CAN_FilterInit(&f);
        }
    }

    /* Leave init mode */
    CAN1->CTLR &= ~CAN_CTLR_INRQ;
    timeout = 1000000;
    while ((CAN1->MSR & CAN_MSR_INAK) && --timeout) { }
}

/* ── Status ── */

void can_driver_get_status(can_status_t *status)
{
    uint32_t esr = CAN1->ESR;

    status->tx_errors = (uint8_t)((esr >> 16) & 0xFF);
    status->rx_errors = (uint8_t)((esr >> 24) & 0xFF);

    if (esr & CAN_ESR_BOFF) {
        status->state   = CAN_STATE_BUS_OFF;
        status->bus_off = true;
        g_can_state     = CAN_STATE_BUS_OFF;
    } else {
        status->state   = g_can_state;
        status->bus_off = false;
    }
}
