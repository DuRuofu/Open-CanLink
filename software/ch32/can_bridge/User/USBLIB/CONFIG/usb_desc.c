/*
 * usb_desc.c - USB descriptors for open-can-link CAN bridge
 *
 * Modified from WCH SimulateCDC:
 *   - Product string changed to "open-can-link CAN Bridge"
 *   - VID/PID kept as WCH defaults (will apply for custom VID/PID later)
 */

#include "usb_lib.h"
#include "usb_desc.h"

/* USB Device Descriptor */
const uint8_t USBD_DeviceDescriptor[] = {
    USBD_SIZE_DEVICE_DESC,           // bLength
    0x01,                            // bDescriptorType
    0x10, 0x01,                      // bcdUSB (1.1)
    0x02,                            // bDeviceClass (CDC)
    0x00,                            // bDeviceSubClass
    0x00,                            // bDeviceProtocol
    DEF_USBD_UEP0_SIZE,              // bMaxPacketSize0 (8)
    0x86, 0x1A,                      // idVendor  (0x1A86 = WCH)
    0x0C, 0xFE,                      // idProduct (0xFE0C)
    0x00, 0x01,                      // bcdDevice (1.00)
    0x01,                            // iManufacturer
    0x02,                            // iProduct
    0x00,                            // iSerialNumber
    0x01,                            // bNumConfigurations
};

/* USB Configuration Descriptor (CDC ACM, 2 interfaces, 3 endpoints) */
const uint8_t USBD_ConfigDescriptor[] = {
    /* Configuration Descriptor */
    0x09, 0x02,
    USBD_SIZE_CONFIG_DESC & 0xFF, USBD_SIZE_CONFIG_DESC >> 8,
    0x02,                            // bNumInterfaces
    0x01,                            // bConfigurationValue
    0x00,                            // iConfiguration
    0x80,                            // bmAttributes: Bus Powered, Remote Wakeup
    0x32,                            // MaxPower: 100mA

    /* Interface 0 (CDC Control) */
    0x09, 0x04, 0x00, 0x00, 0x01,
    0x02, 0x02, 0x01, 0x00,

    /* CDC Functional Descriptors */
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,

    /* Interrupt IN Endpoint (EP1) */
    0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x01,

    /* Interface 1 (CDC Data) */
    0x09, 0x04, 0x01, 0x00, 0x02,
    0x0A, 0x00, 0x00, 0x00,

    /* Bulk OUT Endpoint (EP2) */
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,

    /* Bulk IN Endpoint (EP3) */
    0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,
};

/* USB String Descriptors */

const uint8_t USBD_StringLangID[USBD_SIZE_STRING_LANGID] = {
    USBD_SIZE_STRING_LANGID,
    USB_STRING_DESCRIPTOR_TYPE,
    0x09, 0x04   /* English (US) */
};

/* Manufacturer: "wch.cn" */
const uint8_t USBD_StringVendor[USBD_SIZE_STRING_VENDOR] = {
    USBD_SIZE_STRING_VENDOR,
    USB_STRING_DESCRIPTOR_TYPE,
    'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0
};

/* Product: "CAN Bridge" (10 chars, fits existing 22-byte descriptor) */
const uint8_t USBD_StringProduct[USBD_SIZE_STRING_PRODUCT] = {
    USBD_SIZE_STRING_PRODUCT,
    USB_STRING_DESCRIPTOR_TYPE,
    'C', 0, 'A', 0, 'N', 0, ' ', 0, 'B', 0, 'r', 0, 'i', 0, 'd', 0, 'g', 0, 'e', 0
};

/* Serial Number: "0123456789" */
uint8_t USBD_StringSerial[USBD_SIZE_STRING_SERIAL] = {
    USBD_SIZE_STRING_SERIAL,
    USB_STRING_DESCRIPTOR_TYPE,
    '0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0, '8', 0, '9', 0
};
