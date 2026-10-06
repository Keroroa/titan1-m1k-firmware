# Titan-1 鼠标固件

[English](README.md) | **简体中文**

Titan-1(Bst)游戏鼠标(ATmega32U4 + PixArt PMW3360)的开源极限低延迟固件,
基于 [Zaunkönig M1K](https://github.com/zaunkoenig-firmware/m1k-firmware) 的架构,
附零依赖 WebHID 网页配置器与原厂固件的完整逆向取证报告。

完整历程:原厂固件 → 二进制补丁 → QMK 移植 → **本固件**。
让这一切成为可能的取证分析在 [`docs/`](docs/)。

## 它为什么快

| 环节 | 常规固件(含 QMK) | 本固件 |
|---|---|---|
| 传感器轮询 | 自由跑循环,与 USB 帧无对齐 | **125µs 固定节拍**,与 USB SOF 硬同步 |
| 采样→上线时机 | 随机相位,平均多等 ~0.5ms | 对齐主机轮询窗口,完全确定 |
| 按键路径 | 矩阵扫描 + 5ms 去抖 + 状态机 | **EIFR 边沿锁存,按下零去抖延迟(≤125µs)** |
| 报告通道 | 发送队列 | bank 忙则杀行合并,永不阻塞、永不丢包 |
| 传感器休眠 | 默认电源管理 | rest mode 彻底关闭,无唤醒惩罚 |
| 体积 | QMK 构建 ~17KB | **7.9KB**(3.8KB 代码 + 4KB SROM) |

USB 全速芯片上报率封顶 1kHz——要 8kHz 需换带高速 USB 的 STM32
(参考 Zaunkönig M2K/M3K)。

## 功能

- **三档 DPI**(100–12000,步进 100)+ 专用循环切档键,LED 反馈
- **网页配置器**([`web/titan1.html`](web/titan1.html),Chrome/Edge,单文件):
  点卡片切档、LOD / 角度捕捉 / 滚轮方向、**实时回报率表 + X/Y 运动示波器**、
  **DPI 实测校准**、配置导入导出、一键进 DFU
- **遥测流**:固件每 USB 帧经 vendor 接口发一包遥测(浏览器无权读取鼠标接口,
  这是绕行方案,与 EGG 8k 配置器同思路)
- 配置 EEPROM 持久化;按住中键插线进 DFU;bootloader 永不覆盖,随时可回滚

## 硬件(逆向实锤,详见 docs/)

| 功能 | ATmega32U4 引脚 |
|---|---|
| PMW3360 NCS / SPI | B0 / B1(SCK) B2(MOSI) B3(MISO) |
| 按键 1–5 | D0–D4(低有效+上拉;INT0-3 的 EIFR 当边沿锁存器) |
| DPI 循环键 | D5(原厂固件未启用的"幽灵键") |
| 滚轮 A/B | C6 / C7 |
| LED | B5、B6(常亮)、D7(档位反馈) |
| 进 DFU | 按住 PD2(中键)插 USB |

## 构建

任意 AVR 工具链(avr-gcc ≥ 8、avr-libc、avr-binutils),纯 `make`:

```bash
cd firmware
make        # 产出 bstmouse2.hex(约 7.9KB / 28.7KB 可用)
```

WSL 流水线示例(源码在 Windows、编译在 Linux、hex 拷回)见
`firmware/build.sh.example`。

## 刷机

1. 进 DFU:按住中键插线,或在网页配置器点「进入刷机模式」,
   或运行 `tools/send_dfu.ps1`(Windows,发送 raw HID 命令,无需按键)。
2. 首次需装 [Atmel DFU 驱动](https://sourceforge.net/projects/dfu-programmer/)
   (`pnputil /add-driver atmel_usb_dfu.inf /install`,注意要**整个目录**而非单个 inf)。
3. 烧录:

```bash
dfu-programmer atmega32u4 erase --force
dfu-programmer atmega32u4 flash bstmouse2.hex
dfu-programmer atmega32u4 start
```

bootloader 位于 0x7E00,任何刷写都不会擦到它——刷坏随时可以重刷救回。

## raw HID 配置协议(32 字节包,usage FF60/61)

`data[0]` = 命令,`data[1]` = 状态(0 = 成功):

| 命令 | 值 | 用途 |
|---|---|---|
| GET_INFO | 0x10 | 'B''M' 魔数 + 固件/协议版本 + 当前档 |
| GET_CFG / SET_CFG | 0x11 / 0x12 | 3×DPI u16LE + 档位 + LOD + 角度捕捉 + 滚轮反转 |
| GET_DEBUG | 0x13 | Product_ID / SROM_ID / Config1 / Motion / Lift / Snap |
| BOOTLOADER | 0x14 | 跳 DFU |
| TELEMETRY | 0x15 | data[2] = 1/0 开关每帧遥测流 |

遥测包(tag 0x20,每 USB 帧一包):帧号 u16、按键位图、X/Y 增量 i16×2、
滚轮 i8、运动标志。

## 仓库结构

```
firmware/   M1K 架构固件(avr-gcc,~7.9KB)
web/        网页配置器(单文件,零依赖,WebHID)
docs/       原厂固件取证报告(引脚 / SPI 协议 / USB 报告格式)
tools/      逆向工具(avrdump.py 反汇编器、make_mod.py 补丁器)+ DFU 触发脚本
```

## 致谢与许可

- USB 栈血统:PJRC Teensy(MIT)→ Furiosus/qsxcv →
  [Zaunkönig M1K](https://github.com/zaunkoenig-firmware/m1k-firmware)(MIT);本项目沿用 MIT
- SROM blob 来自 M1K 仓库(PixArt PMW3360 传感器固件数据,
  按 QMK/M1K 社区惯例随附用于互操作)
- 逆向方法与 [QMK](https://qmk.fm) 的 pmw33xx 驱动交叉验证
- 本仓库刻意**不包含**原厂固件镜像及其衍生反汇编(版权原因);
  `docs/` 的分析结论为独立研究成果,`tools/` 的脚本需自备固件转储才能复现

## 免责声明

仅限在你自己拥有的硬件上使用。刷机有风险,操作前请确保理解 DFU 流程。
对因使用本仓库内容造成的任何损失,作者不承担责任。
