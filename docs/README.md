# Bst 鼠标固件逆向 · 总结论(三路分析合并)

主控 ATmega32U4(QFN44,16MHz 外部晶振)/ 传感器 PMW3360 / 固件 LUFA 风格,VID 0x16C0 PID 0x047E。
固件:`2mm_400_1000_1600.hex` = fw1,`2mm_400_4000_800.hex` = fw2(骨架相同,仅默认 DPI 与一笔额外寄存器写不同)。
详细报告:[sensor_protocol.md](sensor_protocol.md) · [gpio_pins.md](gpio_pins.md) · [usb_hid.md](usb_hid.md)

## 引脚总表(QMK config 直接抄)

| 32U4 脚 | QFN44 | 方向 | 功能 | 证据 |
|---|---|---|---|---|
| PB0 | 8 | 出 | PMW3360 NCS(空闲高) | 0x1226/0x1228 |
| PB1 | 9 | 出 | SPI SCK(硬件 SPI,mode3,250kHz) | 0x1222,SPCR=0x5C |
| PB2 | 10 | 出 | SPI MOSI | 0x1222 |
| PB3 | 11 | 入 | SPI MISO | 复位默认 |
| PB5 | 29 | 出 | LED | 0x11E6 |
| PB6 | 30 | 出 | LED(常高) | 0x11EE |
| PB4/PB7 | 28/12 | — | 未使用 | 无访问 |
| PC6 | 31 | 入+上拉 | 滚轮编码器 A 相 | 0x11D8,读@0x1690 |
| PC7 | 32 | 入+上拉 | 滚轮编码器 B 相 | 0x11D8,读@0x1698 |
| PD0 | 18 | 入+上拉 | 按键1 → 报告 bit0(兼 strap:DPI 400 档) | 0x11D2/0x16E8 |
| PD1 | 19 | 入+上拉 | 按键2 → bit1(兼 strap:第三 DPI 档) | 0x1702 |
| PD2 | 20 | 入+上拉 | 按键3 → bit2(兼 strap:上电拉低→进 DFU bootloader 0x7E00) | 0x1218 |
| PD3 | 21 | 入+上拉 | 按键4 → bit3 | 0x171C |
| PD4 | 25 | 入+上拉 | 按键5 → bit4 | 0x1736 |
| PD5 | 22 | 入+上拉 | 第6键:扫描+去抖了,但 `ANDI 0x1F`@0x1770 把它丢弃,永不上报 | 0x1750/0x1770 |
| PD6 | 26 | 出 | 恒低,用途不明(测试点/预留) | 0x11E2 |
| PD7 | 27 | 出 | LED(DPI 档位指示:档1/2 近常高,档3 常低) | 0x11EA/0x18EC |
| PE2/PE6 | 33/1 | — | 未使用(MOTION 引脚原厂根本没接中断,轮询制) | EIMSK=0 |
| PF0-7 | 36-41 | — | 全部未使用 | 无访问 |

最终掩码:DDRB=0x67 PORTB=0x61 | DDRC=0x00 PORTC=0xC0 | DDRD=0xC0 PORTD=0xBF | DDRE/DDRF=0

## 传感器协议摘要

- SPI mode 3,NCS 空闲高;写=0x80|reg+data(笔间≥180µs);读=reg→≥160µs(burst≥35µs)→哑字节→取回
- 初始化 17 步全序列见 sensor_protocol.md §2;SROM 下载自 flash **0x00AC-0x10A9(4094 字节,两固件逐字节相同)**
- DPI = Config1(0x0F),值=(cpi/100)-1。fw1 {400,1000,1600} 与文件名一致;fw2 默认 4000,第三档代码实为 1600(文件名"800"有误)
- **DPI 无运行时切换**:上电按住按键 strap(PD0=400 / PD1=第三档 / 不按=默认),选中档位只影响 LED 模式
- Lift_Config=0x02(+2mm,即"2mm"含义)、Angle_Snap 关、Config2=0x00
- 运行时:Timer0 CTC 62.5µs(16kHz)节拍,主循环里 Motion_Burst(0x50)只读 Delta X/Y 四字节,不判 MOT 位直接累加

## USB/HID 摘要

- IN 报告 6 字节:[0]=按键 bit0-4(+3bit 填充) [1/2]=X int16 LE [3/4]=Y int16 LE [5]=滚轮 int8
- EP0=32B 控制传输(支持 SET_ADDRESS/GET_DESCRIPTOR/SET_PROTOCOL 等,SET_IDLE/SET_REPORT STALL);EP3 IN 中断 8B FIFO,bInterval=1ms
- USB_General/USB_Endpoint 向量在 0x0028/0x002C(中断驱动);SRAM 0x0102 是软件标志(SET_CONFIGURATION 值),主循环每帧 SEI 窗口内服务 EP0
- 报告"有变化才发"(按键变化∨ΔX∨ΔY∨滚轮≠0),bank 忙则合并增量;每 8 报告用 SOF 重同步 Timer0

## 意外发现

1. **上电按住 PD2 键 = 进 DFU bootloader**(JMP 0x7E00),不用拆机短接焊盘
2. PD5 是一个"幽灵键":完整扫描去抖但被掩码丢弃(SKU 预留/测试点)
3. fw2 文件名的 800 档实为 1600(代码里不存在 0x07=800cpi 的写入)
4. 原厂不读 Product_ID/SROM_ID,对传感器全盲写
5. 固件内嵌 SROM 4094 字节(手册要求 4096,差 2 字节)

## QMK 移植要点

- 硬件 SPI 默认脚(PB1/PB2/PB3)与 QMK 32U4 默认完全一致,NCS=PB0,零引脚冲突
- 原厂轮询传感器(MOTION 脚未接中断),QMK pmw3360 可配 MOTION 中断获得更好延迟,或照抄轮询
- SROM:QMK pmw3360 自带 4096B blob;flash 预算 28KB(32KB-4KB bootloader),Ploopy Classic(同芯片同传感器跑 QMK)已验证放得下
- 按键 PD0-4(可选 PD5 补第6键)、滚轮 PC6/PC7、LED PB5/PB6/PD7
- 网页配置(QMK VIA 或自写 WebHID)的前提已经齐了:协议时序、SROM、引脚全部已知
