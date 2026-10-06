#ifndef BST_USB_H
#define BST_USB_H

#include <stdint.h>

void usb_init(void);
uint8_t usb_configured(void);

#define MOUSE_ENDPOINT    3   /* interface 0: 鼠标报告 IN */
#define RAW_IN_ENDPOINT   1   /* interface 1: raw HID 应答 IN */
#define RAW_OUT_ENDPOINT  2   /* interface 1: raw HID 命令 OUT */
#define RAW_EPSIZE        32

/* usb.c 内部使用 */
#ifdef USB_PRIVATE_INCLUDE
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#define EP_TYPE_CONTROL       0x00
#define EP_TYPE_BULK_IN       0x81
#define EP_TYPE_BULK_OUT      0x80
#define EP_TYPE_INTERRUPT_IN  0xC1
#define EP_TYPE_INTERRUPT_OUT 0xC0

#define EP_SINGLE_BUFFER      0x02
#define EP_DOUBLE_BUFFER      0x06

#define EP_SIZE(s) ((s) == 64 ? 0x30 : ((s) == 32 ? 0x20 : ((s) == 16 ? 0x10 : 0x00)))

#define MAX_ENDPOINT          4

#define LSB(n) (n & 255)
#define MSB(n) ((n >> 8) & 255)

/* ATmega32U4 @16MHz(晶振) */
#define HW_CONFIG()   (UHWCON = 0x01)
#define PLL_CONFIG()  (PLLCSR = 0x12)
#define USB_CONFIG()  (USBCON = ((1<<USBE)|(1<<OTGPADE)))
#define USB_FREEZE()  (USBCON = ((1<<USBE)|(1<<FRZCLK)))

#define GET_STATUS        0
#define CLEAR_FEATURE     1
#define SET_FEATURE       3
#define SET_ADDRESS       5
#define GET_DESCRIPTOR    6
#define GET_CONFIGURATION 8
#define SET_CONFIGURATION 9
#define GET_INTERFACE     10
#define SET_INTERFACE     11

#define HID_GET_REPORT    1
#define HID_GET_IDLE      2
#define HID_GET_PROTOCOL  3
#define HID_SET_REPORT    9
#define HID_SET_IDLE      10
#define HID_SET_PROTOCOL  11
#endif

#endif
