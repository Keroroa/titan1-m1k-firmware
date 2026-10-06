# Titan-1 Mouse · 开源低延迟固件 + 网页配置器

把一只 Bst 牌游戏鼠标(ATmega32U4 + PixArt PMW3360)从"原厂盲写固件"改造成
M1K 架构的极限低延迟可编程设备,并配了一个 Linear 风格的 WebHID 网页配置器。

逆向 → 二进制补丁 → QMK → **M1K 架构专用固件(本仓库)**,四步全程自研实录,
逆向取证报告在 [`docs/`](docs/)。

## 这个固件为什么快

| 环节 | 常规固件(含 QMK) | 本固件 |
|---|---|---|
| 传感器轮询 | 自由跑循环,与 USB 帧无对齐 | **125µs 固定节拍**,每帧 SOF 硬同步 |
| 采样→上线时机 | 随机相位,平均 +0.5ms | 固定贴着主机轮询窗口,确定性 |
| 按键路径 | 矩阵扫描 + 5ms 去抖 + 状态机 | **EIFR 边沿锁存,按下零去抖延迟(≤125µs)** |
| 报告通道 | 发送队列 | bank 忙则杀行合并,永不阻塞永不丢 |
| 传感器休眠 | 默认配置 | rest mode 彻底关闭,无唤醒惩罚 |
| 体积 | QMK ~17KB | **7.9KB**(纯代码 3.8KB + SROM 4KB) |

USB 全速芯片上报率封顶 1kHz(要 8kHz 需换 STM32 + 高速 USB)。

## 功能

- **三档 DPI**(100–12000,步进 100)+ 第 6 键循环切档,LED 反馈
- **网页配置器**([`web/titan1.html`](web/titan1.html),Chrome/Edge 打开即用):
  点卡片切档、LOD/角度捕捉/滚轮方向、**实时回报率表 + X/Y 轨迹示波器**、
  **DPI 实测校准**、配置导入导出、一键进 DFU
- **遥测流**:固件每帧经配置接口发遥测包(浏览器禁止网页读鼠标接口,这是绕行方案)
- EEPROM 配置持久化;PD2(中键)按住插线进 DFU;bootloader 永不覆盖,随时回滚

## 硬件(逆向实锤,详见 docs/)

| 功能 | ATmega32U4 引脚 |
|---|---|
| PMW3360 NCS / SPI | B0 / B1(SCK) B2(MOSI) B3(MISO) |
| 按键 1-5 | D0-D4(低有效+上拉;INT0-3 的 EIFR 当边沿锁存器) |
| DPI 循环键 | D5(原厂"幽灵键"位) |
| 滚轮 A/B | C6 / C7 |
| LED | B5、B6(常亮)、D7(档位反馈) |
| 进 DFU | 按住 PD2(中键)插 USB |

## 构建

任何 AVR 工具链(avr-gcc ≥ 8,avr-libc,avr-binutils)直接 make:

```bash
cd firmware
make        # 产物 bstmouse2.hex(约 7.9KB / 28.7KB 可用)
```

WSL 参考流水线(Windows 侧源码 → Linux 侧编译 → hex 拷回)见 `firmware/build.sh.example`
的注释;克隆到 WSL 后 `make` 即可。

## 刷机

1. 进 DFU:按住鼠标中键插 USB,或网页配置器点"进入刷机模式",
   或 `tools/send_dfu.ps1`(Windows,经 raw HID 命令免按键)
2. 安装 [Atmel DFU 驱动](https://sourceforge.net/projects/dfu-programmer/)(首次,pnputil 装整个目录)
3. 烧录:

```bash
dfu-programmer atmega32u4 erase --force
dfu-programmer atmega32u4 flash bstmouse2.hex
dfu-programmer atmega32u4 start
```

bootloader 位于 0x7E00,任何刷写操作都不会碰它;刷错随时可再来。

## raw HID 配置协议(32 字节包,usage FF60/61)

`data[0]` = 命令,`data[1]` = 状态(0 成功):

| 命令 | 值 | 说明 |
|---|---|---|
| GET_INFO | 0x10 | 'B''M' + 固件版 + 协议版 + 当前档 |
| GET_CFG / SET_CFG | 0x11 / 0x12 | 3×DPI u16LE + 档位 + LOD + 捕捉 + 滚轮反转 |
| GET_DEBUG | 0x13 | Product_ID / SROM_ID / Config1 / Motion / Lift / Snap |
| BOOTLOADER | 0x14 | 跳 DFU |
| TELEMETRY | 0x15 | data[2]=1/0 开关帧遥测流 |

遥测包(tag=0x20,每 USB 帧一份):帧号 u16、按键位图、X/Y 增量 i16×2、
滚轮 i8、运动标志。

## 仓库结构

```
firmware/   M1K 架构固件(avr-gcc,~7.9KB)
web/        网页配置器(单文件,无依赖,WebHID)
docs/       原厂固件逆向取证报告(引脚/SPI 协议/USB 报告格式)
tools/      逆向工具(avrdump.py 反汇编器、make_mod.py 补丁器)+ DFU 触发脚本
```

## 致谢与许可

- USB 栈血统:PJRC Teensy(MIT)→ qsxcv/Furiosus → [Zaunkönig M1K](https://github.com/zaunkoenig-firmware/m1k-firmware)(MIT),本项目保持 MIT
- SROM blob 来自 M1K 仓库(PixArt PMW3360 固件数据,按社区惯例随附)
- 逆向方法参考了 [QMK](https://qmk.fm) 生态的 pmw33xx 驱动实现
- 本仓库**不包含**原厂固件镜像与其衍生反汇编(版权原因);`docs/` 中的分析结论为独立研究成果,`tools/` 脚本需自备原厂镜像才能复现

## 免责声明

仅适用于你自己拥有的硬件。刷机有风险,操作前确保理解 DFU 流程;
因使用本仓库内容造成的任何损失,作者不承担责任。
