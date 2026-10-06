/* Bst 鼠标 M1K 架构固件 · USB 栈
 * 血统: PJRC Teensy usb_mouse (MIT, qsxcv/Furiosus 改) → Zaunkoenig M1K → 本文件
 * 改动: ATmega32U4 单芯片; 5 键+16bit XY+滚轮报告; 增加 interface 1 (raw HID 配置通道)
 */

#define USB_PRIVATE_INCLUDE
#include "usb.h"

#define STR_MANUFACTURER  L"Titan"
#define STR_PRODUCT       L"Titan-1"

/* 与原厂固件/QMK 版相同的 VID:PID */
#define VENDOR_ID   0x16C0
#define PRODUCT_ID  0x047E

#define SUPPORT_ENDPOINT_HALT

#define ENDPOINT0_SIZE  32

#define MOUSE_INTERFACE 0
#define RAW_INTERFACE    1
#define MOUSE_SIZE       8    /* 报告 6 字节,端点 8 */

/* EP1 raw IN / EP2 raw OUT / EP3 mouse IN,EP4 空闲 */
static const uint8_t PROGMEM endpoint_config_table[] = {
	1, EP_TYPE_INTERRUPT_IN,  EP_SIZE(RAW_EPSIZE) | EP_SINGLE_BUFFER,
	1, EP_TYPE_INTERRUPT_OUT, EP_SIZE(RAW_EPSIZE) | EP_SINGLE_BUFFER,
	1, EP_TYPE_INTERRUPT_IN,  EP_SIZE(MOUSE_SIZE) | EP_SINGLE_BUFFER,
	0
};

static const uint8_t PROGMEM device_descriptor[] = {
	18,					// bLength
	1,					// bDescriptorType
	0x00, 0x02,				// bcdUSB
	0,					// bDeviceClass
	0,					// bDeviceSubClass
	0,					// bDeviceProtocol
	ENDPOINT0_SIZE,				// bMaxPacketSize0
	LSB(VENDOR_ID), MSB(VENDOR_ID),		// idVendor
	LSB(PRODUCT_ID), MSB(PRODUCT_ID),	// idProduct
	0x00, 0x02,				// bcdDevice
	1,					// iManufacturer
	2,					// iProduct
	0,					// iSerialNumber
	1					// bNumConfigurations
};

/* 鼠标报告: 5 键 + X16 + Y16 + 滚轮 (6 字节,与原厂固件同格式) */
static const uint8_t PROGMEM mouse_hid_report_desc[] = {
	0x05, 0x01,		// Usage Page (Generic Desktop)
	0x09, 0x02,		// Usage (Mouse)
	0xA1, 0x01,		// Collection (Application)
	0x05, 0x09,		//   Usage Page (Button)
	0x19, 0x01,		//   Usage Minimum (Button 1)
	0x29, 0x05,		//   Usage Maximum (Button 5)
	0x15, 0x00,		//   Logical Minimum (0)
	0x25, 0x01,		//   Logical Maximum (1)
	0x95, 0x05,		//   Report Count (5)
	0x75, 0x01,		//   Report Size (1)
	0x81, 0x02,		//   Input (Data, Variable, Absolute)
	0x95, 0x01,		//   Report Count (1)
	0x75, 0x03,		//   Report Size (3)
	0x81, 0x03,		//   Input (Constant) — 字节 0
	0x05, 0x01,		//   Usage Page (Generic Desktop)
	0x09, 0x30,		//   Usage (X)
	0x09, 0x31,		//   Usage (Y)
	0x16, 0x01, 0x80,	//   Logical Minimum (-32767)
	0x26, 0xFF, 0x7F,	//   Logical Maximum (32767)
	0x75, 0x10,		//   Report Size (16)
	0x95, 0x02,		//   Report Count (2)
	0x81, 0x06,		//   Input (Data, Variable, Relative) — 字节 1-4
	0x09, 0x38,		//   Usage (Wheel)
	0x15, 0x81,		//   Logical Minimum (-127)
	0x25, 0x7F,		//   Logical Maximum (127)
	0x75, 0x08,		//   Report Size (8)
	0x95, 0x01,		//   Report Count (1)
	0x81, 0x06,		//   Input (Data, Variable, Relative) — 字节 5
	0xC0			// End Collection
};

/* raw HID: FF60/61,输入输出各 32 字节(与 QMK raw HID 同构,网页配置器零改动) */
static const uint8_t PROGMEM raw_hid_report_desc[] = {
	0x06, 0x60, 0xFF,	// Usage Page (Vendor 0xFF60)
	0x09, 0x61,		// Usage (0x61)
	0xA1, 0x01,		// Collection (Application)
	0x09, 0x62,		//   Usage (0x62)
	0x15, 0x00,		//   Logical Minimum (0)
	0x26, 0xFF, 0x00,	//   Logical Maximum (255)
	0x75, 0x08,		//   Report Size (8)
	0x95, RAW_EPSIZE,	//   Report Count (32)
	0x81, 0x02,		//   Input (Data, Variable, Absolute)
	0x09, 0x63,		//   Usage (0x63)
	0x15, 0x00,		//   Logical Minimum (0)
	0x26, 0xFF, 0x00,	//   Logical Maximum (255)
	0x75, 0x08,		//   Report Size (8)
	0x95, RAW_EPSIZE,	//   Report Count (32)
	0x91, 0x02,		//   Output (Data, Variable, Absolute)
	0xC0			// End Collection
};

/* 指向各接口 HID 类描述符(9 字节)在 config 描述符内的偏移 */
#define MOUSE_HID_DESC_OFFSET  (9 + 9)                /* config + iface0 */
#define RAW_HID_DESC_OFFSET    (9 + 25 + 9)           /* + iface0 全部 + iface1 */

#define CONFIG1_DESC_SIZE  (9 + (9+9+7) + (9+9+7+7))
static const uint8_t PROGMEM config1_descriptor[CONFIG1_DESC_SIZE] = {
	9,					// bLength
	2,					// bDescriptorType
	LSB(CONFIG1_DESC_SIZE),			// wTotalLength
	MSB(CONFIG1_DESC_SIZE),
	2,					// bNumInterfaces
	1,					// bConfigurationValue
	0,					// iConfiguration
	0xC0,					// bmAttributes
	50,					// bMaxPower (100mA)
	/* interface 0: 鼠标(boot) */
	9, 4, MOUSE_INTERFACE, 0, 1, 0x03, 0x01, 0x02, 0,
	9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(mouse_hid_report_desc), 0,
	7, 5, MOUSE_ENDPOINT | 0x80, 0x03, 6, 0, 1,
	/* interface 1: raw HID(厂商定义) */
	9, 4, RAW_INTERFACE, 0, 2, 0x03, 0x00, 0x00, 0,
	9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof(raw_hid_report_desc), 0,
	7, 5, RAW_IN_ENDPOINT | 0x80, 0x03, RAW_EPSIZE, 0, 1,
	7, 5, RAW_OUT_ENDPOINT, 0x03, RAW_EPSIZE, 0, 1
};

struct usb_string_descriptor_struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	int16_t wString[];
};
static const struct usb_string_descriptor_struct PROGMEM string0 = { 4, 3, {0x0409} };
static const struct usb_string_descriptor_struct PROGMEM string1 = {
	sizeof(STR_MANUFACTURER), 3, STR_MANUFACTURER
};
static const struct usb_string_descriptor_struct PROGMEM string2 = {
	sizeof(STR_PRODUCT), 3, STR_PRODUCT
};

static const struct descriptor_list_struct {
	uint16_t	wValue;
	uint16_t	wIndex;
	const uint8_t	*addr;
	uint8_t		length;
} PROGMEM descriptor_list[] = {
	{0x0100, 0x0000, device_descriptor, sizeof(device_descriptor)},
	{0x0200, 0x0000, config1_descriptor, sizeof(config1_descriptor)},
	{0x2200, MOUSE_INTERFACE, mouse_hid_report_desc, sizeof(mouse_hid_report_desc)},
	{0x2100, MOUSE_INTERFACE, config1_descriptor + MOUSE_HID_DESC_OFFSET, 9},
	{0x2200, RAW_INTERFACE, raw_hid_report_desc, sizeof(raw_hid_report_desc)},
	{0x2100, RAW_INTERFACE, config1_descriptor + RAW_HID_DESC_OFFSET, 9},
	{0x0300, 0x0000, (const uint8_t *)&string0, 4},
	{0x0301, 0x0409, (const uint8_t *)&string1, sizeof(STR_MANUFACTURER)},
	{0x0302, 0x0409, (const uint8_t *)&string2, sizeof(STR_PRODUCT)}
};
#define NUM_DESC_LIST (sizeof(descriptor_list)/sizeof(struct descriptor_list_struct))

static volatile uint8_t usb_configuration = 0;
static uint8_t mouse_protocol = 1;

void usb_init(void)
{
	HW_CONFIG();
	USB_FREEZE();
	PLL_CONFIG();
	while (!(PLLCSR & (1<<PLOCK))) ;
	USB_CONFIG();
	UDCON = 0;
	usb_configuration = 0;
	UDIEN = (1<<EORSTE);
	sei();
}

uint8_t usb_configured(void)
{
	return usb_configuration;
}

ISR(USB_GEN_vect)
{
	uint8_t intbits;
	intbits = UDINT;
	UDINT = 0;
	if (intbits & (1<<EORSTI)) {
		UENUM = 0;
		UECONX = 1;
		UECFG0X = EP_TYPE_CONTROL;
		UECFG1X = EP_SIZE(ENDPOINT0_SIZE) | EP_SINGLE_BUFFER;
		UEIENX = (1<<RXSTPE);
		usb_configuration = 0;
	}
}

static inline void usb_wait_in_ready(void) { while (!(UEINTX & (1<<TXINI))) ; }
static inline void usb_send_in(void) { UEINTX = ~(1<<TXINI); }

ISR(USB_COM_vect)
{
	uint8_t intbits;
	const uint8_t *list;
	const uint8_t *cfg;
	uint8_t i, n, len, en;
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
	uint16_t desc_val;
	const uint8_t *desc_addr;
	uint8_t desc_length;

	UENUM = 0;
	intbits = UEINTX;
	if (intbits & (1<<RXSTPI)) {
		bmRequestType = UEDATX;
		bRequest = UEDATX;
		wValue = UEDATX;
		wValue |= (UEDATX << 8);
		wIndex = UEDATX;
		wIndex |= (UEDATX << 8);
		wLength = UEDATX;
		wLength |= (UEDATX << 8);
		UEINTX = ~((1<<RXSTPI) | (1<<RXOUTI) | (1<<TXINI));
		if (bRequest == GET_DESCRIPTOR) {
			list = (const uint8_t *)descriptor_list;
			for (i=0; ; i++) {
				if (i >= NUM_DESC_LIST) {
					UECONX = (1<<STALLRQ)|(1<<EPEN);
					return;
				}
				desc_val = pgm_read_word(list);
				if (desc_val != wValue) {
					list += sizeof(struct descriptor_list_struct);
					continue;
				}
				list += 2;
				desc_val = pgm_read_word(list);
				if (desc_val != wIndex) {
					list += sizeof(struct descriptor_list_struct)-2;
					continue;
				}
				list += 2;
				desc_addr = (const uint8_t *)pgm_read_word(list);
				list += 2;
				desc_length = pgm_read_byte(list);
				break;
			}
			len = (wLength < 256) ? wLength : 255;
			if (len > desc_length) len = desc_length;
			do {
				do {
					i = UEINTX;
				} while (!(i & ((1<<TXINI)|(1<<RXOUTI))));
				if (i & (1<<RXOUTI)) return;
				n = len < ENDPOINT0_SIZE ? len : ENDPOINT0_SIZE;
				for (i = n; i; i--) {
					UEDATX = pgm_read_byte(desc_addr++);
				}
				len -= n;
				usb_send_in();
			} while (len || n == ENDPOINT0_SIZE);
			return;
		}
		if (bRequest == SET_ADDRESS) {
			usb_send_in();
			usb_wait_in_ready();
			UDADDR = wValue | (1<<ADDEN);
			return;
		}
		if (bRequest == SET_CONFIGURATION && bmRequestType == 0) {
			usb_configuration = wValue;
			usb_send_in();
			cfg = endpoint_config_table;
			for (i=1; i<5; i++) {
				UENUM = i;
				en = pgm_read_byte(cfg++);
				UECONX = en;
				if (en) {
					UECFG0X = pgm_read_byte(cfg++);
					UECFG1X = pgm_read_byte(cfg++);
				}
			}
			UERST = 0x1E;
			UERST = 0;
			return;
		}
		if (bRequest == GET_CONFIGURATION && bmRequestType == 0x80) {
			usb_wait_in_ready();
			UEDATX = usb_configuration;
			usb_send_in();
			return;
		}
		if (bRequest == GET_STATUS) {
			usb_wait_in_ready();
			i = 0;
			#ifdef SUPPORT_ENDPOINT_HALT
			if (bmRequestType == 0x82) {
				UENUM = wIndex;
				if (UECONX & (1<<STALLRQ)) i = 1;
				UENUM = 0;
			}
			#endif
			UEDATX = i;
			UEDATX = 0;
			usb_send_in();
			return;
		}
		#ifdef SUPPORT_ENDPOINT_HALT
		if ((bRequest == CLEAR_FEATURE || bRequest == SET_FEATURE)
		  && bmRequestType == 0x02 && wValue == 0) {
			i = wIndex & 0x7F;
			if (i >= 1 && i <= MAX_ENDPOINT) {
				usb_send_in();
				UENUM = i;
				if (bRequest == SET_FEATURE) {
					UECONX = (1<<STALLRQ)|(1<<EPEN);
				} else {
					UECONX = (1<<STALLRQC)|(1<<RSTDT)|(1<<EPEN);
					UERST = (1 << i);
					UERST = 0;
				}
				return;
			}
		}
		#endif
		if (wIndex == MOUSE_INTERFACE) {
			if (bmRequestType == 0xA1) {
				if (bRequest == HID_GET_REPORT) {
					usb_wait_in_ready();
					UEDATX = 0; UEDATX = 0; UEDATX = 0;
					UEDATX = 0; UEDATX = 0; UEDATX = 0;
					usb_send_in();
					return;
				}
				if (bRequest == HID_GET_PROTOCOL) {
					usb_wait_in_ready();
					UEDATX = mouse_protocol;
					usb_send_in();
					return;
				}
			}
			if (bmRequestType == 0x21) {
				if (bRequest == HID_SET_PROTOCOL) {
					mouse_protocol = wValue;
					usb_send_in();
					return;
				}
			}
		}
	}
	UECONX = (1<<STALLRQ) | (1<<EPEN);	// stall
}
