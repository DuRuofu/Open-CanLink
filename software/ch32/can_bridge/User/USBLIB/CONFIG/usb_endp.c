/*
 * usb_endp.c - USB endpoint callbacks for open-can-link CAN bridge
 *
 * Modified from WCH SimulateCDC example:
 *   EP2_OUT: USB data → ring buffer (replaces UART Tx)
 *   EP3_IN:  clear busy flag
 *   Added usb_cdc_send() public API
 */

#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_mem.h"
#include "hw_config.h"
#include "usb_istr.h"
#include "usb_pwr.h"
#include "usb_prop.h"
#include "ring_buffer.h"

extern ring_buffer_t g_usb_rx_ring;

volatile uint8_t USBD_Endp3_Busy;

void EP1_IN_Callback(void)
{
    /* CDC serial state notification — unused */
}

/*
 * EP2_OUT_Callback — called from USB ISR when host sends data.
 * Copy received bytes into the global ring buffer.
 */
void EP2_OUT_Callback(void)
{
    uint8_t buf[64];
    uint32_t len = GetEPRxCount(EP2_OUT & 0x7F);

    PMAToUserBufferCopy(buf, GetEPRxAddr(EP2_OUT & 0x7F), len);

    for (uint32_t i = 0; i < len; i++) {
        ring_buffer_write_isr(&g_usb_rx_ring, buf[i]);
    }

    /* Re-arm EP2 OUT for next packet */
    SetEPRxValid(ENDP2);
}

/*
 * EP3_IN_Callback — called when a bulk-IN transfer completes.
 */
void EP3_IN_Callback(void)
{
    USBD_Endp3_Busy = 0;
}

/*
 * usb_cdc_send — non-blocking send to USB CDC host.
 *
 * Splits data into 64-byte chunks (max full-speed packet).
 * Returns 0 on success, -1 if endpoint is busy.
 */
int usb_cdc_send(const uint8_t *data, uint16_t len)
{
    if (len == 0) return 0;

    uint16_t remain = len;
    const uint8_t *ptr = data;

    while (remain > 0) {
        uint16_t chunk = (remain > DEF_USBD_MAX_PACK_SIZE)
                         ? DEF_USBD_MAX_PACK_SIZE : remain;

        if (USBD_Endp3_Busy) {
            return -1;  /* endpoint still transmitting previous packet */
        }

        USBD_Endp3_Busy = 1;
        UserToPMABufferCopy((uint8_t *)ptr, ENDP3_TXADDR, chunk);
        SetEPTxCount(ENDP3, chunk);
        SetEPTxStatus(ENDP3, EP_TX_VALID);

        ptr += chunk;
        remain -= chunk;
    }

    return 0;
}

/*
 * USBD_ENDPx_DataUp — original WCH API, kept for compatibility.
 */
uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len)
{
    if (endp == ENDP3) {
        if (USBD_Endp3_Busy) {
            return USB_ERROR;
        }
        USB_SIL_Write(EP3_IN, pbuf, len);
        USBD_Endp3_Busy = 1;
        SetEPTxStatus(ENDP3, EP_TX_VALID);
    } else {
        return USB_ERROR;
    }
    return USB_SUCCESS;
}
