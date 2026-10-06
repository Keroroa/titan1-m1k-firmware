/* Bst 鼠标 M1K 架构固件 · main
 *
 * 设计(延迟优先,参照 Zaunkoenig M1K + 原厂 Bst 固件逆向结论):
 *  - Timer0 CTC 125µs 节拍,每帧 8 个子周期,i=0 用 USB SOF 硬同步
 *  - 每子周期:EIFR 边沿锁存读键(按下零去抖延迟)→ 滚轮解码 → 传感器 Motion_Burst
 *  - 报告 bank 忙则杀行合并,永不阻塞;只发有变化的 6 字节报告(5键+X16+Y16+滚轮)
 *  - 传感器 rest mode 关闭;X/Y 原始增量直接透传(实证:原厂固件即如此;
 *    当初 QMK 版颠倒的根源是 QMK 驱动内置的 delta *= -1,并非传感器装反)
 *  - raw HID(interface 1)承载网页配置器协议,与 QMK 版完全兼容
 *  - PD2 按住插线 → 跳 0x7E00 bootloader(原厂机制);raw HID 0x14 同效
 *  - EEPROM 配置布局与 QMK 版逐字节兼容(不丢用户设置)
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>

#include "usb.h"
#include "srom_3360_0x05.h"

#define delay_us(t) __builtin_avr_delay_cycles((uint32_t)(t) * 16UL)  /* 16MHz */

/* ---------- 引脚(逆向实锤) ----------
 * PB0=NCS  PB1=SCK PB2=MOSI PB3=MISO
 * PD0-4=按键1-5(低有效+上拉)  PD5=DPI循环键  PD7=LED
 * PC6/PC7=滚轮 A/B  PB5/PB6=LED(B6 原厂常亮)
 */
#define SS_LOW   (PORTB &= ~_BV(PB0))
#define SS_HIGH  (PORTB |=  _BV(PB0))

/* ---------- 配置(EEPROM,布局兼容 QMK 版) ---------- */
#define BST_EE_ADDR 64
#define BST_MAGIC   0xB57A
#define BST_VERSION 2               /* v2:滚轮反转默认关(正转为基准) */

typedef struct __attribute__((packed)) {
	uint16_t magic;
	uint8_t  version;
	uint16_t dpi[3];
	uint8_t  active;
	uint8_t  lod_3mm;
	uint8_t  angle_snap;
	uint8_t  wheel_invert;
	uint8_t  reserved[2];
} bst_config_t;

static bst_config_t cfg;
static bool cfg_dirty;

static const bst_config_t bst_defaults = {
	BST_MAGIC, 2, {400, 800, 1600}, 1, 0, 0, 0, {0, 0}
};

static void cfg_load(void)
{
	eeprom_read_block(&cfg, (const void *)BST_EE_ADDR, sizeof(cfg));
	if (cfg.magic != BST_MAGIC) {
		cfg = bst_defaults;
		eeprom_update_block(&cfg, (void *)BST_EE_ADDR, sizeof(cfg));
	}
	for (uint8_t i = 0; i < 3; i++) {
		if (cfg.dpi[i] < 100) cfg.dpi[i] = 100;
		if (cfg.dpi[i] > 12000) cfg.dpi[i] = 12000;
	}
	if (cfg.active > 2) cfg.active = 1;
}

static void cfg_save(void)
{
	/* EEPROM 写期间放开中断:EP0 控制传输照常服务 */
	uint8_t s = SREG;
	sei();
	eeprom_update_block(&cfg, (void *)BST_EE_ADDR, sizeof(cfg));
	SREG = s;
}

/* ---------- SPI / 传感器 ---------- */
static uint8_t dbg_product_id, dbg_srom_id;

static void spi_init(void)
{
	DDRB |= _BV(PB0) | _BV(PB1) | _BV(PB2);   /* NCS/SCK/MOSI 输出 */
	PORTB |= _BV(PB0);                        /* NCS 空闲高 */
	/* SPE|MSTR|mode3|SPR0 → fosc/8 = 2MHz(手册上限) */
	SPCR = _BV(SPE) | _BV(MSTR) | _BV(CPOL) | _BV(CPHA) | _BV(SPR0);
}

static inline void spi_send(uint8_t b)
{
	SPDR = b;
	while (!(SPSR & _BV(SPIF))) ;
}
static inline uint8_t spi_recv(void)
{
	spi_send(0);
	return SPDR;
}

/* 写 = 0x80|reg, data;t_SWW=180µs(SS 由调用者控制) */
static void spi_w(uint8_t r, uint8_t d)
{
	spi_send(r | 0x80);
	delay_us(180);
	spi_send(d);
	delay_us(180);
}
/* 读 = reg → t_SRAD=160µs → 哑字节 → 取回 → 20µs */
static uint8_t spi_r(uint8_t r)
{
	spi_send(r);
	delay_us(160);
	uint8_t v = spi_recv();
	delay_us(20);
	return v;
}

static void burst_arm(void);   /* 0x50←0x00 一次性武装突发读 */

static void sensor_init(void)
{
	SS_HIGH;
	_delay_ms(50);                       /* 上电稳定(手册 ≥50ms) */

	SS_LOW;  spi_w(0x3B, 0xB6); SS_HIGH;   /* shutdown */
	_delay_ms(300);
	SS_LOW;  delay_us(40); SS_HIGH; delay_us(40);  /* NCS 复位脉冲 */
	SS_LOW;  spi_w(0x3A, 0x5A); SS_HIGH;   /* Power_Up_Reset */
	_delay_ms(50);

	SS_LOW;                                /* 清残留,读 0x02-0x06 */
	(void)spi_r(0x02); (void)spi_r(0x03); (void)spi_r(0x04);
	(void)spi_r(0x05); (void)spi_r(0x06);
	spi_w(0x10, 0x00);                     /* Config2: rest mode 关 */
	spi_w(0x22, 0x00);                     /* 清 Observation */
	SS_HIGH;

	SS_LOW;  spi_w(0x13, 0x1D); SS_HIGH;   /* SROM_Enable 一 */
	_delay_ms(10);
	SS_LOW;  spi_w(0x13, 0x18);            /* SROM_Enable 二 */
	spi_send(0x62 | 0x80);                 /* SROM_Load_Burst */
	for (uint16_t i = 0; i < SROM_LENGTH; i++) {
		delay_us(16);
		spi_send(pgm_read_byte(&srom[i]));
	}
	delay_us(18);
	SS_HIGH;
	delay_us(200);

	SS_LOW;                                /* post-SROM 配置窗口 */
	spi_w(0x10, 0x00);                     /* rest mode 关(再来一次,手册要求) */
	spi_w(0x14, 0xFF); spi_w(0x17, 0xFF);  /* run-on 延时最大 */
	spi_w(0x18, 0x00); spi_w(0x19, 0x00);
	spi_w(0x1B, 0x00); spi_w(0x1C, 0x00);
	spi_w(0x2C, 0x0A); spi_w(0x2B, 0x10);  /* M1K/原厂同值 */
	spi_w(0x0D, 0x00);                     /* 传感器侧不反转(固件处理) */
	spi_w(0x11, 0x00);                     /* 原厂保留写 */
	spi_w(0x0F, (uint8_t)(cfg.dpi[cfg.active] / 100 - 1));  /* Config1 = DPI */
	spi_w(0x63, cfg.lod_3mm ? 0x03 : 0x02);                 /* Lift_Config */
	spi_w(0x42, cfg.angle_snap ? 0x80 : 0x00);              /* Angle_Snap */
	SS_HIGH;

	SS_LOW;  dbg_product_id = spi_r(0x02);   /* 应 0x42(原厂不查,我们查) */
	SS_HIGH;
	SS_LOW;  dbg_srom_id = spi_r(0x2A);      /* 应 0x05 */
	SS_HIGH;

	burst_arm();                          /* 进入 Motion_Burst 模式(关键!) */
}

/* 0x50←任意值 一次性武装突发读;任何其他寄存器写/读之后需重新武装 */
static void burst_arm(void)
{
	SS_LOW;
	spi_w(0x50, 0x00);
	SS_HIGH;
}

static void apply_cfg(void)
{
	SS_LOW;
	spi_w(0x0F, (uint8_t)(cfg.dpi[cfg.active] / 100 - 1));
	spi_w(0x63, cfg.lod_3mm ? 0x03 : 0x02);
	spi_w(0x42, cfg.angle_snap ? 0x80 : 0x00);
	SS_HIGH;
	cfg_dirty = false;
	burst_arm();
}

/* ---------- bootloader:QMK 式魔数 + 看门狗复位跳转 ----------
 * 直接 jmp 0x7E00 时 USB 控制器状态是脏的,小 bootloader 起不来(实测)。
 * QMK 的实证方案:拆 USB → 写 .noinit 魔数 → 看门狗复位;
 * .init3(早于一切 C 初始化)检测 WDRF+魔数后以字地址 0x3F00(=字节 0x7E00)跳入。
 */
#define BOOTLOADER_WORD_ADDR 0x3F00
#define BOOTLOADER_RESET_KEY 0xB007B007U

static uint32_t reset_key __attribute__((section(".noinit")));

__attribute__((used, naked, section(".init3")))
static void bootloader_after_wdt_reset(void)
{
	if ((MCUSR & (1 << WDRF)) && reset_key == BOOTLOADER_RESET_KEY) {
		reset_key = 0;
		((void (*)(void))BOOTLOADER_WORD_ADDR)();
	}
}

static void run_bootloader(void)
{
	cli();
	UDCON  = 1;                      /* detach */
	USBCON = (1 << FRZCLK);          /* 关 USB 控制器 */
	UCSR1B = 0;
	_delay_ms(5);
	reset_key = BOOTLOADER_RESET_KEY;
	wdt_enable(WDTO_250MS);
	for (;;) ;
}

/* ---------- 按键:EIFR 边沿锁存,按下零延迟 ---------- */
#define RELEASE_TICKS 8     /* 释放去抖 8×125µs = 1ms */

static uint8_t btn_state;               /* bit0-4 = 按键1-5 */
static uint8_t btn_release_cnt[5];

static inline void buttons_task(void)
{
	/* INT0-3 = PD0-3 任意沿置 EIFR;有边沿一律视为低(不错过任何按下) */
	uint8_t raw = PIND & ~EIFR;
	EIFR = 0x0F;
	for (uint8_t i = 0; i < 5; i++) {
		if (!(raw & _BV(i))) {
			btn_state |= _BV(i);
			btn_release_cnt[i] = RELEASE_TICKS;
		} else if (btn_state & _BV(i)) {
			if (--btn_release_cnt[i] == 0)
				btn_state &= ~_BV(i);
		}
	}
}

/* PD5 = DPI 循环键(独立简单去抖,不进报告);EEPROM 空闲延迟保存 */
static volatile uint16_t now_ms;
static bool     save_pending;
static uint16_t save_at_ms;

static void cycle_key_task(void)
{
	static uint8_t cnt;
	static bool pressed;
	bool low = !(PIND & _BV(PD5));
	if (low) {
		if (cnt < 250) cnt++;
		if (cnt == 3 && !pressed) {
			pressed = true;
			cfg.active = (cfg.active + 1) % 3;
			cfg_dirty = true;
			save_pending = true;          /* 动作中不动 EEPROM,空闲 1s 后落盘 */
			save_at_ms = now_ms + 1000;
			PORTD ^= _BV(PD7);            /* 档位切换即时反馈 */
		}
	} else {
		cnt = 0;
		pressed = false;
	}
}

/* ---------- 滚轮:A=PC6 B=PC7,复刻原厂半态/整态判向 ---------- */
static bool wheel_valid;
static uint8_t wheel_last_full, wheel_mid;

static inline int8_t wheel_task(void)
{
	bool a = (PINC & _BV(PC6)) != 0;
	bool b = (PINC & _BV(PC7)) != 0;
	if (!wheel_valid) {
		wheel_last_full = a;
		wheel_valid = true;
		return 0;
	}
	if (a != b) {
		wheel_mid = a;
		return 0;
	}
	if (a != wheel_last_full) {
		int8_t dir = (wheel_mid ^ a) ? -1 : 1;   /* 基准=正转(2026-10-06 实测定向) */
		wheel_last_full = a;
		return cfg.wheel_invert ? -dir : dir;
	}
	return 0;
}

/* ---------- raw HID 配置协议(与 QMK 版网页配置器兼容) ---------- */
#define CMD_GET_INFO   0x10
#define CMD_GET_CFG    0x11
#define CMD_SET_CFG    0x12
#define CMD_GET_DEBUG  0x13
#define CMD_BOOTLOADER 0x14
#define CMD_TELEMETRY  0x15   /* data[2]=1/0 开关帧遥测流 */

static uint8_t  reply[RAW_EPSIZE];
static bool     reply_pending;

/* 遥测:每 USB 帧一份,tag=0x20,与 RPC 应答(0x10..0x15)不冲突 */
static bool     telemetry_on;
static uint16_t tel_frame;
static int16_t  tel_dx, tel_dy;
static int8_t   tel_whl;
static uint8_t  tel_moved;
static uint8_t  tel_pkt[RAW_EPSIZE];

static void telemetry_tick(void)
{
	if (!telemetry_on)
		return;
	tel_frame++;
	for (uint8_t i = 0; i < RAW_EPSIZE; i++)
		tel_pkt[i] = 0;
	tel_pkt[0] = 0x20;
	tel_pkt[1] = tel_frame & 0xFF;
	tel_pkt[2] = tel_frame >> 8;
	tel_pkt[3] = btn_state;
	tel_pkt[4] = tel_dx & 0xFF;  tel_pkt[5] = tel_dx >> 8;
	tel_pkt[6] = tel_dy & 0xFF;  tel_pkt[7] = tel_dy >> 8;
	tel_pkt[8] = (uint8_t)tel_whl;
	tel_pkt[9] = tel_moved;
	tel_dx = 0; tel_dy = 0; tel_whl = 0; tel_moved = 0;
	if (reply_pending)
		return;                 /* RPC 应答优先占用 IN bank */
	UENUM = RAW_IN_ENDPOINT;
	if (UEINTX & _BV(TXINI)) {
		for (uint8_t i = 0; i < RAW_EPSIZE; i++)
			UEDATX = tel_pkt[i];
		UEINTX = (uint8_t)~((1 << TXINI) | (1 << FIFOCON));
	}
}

static void rawhid_send_reply(void)
{
	UENUM = RAW_IN_ENDPOINT;
	if (!(UEINTX & _BV(TXINI))) {
		reply_pending = true;      /* IN bank 忙,下轮再发 */
		return;
	}
	for (uint8_t i = 0; i < RAW_EPSIZE; i++)
		UEDATX = reply[i];
	UEINTX = (uint8_t)~((1 << TXINI) | (1 << FIFOCON));
	reply_pending = false;
}

static uint16_t clamp_dpi(uint16_t v)
{
	if (v < 100) v = 100;
	if (v > 12000) v = 12000;
	return (v + 50) / 100 * 100;
}

static void pack_cfg(uint8_t *d)
{
	for (uint8_t i = 0; i < 3; i++) {
		d[2 + i * 2] = cfg.dpi[i] & 0xFF;
		d[3 + i * 2] = cfg.dpi[i] >> 8;
	}
	d[8]  = cfg.active;
	d[9]  = cfg.lod_3mm;
	d[10] = cfg.angle_snap;
	d[11] = cfg.wheel_invert;
}

static void rawhid_poll(void)
{
	if (reply_pending)
		rawhid_send_reply();

	UENUM = RAW_OUT_ENDPOINT;
	if (!(UEINTX & _BV(RXOUTI)))
		return;

	uint8_t buf[RAW_EPSIZE];
	for (uint8_t i = 0; i < RAW_EPSIZE; i++)
		buf[i] = UEDATX;
	UEINTX = (uint8_t)~((1 << RXOUTI) | (1 << FIFOCON));

	for (uint8_t i = 0; i < RAW_EPSIZE; i++)
		reply[i] = 0;
	reply[0] = buf[0];
	reply[1] = 0x00;

	bool jump_dfu = false;
	switch (buf[0]) {
	case CMD_GET_INFO:
		reply[2] = 'B';
		reply[3] = 'M';
		reply[4] = 2;              /* 固件版本 2 = m1k-arch */
		reply[5] = 1;
		reply[6] = cfg.active;
		break;
	case CMD_GET_CFG:
		pack_cfg(reply);
		break;
	case CMD_SET_CFG:
		for (uint8_t i = 0; i < 3; i++) {
			uint16_t v = buf[2 + i * 2] | (buf[3 + i * 2] << 8);
			cfg.dpi[i] = clamp_dpi(v);
		}
		cfg.active       = buf[8] < 3 ? buf[8] : 1;
		cfg.lod_3mm      = buf[9] ? 1 : 0;
		cfg.angle_snap   = buf[10] ? 1 : 0;
		cfg.wheel_invert = buf[11] ? 1 : 0;
		cfg_save();
		cfg_dirty = true;
		pack_cfg(reply);
		break;
	case CMD_GET_DEBUG: {
		SS_LOW; reply[2] = spi_r(0x02); SS_HIGH;   /* Product_ID */
		SS_LOW; reply[4] = spi_r(0x0F); SS_HIGH;   /* Config1 */
		SS_LOW; reply[5] = spi_r(0x03); SS_HIGH;   /* Motion */
		SS_LOW; reply[6] = spi_r(0x63); SS_HIGH;   /* Lift_Config */
		SS_LOW; reply[7] = spi_r(0x42); SS_HIGH;   /* Angle_Snap */
		reply[3] = dbg_srom_id;
		burst_arm();
		break;
	}
	case CMD_BOOTLOADER:
		jump_dfu = true;
		break;
	case CMD_TELEMETRY:
		telemetry_on = buf[2] ? true : false;
		reply[2] = telemetry_on;
		break;
	default:
		reply[1] = 0xFF;
		break;
	}

	rawhid_send_reply();
	if (jump_dfu) {
		sei();
		for (uint8_t t = 0; t < 100; t++) {   /* 让 ACK 上总线 */
			_delay_ms(1);
			if (reply_pending)
				rawhid_send_reply();
		}
		run_bootloader();
	}
}

/* ---------- main ---------- */
int main(void)
{
	CLKPR = 0x80;
	CLKPR = 0x00;                     /* 16MHz 不分频 */

	/* 引脚 */
	DDRD  = _BV(PD6) | _BV(PD7);       /* PD6 原厂恒低,PD7 LED;其余输入 */
	PORTD = 0x3F;                     /* PD0-5 上拉 */
	DDRC  = 0x00;
	PORTC = 0xC0;                     /* 滚轮上拉 */
	PORTB = _BV(PB0) | _BV(PB6);      /* NCS 高 + B6 常亮 */
	DDRB  = _BV(PB0) | _BV(PB1) | _BV(PB2) | _BV(PB5) | _BV(PB6);

	EICRA = 0x55;                     /* INT0-3 任意沿锁存 EIFR(不开中断) */
	EIMSK = 0;
	EIFR  = 0x0F;

	_delay_ms(50);                    /* 上电稳定 + 按键strap窗口 */
	if (!(PIND & _BV(PD2)))           /* 按住中键插线 → DFU(原厂机制) */
		run_bootloader();

	cfg_load();
	spi_init();
	sensor_init();
	usb_init();
	while (!usb_configured()) ;

	/* Timer0 CTC 125µs */
	TCCR0A = 0x02;
	TCCR0B = 0x02;                    /* clk/8 → 0.5µs/tick */
	OCR0A  = 249;                     /* 250 × 0.5µs = 125µs */

	/* 报告累加器 */
	uint8_t  btn_prev = 0;
	uint8_t  btn_bank = 0, btn_bank_prev = 0;
	int16_t  x_bank = 0, y_bank = 0;
	int8_t   whl_bank = 0;
	uint16_t ms = 0, last_motion_ms = 0, save_at_ms = 0;
	bool     save_pending = false;
	uint8_t  sub = 0;

	for (;;) {
		if (sub == 0) {
			UDINT &= ~_BV(SOFI);
			while (!(UDINT & _BV(SOFI))) ;   /* 帧对齐 */
			GTCCR |= _BV(PSRSYNC);
			TCNT0 = 0;
			ms++;
			now_ms = ms;
			/* 空闲时补写 EEPROM(DPI 循环键的延迟保存) */
			if (save_pending && ms >= save_at_ms &&
			    (uint16_t)(ms - last_motion_ms) > 200) {
				cfg_save();
				save_pending = false;
			}
		} else {
			while (!(TIFR0 & _BV(OCF0A))) ;
			TIFR0 = _BV(OCF0A);
		}
		sub = (sub + 1) & 7;

		uint8_t sreg = SREG;
		cli();

		if (sub == 0)
			telemetry_tick();          /* 帧边界:发上一帧累计,随后清零 */

		buttons_task();
		cycle_key_task();
		int8_t whl = wheel_task();

		/* 传感器 Motion_Burst:7 字节 @2MHz + 35µs ≈ 63µs */
		SS_LOW;
		spi_send(0x50);
		delay_us(35);
		(void)spi_recv();                 /* Motion(不查 MOT,与原厂一致) */
		(void)spi_recv();                 /* Observation */
		uint8_t xl = spi_recv(), xh = spi_recv();
		uint8_t yl = spi_recv(), yh = spi_recv();
		SS_HIGH;
		int16_t dx = (int16_t)(xl | (xh << 8));   /* 原始增量直接透传 */
		int16_t dy = (int16_t)(yl | (yh << 8));

		tel_dx += dx; tel_dy += dy; tel_whl += whl;   /* 遥测累计(每帧) */

		if (cfg_dirty)
			apply_cfg();              /* 一次性 ~1.2ms */

		rawhid_poll();                   /* 罕见;配置/调试用 */

		/* 发送:bank 忙则杀行合并(原厂/M1K 同款) */
		if (btn_state != btn_prev || dx || dy || whl) {
			tel_moved = 1;
			if (usb_configured()) {
				UENUM = MOUSE_ENDPOINT;
				if (UESTA0X & _BV(NBUSYBK0)) {
					UEINTX |= _BV(RXOUTI);        /* KILLBK */
					while (UEINTX & _BV(RXOUTI)) ;
				} else {
					btn_bank_prev = btn_bank;
					btn_bank = 0;
					x_bank = 0; y_bank = 0; whl_bank = 0;
				}
				btn_bank |= btn_state;
				x_bank += dx;
				y_bank += dy;
				if (whl_bank < 127 && whl_bank > -128)
					whl_bank += whl;
				if ((btn_bank != btn_bank_prev) || x_bank || y_bank || whl_bank) {
					UEDATX = btn_bank;
					UEDATX = (uint8_t)(x_bank & 0xFF);
					UEDATX = (uint8_t)((x_bank >> 8) & 0xFF);
					UEDATX = (uint8_t)(y_bank & 0xFF);
					UEDATX = (uint8_t)((y_bank >> 8) & 0xFF);
					UEDATX = (uint8_t)whl_bank;
					UEINTX = 0x3A;
				}
			}
			btn_prev = btn_state;
			if (dx || dy)
				last_motion_ms = ms;
		}

		SREG = sreg;
	}
	return 0;
}
