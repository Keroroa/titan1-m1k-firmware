# USB HID 层逆向报告(fw_400_1000_1600.bin / fw_400_4000_800.bin)

对象:ATmega32U4 游戏鼠标固件,USB 字符串 `"Public"`@0x10DD / `"Bst"`@0x10ED,VID 0x16C0 PID 0x047E。
所有地址为 bin 内字节偏移(= flash 字节地址)。本文与 `sensor_protocol.md` 同一套地址约定;
fw2 的 USB 功能代码与 fw1 **逐字节相同**(仅整体 +0x20 平移,见 §1.4)。

**工具修正(在 `_re_decode.py` 之上)**:该解码器仍漏掉 3 个 SBRC/SBRS 变体,本文手工修正:

| 地址 | 机器码 | 实际指令 | 说明 |
|---|---|---|---|
| 0x1928 | `00 FE` | **SBRC r0, 0** | (w&0xFE00)==0xFC00 才是 SBRC,解码器掩码写错 |
| 0x169E 区 / 0x188C / 0x1BBE / 0x1BFA / 0x1C12 / 0x1C0E | `80 FF` | **SBRS r24, 0** | (w&0xFE00)==0xFE00 |
| 0x195E / 0x19B8 | `83 FF` | **SBRS r24, 3** | 同上 |

**寄存器映射修正(重要)**:任务给的 "32U4 USB 寄存器(SRAM 地址)UHWCON 0xF8 … UEINT 0x114" 一表
**整体错了 ~0x20**。以 avr-libc `iom32u4.h` 为准(已下载核对,且与固件代码访问地址完全互证):

| 寄存器 | SRAM 地址 | 寄存器 | SRAM 地址 |
|---|---|---|---|
| UHWCON | 0xD7 | UESTA0X | 0xEE |
| USBCON | 0xD8 | UESTA1X | 0xEF |
| USBSTA | 0xD9 | UEIENX | 0xF0 |
| USBINT | 0xDA | **UEDATX** | **0xF1** |
| UDCON | 0xE0 | UEBCLX | 0xF2 |
| **UDINT** | **0xE1** | UEINT | 0xF4 |
| UDIEN | 0xE2 | | |
| UDADDR | 0xE3 | 位定义(关键字节) | |
| UDFNUML/H | 0xE4/0xE5 | UEINTX: TXINI=0, STALLEDI=1, RXOUTI=2, **RXSTPI=3**, NAKOUTI=4, RWAL=5, NAKINI=6, FIFOCON=7 | |
| UDMFN | 0xE6 | UEIENX: TXINE=0, STALLEDE=1, RXOUTE=2, **RXSTPE=3** | |
| **UEINTX** | **0xE8** | UECONX: EPEN=0, RSTDT=3, STALLRQC=4, **STALLRQ=5** | |
| UENUM | 0xE9 | UECFG0X: EPDIR=0, EPTYPE[1:0]=bit7:6(00=CTRL,01=ISO,10=BULK,**11=INT**) | |
| UERST | 0xEA | UECFG1X: ALLOC=1, EPBK[1:0]=3:2, EPSIZE[2:0]=6:4(0=8B,1=16B,**2=32B**,3=64B) | |
| UECONX | 0xEB | UDINT/UDIEN: SUSPI=0, **SOFI=2**, **EORSTI=3**, WAKEUPI=4, EORSMI=5, UPRSMI=6 | |
| UECFG0X | 0xEC | UESTA0X: **NBUSYBK[1:0]=1:0**, DTSEQ[1:0]=3:2, UNDERFI=5, OVERFI=6, CFGOK=7 | |
| UECFG1X | 0xED | USBCON: USBE=7, FRZCLK=5, VBUSTE=0;UHWCON: UVREGE=0;PLLCSR: PLOCK=0, PLLE=1, PINDIV=4 | |

**UEINTX 语义**(LUFA `Endpoint_AVR8.h` 与 Arduino `USBCore.cpp` 双重印证):中断/状态位
**写 0 清除、写 1 无操作**。清 TXINI = 发送 IN 包;清 RXSTPI = 确认 SETUP;清 FIFOCON|RXOUTI = 释放 OUT bank。
固件的三条 UEINTX 写序列与 Arduino USB core **逐常量一致**(§6 证据)。

---

## 1. 向量表与两个中断源的真实槽位(Q2 结论 + 证据)

### 1.1 表结构

0x0000-0x00AB 共 172 字节 = **43 个 4 字节槽**,每槽一条 `JMP`(940C + 16 位地址),
0x00AC 起才是 SROM 数据 —— 表长与 iom32u4.h 的 43 个向量(_VECTOR(0..42))精确吻合。
avr-gcc 对 >8KB flash 器件用双字 JMP 向量项,故 `_VECTOR(n)` 的字节地址 = **4n**(不是 2n)。
`sensor_protocol.md` 疑点 11 的 "0x0028 按 32U4 标准序是 Timer1_COMPB" 由此解开:那是按 2 字节槽算的。

### 1.2 实测槽内容

- 0x0000: `JMP 0x1178` (RESET → GCC 启动,EOR r1,r1)
- **0x0028: `JMP 0x194A` = _VECTOR(10) = `USB_GEN_vect`(USB General)**
- **0x002C: `JMP 0x198C` = _VECTOR(11) = `USB_COM_vect`(USB Endpoint)**
- 其余 40 槽全部 `JMP 0x11AE`;0x11AE 处是 `JMP 0x0000` —— 未挂接中断的默认处理是**软重启**(不是死循环)。

### 1.3 交叉证据(槽位号 = 中断身份)

- iom32u4.h:`USB_GEN_vect = _VECTOR(10)`、`USB_COM_vect = _VECTOR(11)`;10×4=0x28,11×4=0x2C。✓
- 0x194A 的 ISR 只访问 UDINT(0xE1)/UDIEN 使能位(0x1916 处 `UDIEN=0x08`=EORSTE)→ General 源。✓
- 0x198C 的 ISR 开头 `UENUM=0` + 读 UEINTX.RXSTPI(bit3)→ EP0 SETUP → Endpoint 源。✓
- 全镜像仅这两个 ISR;两个 ISR 内无任何 SPI/PORTB 访问(与 sensor_protocol.md §7 一致)。

### 1.4 fw2 对照

fw1[0x1916..0x1C4E](init+两 ISR+helper)与 fw2[0x1936..0x1C6E] **逐字节相同**;向量表 0x28/0x2C 的
目标地址 fw2 为 0x196A/0x19AC(+0x20)。两固件 USB 行为完全一致。

---

## 2. HID 报告描述符逐条解码(Q1)

位置 0x111B,69 字节(HID 描述符 @0x110B 声明 wDescriptorLength=0x0045 ✓)。逐 item:

| # | 字节 | item | 含义 |
|---|---|---|---|
| 1 | `05 01` | Usage Page (Generic Desktop) | |
| 2 | `09 02` | Usage (Mouse) | |
| 3 | `A1 01` | Collection (Application) | |
| 4 | `05 09` | Usage Page (Button) | |
| 5 | `19 01` | Usage Minimum (Button 1) | |
| 6 | `29 05` | Usage Maximum (Button 5) | **只有 5 个按键** |
| 7 | `15 00` | Logical Min (0) | |
| 8 | `25 01` | Logical Max (1) | |
| 9 | `95 05` | Report Count (5) | |
| 10 | `75 01` | Report Size (1) | |
| 11 | `81 02` | **Input (Data,Var,Abs)** | 字节0 位0-4 = 按键1-5 |
| 12 | `95 01` | Report Count (1) | |
| 13 | `75 03` | Report Size (3) | |
| 14 | `81 03` | **Input (Const,Var,Abs)** | 字节0 位5-7 = 恒 0 填充 |
| 15 | `05 01` | Usage Page (Generic Desktop) | |
| 16 | `09 30` | Usage (X) | |
| 17 | `09 31` | Usage (Y) | |
| 18 | `16 01 80` | Logical Min (**-32767**) | |
| 19 | `26 FF 7F` | Logical Max (32767) | |
| 20 | `36 01 80` | Physical Min (-32767) | |
| 21 | `46 FF 7F` | Physical Max (32767) | |
| 22 | `75 10` | Report Size (16) | |
| 23 | `95 02` | Report Count (2) | |
| 24 | `81 06` | **Input (Data,Var,Rel)** | 字节1-2 = X(int16 LE),字节3-4 = Y |
| 25 | `09 38` | Usage (Wheel) | **滚轮在这里** |
| 26 | `15 81` | Logical Min (-127) | |
| 27 | `25 7F` | Logical Max (127) | |
| 28 | `35 81` | Physical Min (-127) | |
| 29 | `45 7F` | Physical Max (127) | |
| 30 | `75 08` | Report Size (8) | |
| 31 | `95 01` | Report Count (1) | |
| 32 | `81 06` | **Input (Data,Var,Rel)** | 字节5 = 垂直滚轮(int8) |
| 33 | `C0` | End Collection | |

合计 28×2 + 4×3 + 1 = **69 字节** ✓。无 Report ID;无 AC Pan(0x48)→ 没有横向滚轮字节。

### 2.1 IN 报告 6 字节完整布局(与代码互证)

| 字节 | 位 | 内容 | 代码证据 |
|---|---|---|---|
| 0 | bit0-4 | 按键(PD0→bit0 … PD4→bit4,低有效去抖后正逻辑) | `0x176E LDD r20,Y+10; 0x1770 ANDI r20,0x1F; 0x17A8 STS 0xF1(UEDATX),r20` |
| 0 | bit5-7 | 0(填充) | 描述符 item 14 |
| 1 | 全 | **X_L**(int16 LE 低字节,两补码,相对值) | `0x17AC STS 0xF1, r24`(r24=r12=X_L) |
| 2 | 全 | **X_H** | `0x17B0 STS 0xF1, r25`(r25=r13=X_H) |
| 3 | 全 | **Y_L** | `0x17B4 STS 0xF1, r18`(r18=r10=Y_L) |
| 4 | 全 | **Y_H** | `0x17B8 STS 0xF1, r19`(r19=r11=Y_H) |
| 5 | 全 | **垂直滚轮**(int8,相对值) | `0x17BC STS 0xF1, r15`(r15=滚轮累加) |

**第 6 字节 = 滚轮**。sensor 报告里"报告组装只有 5 字节"是因为它没追 r15:组装顺序实际是
按键→X_L→X_H→Y_L→Y_H→**滚轮(r15)**,共 6 字节,与描述符完全对齐。
滚轮来源:PC6/PC7 正交编码器(§7)。

X/Y 是**相对增量**:发送值 = 当前 16kHz 帧增量(r13:r12 / r11:r10),若 EP3 bank 里还有没被主机
取走的旧包,则与旧包内容合并后覆盖写(§6.3),主机侧积分。16 位两补码;描述符逻辑范围为
-32767..+32767(-32768 仍可出现在数据里,主机侧会被钳制,无实际影响)。

---

## 3. 两个 ISR 完整解码(Q2)

### 3.1 USB General ISR @0x194A(fw2: 0x196A)— 总线复位处理

```asm
194A  PUSH r1,r0,SREG,r24
1956  LDS  r24, 0xE1        ; r24 = UDINT(复位期间 EORSTI=bit3 已置位)
195A  STS  0xE1, r1         ; UDINT = 0,清全部 General 标志
195E  SBRS r24, 3           ; EORSTI(End Of Reset)置位?  ← bit3,avrlibc 头证实
1960  RJMP 0x1980           ; 没有 → 直接退出
      ; --- 总线复位:重新配置 EP0(控制端点) ---
1962  STS  0xE9, r1         ; UENUM  = 0
1966  LDI  r24, 0x01
1968  STS  0xEB, r24        ; UECONX = EPEN
196C  STS  0xEC, r1         ; UECFG0X= 0        → EPTYPE=CONTROL, EPDIR=OUT
1970  LDI  r24, 0x22
1972  STS  0xED, r24        ; UECFG1X= ALLOC|EPSIZE=32B,单 bank  ← 与设备描述符 bMaxPacketSize0=32 一致
1976  LDI  r24, 0x08
1978  STS  0xF0, r24        ; UEIENX = RXSTPE(bit3) → EP0 收到 SETUP 产生中断
197C  STS  0x0102, r1       ; 软件标志 0x0102 = 0(总线复位 → 失配状态,等待重新 SET_CONFIGURATION)
1980  POP…RETI
```

**身份:USB General(复位)处理。** 唯一使能的 General 中断源是 `UDIEN=0x08 = EORSTE(bit3)`
(0x1916 初始化里设置),与 `SBRS r24,3` 完全对应。它就是"总线上出现 USB 复位 → 重配 EP0 +
清除已配置标志"的标准模式。

### 3.2 USB Endpoint ISR @0x198C(fw2: 0x19AC)— EP0 控制传输状态机

```asm
198C  PUSH r1,r0,SREG,r18-r27,r28,Y,Z
19B0  STS  0xE9, r1         ; UENUM = 0(选 EP0)
19B4  LDS  r24, 0xE8        ; UEINTX
19B8  SBRS r24, 3           ; RXSTPI(收到 SETUP)?
19BA  RJMP 0x1A3C           ; 没有 → 退出(经 UECONX=0x21)
19BC  LDS  r22, 0xF1        ; bmRequestType → r22
19C0  LDS  r24, 0xF1        ; bRequest     → r24
19C4  LDS  r18, 0xF1        ; wValueL      → r18
19C8  LDS  r25, 0xF1        ; wValueH      → r25(→r19)
19D0  LDS  r20, 0xF1        ; wIndexL      → r20
19D4  LDS  r25, 0xF1        ; wIndexH      → r25(→r21)
19DC  LDS  r23, 0xF1        ; wLengthL     → r23
19E0  LDS  r28, 0xF1        ; wLengthH     → r28
19E4  LDI  r25, 0xF2
19E6  STS  0xE8, r25        ; UEINTX = 0xF2 = ~(RXSTPI|RXOUTI|TXINI) —— 与 Arduino USBCore 的
                            ; ClearSetupInt() 常量逐位相同(0xF2)
19EA  CPI  r24, 6           ; ┌ bRequest 分发(见 §5)
      …
1A3C  LDI  r24, 0x21
1A3E  STS  0xEB, r24        ; UECONX = EPEN|STALLRQ → 对不支持的请求 STALL
1A42  POP…RETI
```

**身份:USB Endpoint(EP0 控制)处理。** EP3(报告端点)**没有使能任何端点中断**
(SET_CONFIGURATION 配置它时只写 UECONX/UECFG0X/UECFG1X,从不写 UEIENX),所以该 ISR
实际只服务 EP0 的 SETUP。注意 8 字节 SETUP 是 8 条 `LDS 0xF1`(UEDATX FIFO 弹出)读的,
不是 SRAM 拷贝 —— sensor 报告里"0x19B0+ 端点 FIFO 0x00E8-0x00EE"的说法实为
**UEINTX..UESTA0X 寄存器窗口**,不是软件缓冲。

---

## 4. USB 初始化 @0x1916 逐条分析(Q3)

```asm
1916  LDI r24, 0x01
1918  STS 0xD7, r24     ; UHWCON = UVREGE          —— USB 垫整流器使能
191C  LDI r24, 0xA0
191E  STS 0xD8, r24     ; USBCON = USBE|FRZCLK     —— 使能控制器、冻结 USB 时钟
1922  LDI r24, 0x12
1924  OUT 0x29, r24     ; PLLCSR = PINDIV|PLLE     —— 16MHz/2=8MHz 入 PLL(×6→48MHz),启动 PLL
1926  IN  r0, 0x29      ; ┌ r0 = PLLCSR
1928  SBRC r0, 0        ; │ 若 PLOCK==0 → 跳过下一条 → 落到 0x192C
192A  RJMP 0x1926       ; ┘ PLOCK==1 → 继续轮询
      ; ★ 方向反了!实际语义是"PLOCK 为 1 就一直等、为 0 立即通过" —— 开机瞬间 PLOCK=0,
      ;   所以这个等待环是空转:固件不等 PLL 锁定就继续(无害:主机枚举 ≥100ms,PLL <1ms 锁定)。
192C  LDI r24, 0x90
192E  STS 0xD8, r24     ; USBCON = USBE|bit4(解冻时钟;bit4 在 iom32u4.h 未定义,模板残留〔推断〕)
1932  STS 0xE0, r1      ; UDCON = 0 → DETACH=0,上拉 D+,接入总线(全速:LSM=0)
1936  STS 0x0102, r1    ; 软件标志 = 0(未配置)
193A  LDI r24, 0x08
193C  STS 0xE2, r24     ; UDIEN = EORSTE —— 只使能"总线复位"这一种 General 中断
1940  BSET 0            ; SEI(开全局中断;主循环每帧还会再 SEI/CLI,见 §8)
1942  RET

1944  LDS r24, 0x0102   ; helper:读"已配置"软件标志
1948  RET
```

**端点配置不在 init 里**:init 只做 PLL/USBCON/attach/中断使能。EP0 的配置全部由
USB_General ISR 在**总线复位事件**里完成(§3.1);EP3 的配置在 **SET_CONFIGURATION** 里完成(§5.2)。
初始化顺序与 LUFA `USB_Init` 同构(USBE|FRZCLK → PLL → 解冻 → attach),仅 PLOCK 等待方向相反。

---

## 5. 控制传输(EP0)支持矩阵(Q5)

### 5.1 分发逻辑(bRequest 为主,部分校验 bmRequestType)

USB_COM ISR 读入 8 字节 SETUP 后按 bRequest 分发(0x19EA-0x1A00、0x1B66-0x1B70、0x1ABE-0x1AD4):

| bRequest | bmRequestType | 行为 | 地址 |
|---|---|---|---|
| 0x00 GET_STATUS | 0x80(device) | 回 `{0x00,0x00}`(非自供电、无远程唤醒,与配置描述符 bmAttributes=0xC0 一致) | 0x1A10-0x1A1C |
| | 0x81(interface) | 同样回 `{0,0}`(接口状态恒 0,正确) | 同上 |
| | 0x82(endpoint) | `UENUM=wIndexL`(硬件截取低 3 位→EP3),读 UECONX.STALLRQ(bit5) 回 `{halt,0}` | 0x1C3A-0x1C4C |
| 0x01 CLEAR_FEATURE | 0x02, wValue=0(ENDPOINT_HALT), EP1-4 | `UECONX=0x19=EPEN|RSTDT|STALLRQC` 解除 STALL + 复位数据触发,再 `UERST=1<<ep` 脉冲复位该端点 toggle | 0x1B70-0x1BB2 |
| 0x03 SET_FEATURE | 0x02, wValue=0, EP1-4 | 走 STALL 出口(`UECONX=0x21=EPEN|STALLRQ`)→ 即"暂停该端点" | 0x1B92-0x1B96→0x1A3C |
| 0x05 SET_ADDRESS | (不查) | 先 `UEINTX=0xFE` 发状态 ZLP → 等 TXINI(主机收完)→ `UDADDR = wValue|0x80(ADDEN)` | 0x1BB4-0x1BC8 |
| 0x06 GET_DESCRIPTOR | (不查) | 查 7 项 flash 表(0x10AA-0x10DA,每项 {wValue,wIndex,offset,len} 7 字节,wValue+wIndex 双匹配) | 0x1A22-0x1B64 |
| 0x08 GET_CONFIGURATION | 0x80 | 回 `SRAM 0x0102`(当前配置值) | 0x1AB8-0x1BE0 |
| 0x09 SET_CONFIGURATION | 必须 0x00 | `0x0102 = wValueL`(★解除主循环等待);配 EP1-4 + `UERST=0x1E` 脉冲;状态 ZLP | 0x1A66-0x1AB6 |
| 类请求 0x21 SET_PROTOCOL | 0x21, bRequest=0x0B | `SRAM 0x0100 = wValueL`(0=boot/1=report)→ **接受但主循环从不读它**(§8.4) | 0x1AD0-0x1AE0 |
| 类请求 0xA1 GET_PROTOCOL | 0xA1, bRequest=0x03 | 回 `SRAM 0x0100` 一个字节(.data 初始化为 0x01=report 协议) | 0x1BF6-0x1C0C |
| 类请求 0xA1 GET_REPORT | 0xA1, bRequest=0x01 | 写 **6 个 0x00** 到 UEDATX 后发送 | 0x1C0E-0x1C34 |
| 其它(SET_DESCRIPTOR 7、GET/SET_INTERFACE 10/11、HID SET_REPORT 0x09/SET_IDLE 0x0A/GET_IDLE 0x02、同步帧 0x0C…) | — | **STALL**(`UECONX=0x21`) | 0x1A3C |

**结论:支持 boot protocol 协商(SET/GET_PROTOCOL),不支持 SET_REPORT/SET_IDLE/GET_IDLE。**
HID 类描述符(HID @0x110B)和报告描述符(0x111B)都能通过 GET_DESCRIPTOR 提供。

### 5.2 描述符表(0x10AA 起,7 项 × 7 字节 {wValueL,wValueH,wIndexL,wIndexH,offL,offH,len})

| wValue | wIndex | 目标 | 长度 |
|---|---|---|---|
| 0x0100 DEVICE | 0x0000 | 0x1160 | 18 |
| 0x0200 CONFIG | 0x0000 | 0x10F9 | 34 |
| 0x2200 REPORT | 0x0000 | 0x111B | 69 |
| 0x2100 HID(类) | 0x0000 | 0x110B | 9 |
| 0x0300 STRING0 | 0x0000 | 0x10F5 | 4(语言 0x0409) |
| 0x0301 STRING1 | 0x0409 | 0x10EB | 8 → **"Bst"**(厂商) |
| 0x0302 STRING2 | 0x0409 | 0x10DB | 14 → **"Public"**(产品) |

即 iManufacturer="Bst"、iProduct="Public"(设备描述符 @0x1160:iManufacturer=1, iProduct=2,无序列号,bcdDevice=0x0100,bcdUSB=0x0200)。

### 5.3 描述符多包发送(0x1B16-0x1B64)

发送长度 = min(wLength, 描述符长,wLength>0xFF 时钳到 0xFF);按 **32 字节一块**(= EP0 FIFO 大小)
填 UEDATX,每包前等 `UEINTX & (TXINI|RXOUTI)`(0x1B1E,与 Arduino `WaitForINOrOUT()` 同款),
若期间主机发 OUT(放弃传输)则中止(0x1B22 测 RXOUTI);若总长恰为 32 的倍数补发 ZLP(0x1B56-0x1B58)。
每包 `UEINTX=0xFE` = ~TXINI 发送(= Arduino `ClearIN()`)。

### 5.4 SET_CONFIGURATION 的端点配置表(0x1A74-0x1AB6)

循环 UENUM=1..4,从 flash 表 0x1172 顺序读 3 字节项 {UECONX, UECFG0X, UECFG1X},遇 0 跳过:

```
0x1172: 00 00 01 C1 02 00     →  EP1: 禁用, EP2: 禁用, EP3: {0x01, 0xC1, 0x02}, EP4: 禁用
```

**EP3 = UECONX 0x01(EPEN)| UECFG0X 0xC1(EPTYPE=11 中断 + EPDIR=1 IN)| UECFG1X 0x02(ALLOC,EPSIZE=000 → 8 字节 FIFO,单 bank)**
—— 与端点描述符(0x1114:`07 05 83 03 06 00 01`,EP3 IN 中断 wMaxPacketSize=6 bInterval=1ms)一致,
8 字节 FIFO ≥ 6 字节报告。随后 `UERST=0x1E`(复位 EP1-4)→ `UERST=0`。**EP3 不使能任何中断(轮询驱动)**。

---

## 6. 传输路径:6 字节报告从传感器到 UEDATX(Q4)

### 6.1 全链路

```
16kHz Timer0 帧(62.5µs)
 └ 0x163A-0x168E Motion_Burst 读 4 字节增量 → r12/r13=X_L/X_H, r10/r11=Y_L/Y_H
 └ 0x1690-0x16A8+0x1856 滚轮正交解码(PC6/PC7)→ r15 = 本帧 ±1
 └ 0x16AA-0x17F6 按键去抖(PD0-5,32 帧/2ms)→ Y+10 位图
 └ 0x1754-0x1778 SEI→轮询 0x0102(已配置?)→CLI
 └ 0x186C 触发判定:按键变化 ∨ X≠0 ∨ Y≠0 ∨ 滚轮≠0,否则不发
 └ 0x177C/0x1882 UENUM=3;LDS UESTA0X 测 NBUSYBK0(bit0)
     ├ bank 空闲(bit0=0)→ 0x178A:r24:r25 = 0+帧X, r19:r18 = 0+帧Y("fresh")
     └ bank 占用(bit0=1)→ 0x1890:r24:r25 = r7:r2+帧X, r19:r18 = r6/r5+帧Y,
                            r20 |= r14(按键并集), r15 += r3(滚轮累加)("merge")
 └ 0x1792-0x17A0 结果回存 r2/r7(X)、r6/r5(Y)
 └ 0x17A8-0x17BC STS UEDATX ×6:r20(按键), r24, r25, r18, r19, r15
 └ 0x17C2 STS UEINTX, 0x3A = ~(TXINI|RXOUTI|NAKINI|FIFOCON) → 提交发送
      (= Arduino USBCore ReleaseTX() 的常量 0x3A,逐位相同)
 └ 0x17C6-0x17CA 保存"已发送值"到 r3/r4/r14
```

**EP3 全程轮询驱动**:无 TXINE 中断,主循环每帧直接写寄存器。若 1ms 内主机尚未轮询
(NBUSYBK=1),新数据**合并进未取走的包**(fresh 增量 + 旧包内容),下一帧覆盖 —— 位移不丢,
主机每次拿到"自上一个被取走包以来的累计增量"。NBUSYBK 判据(UESTA0X bit0)对单 bank IN 端点
0=空闲/1=有未取走包〔该位语义按 iom32u4.h,行为与代码自洽,标注推断见 §10〕。

### 6.2 与"1ms 端点轮询"的关系

bInterval=1ms:主机每毫秒发一次 IN;固件每 62.5µs 刷新一次 bank 内容。稳态=主机以 1000Hz
收到"最新合并状态";静止时主循环不发(NBUSYBK=0 且无变化 → 0x1880 → 0x17CC 跳过发送)。

### 6.3 UEINTX 三条写序列与 Arduino core 对照(证据)

| 固件 | 常量 | 清除的位 | Arduino USBCore.cpp 对应 |
|---|---|---|---|
| 0x19E6(SETUP 后) | `0xF2` | RXSTPI, RXOUTI, TXINI | `ClearSetupInt(): UEINTX = ~((1<<RXSTPI)\|(1<<RXOUTI)\|(1<<TXINI))` |
| 0x1A1C 等(IN 发送) | `0xFE` | TXINI | `ClearIN(): UEINTX = ~(1<<TXINI)` |
| 0x17C2(EP3 提交) | `0x3A` | TXINI, RXOUTI, NAKINI, FIFOCON | `ReleaseTX(): UEINTX = 0x3A`(注释逐位一致) |

---

## 7. 滚轮解码(第 6 字节来源)

- 引脚:**PC6/PC7**(0x1690 `IN r24,0x06`=PINC;位 6→r24=A 相,经 `ADC r25,r25` 移位取位 7→r25=B 相;0x11DC-0x11E0 给 PC6/7 上拉)。
- 状态机(0x1690-0x16A8 与 0x1856-0x186A):每帧采 A/B;A==B(稳态)时与上帧 A(Y+9)比较,变了就输出 `r15 = ((Y+8^A)<<1)-1` = **±1**(方向 = A 相对上一过渡态 Y+8 是否翻转),并存 Y+9=A;A≠B(过渡态)时把 A 存 Y+8。Y+8/Y+9 开机清零(0x15B0-0x15B2)。
- 即:每个正交半态输出 ±1(整周期 2 个计数)〔每机械刻度出 1 还是 2 个计数取决于编码器物理结构,未实测,推断〕;r15 为本帧增量,发送路径见 §6.1。

---

## 8. 主循环与 USB 的耦合(Q6)

### 8.1 SRAM 0x0102 的真身

**不是寄存器**。0x0102 是 .bss 里的 1 字节软件标志("USB 当前配置值"),0x0100/0x0101 是 .data
2 字节(0x0100=协议标志,初值 0x01=report;0x0101 初始化后从未被访问,死数据)。0x0102 的读写全集:

| 动作 | 地址 |
|---|---|
| 写 0:USB init(0x1936)、总线复位 ISR(0x197C) | |
| 写 wValueL:SET_CONFIGURATION(0x1A6A)——**唯一的置位点** | |
| 读:helper 0x1944(启动等待 0x157E-0x1582 + 主循环每帧 0x1766)、GET_CONFIGURATION(0x1BD2) | |

任务前提"主循环轮询 SRAM 0x0102 = UDIEN"不成立:UDIEN 实际在 0xE2(且固件从不读它)。

### 8.2 等待与中断窗口

- 启动:USB init → **0x157E-0x1582 循环等待 0x0102≠0(即等到主机 SET_CONFIGURATION)→ 455ms 延时 → Timer0 配置 → 主循环**。
- 主循环**每帧**经过 0x1754-0x176C:`SEI → RCALL 0x1944(0x0102≠0?) → CLI`。未配置时该环常开中断跑,枚举的 EP0 中断都在这里被服务;已配置后 SEI 窗口仅 ~5 个周期 —— **之后的 EP0 控制传输靠标志悬挂,在下一次 SEI 窗口(≤62.5µs 后)被服务**,一次 SETUP 的一次 ISR 内完成整个控制传输(含内部 TXINI 等待),不回主循环。
- 报告发送、传感器 SPI、滚轮/按键扫描都在 CLI 之下,不受 EP0 传输打断;控制传输期间主循环暂停,暂停期位移由传感器增量寄存器自然累积,不丢。

### 8.3 SOF 同步(0x18F0-0x190A)

每 8 个报告(报告计数 r16==0)执行:`UDINT &= ~0xFB`(清 **SOFI=bit2**;注意 avrlibc 的 32U4 位序
SOFI=2,不是常见 AT90 系的 1)→ 忙等下一个 SOFI(bit2 置位)→ `GTCCR|=PSR0` + `TCNT0=0` 重置
Timer0 → 把 16kHz 采样节拍相位对齐 USB 1ms 帧时钟。等待 ≤1ms,不阻塞功能。

### 8.4 boot protocol 的实际效果

SET_PROTOCOL 接受并存储(0x0100),GET_PROTOCOL 能回读,但**主循环从不读 0x0100,报告格式恒为
6 字节 report 格式**。若 BIOS/主机切到 boot protocol(接口声明了 boot+mouse),收到的第 2/3 字节
将是 X_H/Y_L 而非 boot 约定的 Y —— 桌面系统不切 boot 协议,故无实际影响;这是一个原样存在的怪癖。

### 8.5 挂起/恢复/复位

无 SUSP/SUSPE 处理,不支持远程唤醒(GET_STATUS 回 0,bmAttributes 无 bit5)。挂起期间主循环照常
跑、报告进 bank 不被取走(merge 路径吞掉增量);恢复后主机继续轮询即收到合并包,无需重枚举。
只有总线复位(EORSTI)会清 0x0102 并强制主循环重新等待 SET_CONFIGURATION。

---

## 9. QMK 移植要点(Q7)

要"USB 行为一致",需要复刻的**硬约束**:

1. **报告格式**:6 字节无 Report ID = `[buttons(5bit)|pad3, X int16 LE, Y int16 LE, Wheel int8]`。
   QMK 的默认 `report_mouse_t` 是 5 字节(buttons,x8,y8,wheel8,pan8),16 位 X/Y 需开
   `MOUSE_EXTENDED_REPORT`(布局为 buttons,x16,y16,h8,v8,7 字节且含 pan)——**与原固件不同**。
   要逐字节一致,最稳妥是**原样复用这 69 字节报告描述符**(直接拷 0x111B 起的字节),并定义
   对应 6 字节结构体;X/Y 直接喂 PMW3360 的 16 位增量,不取 8 位截断。
2. **端点**:全速;EP0 控制最大包 **32**;报告端点用 **EP3 IN、interrupt、wMaxPacketSize=6、
   bInterval=1(1ms)**;描述符集可整体照抄(0x10F9-0x1177):配置 34B、HID bcdHID=0x0111、
   bmAttributes=0xC0、bMaxPower=0x32(100mA)、VID 0x16C0 / PID 0x047E、字符串 "Bst"/"Public"。
3. **上报时机**:有变化才发(按键变化 ∨ ΔX ∨ ΔY ∨ 滚轮非零);QMK 默认行为即如此,若要完全
   等价可在"上一包未被取走"时合并增量(QMK 的 SOFT_SERIAL/轮询架构下通常天然 1ms 一次,
   直接发本周期累计值即可,主机侧效果一致)。
4. **控制请求**:SET/GET_PROTOCOL 要接受(存变量即可);GET_REPORT 回 6 字节 0;**SET_IDLE/
   SET_REPORT 保持 STALL 也能工作**(主机不依赖);EP 停止(CLEAR/SET_FEATURE endpoint halt)
   至少别让主机死等。
5. **行为怪癖是否复刻**(建议:不复刻,但要知情):
   - boot 协议切换不改格式(§8.4);
   - PLOCK 等待方向反(§4)——QMK/LUFA 写法自然正确;
   - 无 suspend 处理、无远程唤醒;
   - PD5 第 6 键不上报(只有 5 个按键位)。
6. **按键映射**:PD0→bit0(左)、PD1→bit1(右)、PD2→bit2(中)、PD3→bit3、PD4→bit4、PD5→不上报
   〔键位名称按常见布局推断,丝印未验证〕;按键低有效、内部上拉、2ms 去抖。
7. **滚轮**:PC6/PC7 正交,QMK 用 encoder 或手写状态机,每半态 ±1。

---

## 10. 未实锤点(全部标"推断")

1. **UESTA0X bit0=NBUSYBK0 作为"bank 有未取走包"判据**(0x1786/0x188C):位定义按 iom32u4.h
   无误,且与 merge/fresh 逻辑、单 bank 硬件行为自洽,但未在硬件上抓包验证。
2. **滚轮方向表**:±1/半态确定,但顺时针对应 +1 还是 -1 未实测(依赖编码器接线)。
3. **每机械刻度的计数个数**(1 或 2)取决于编码器物理结构,镜像内不可见。
4. **EP0 SETUP-ack 清 TXINI(0xF2)的硬件内部机制**:序列与 Arduino/LUFA 完全一致故行为等价,
   但"清 TXINI 不立即发包"的硅内规则未逐字对照数据手册原文。
5. **PD5(第 6 键)**只进内部位图(Y+10 bit5)且被 `ANDI 0x1F` 挡在报告外,用途不明
   (预留/内部功能键)〔推断〕。
6. **r2-r7 每 8 报告轮换**(0x15CE-0x15DE、0x17D8-0x17E4)的精确意图(疑似 GCC 对小型循环
   缓冲的寄存器分配),不影响已建立的行为模型。
7. **USBCON bit4(0x90 中)**:iom32u4.h 未定义该位,疑为厂商模板残留,无观察到的影响。
8. **PLL 等待环方向相反**(0x1928 SBRC)是有意还是笔误无法判定;可观察到的事实是"开机不等锁"。
9. **滚轮/按键的物理丝印对应关系**(左/右/中/侧键分配)按惯例推断。
10. 0x0101 字节初始化为 0 后全镜像无访问 —— 死数据〔推断,已全量 grep 证实〕。

## 11. 关键地址速查

| 内容 | 地址 |
|---|---|
| 设备/配置/接口/HID/端点描述符 | 0x1160 / 0x10F9 / 0x1102 / 0x110B / 0x1114 |
| 报告描述符(69B) | 0x111B |
| 字符串(语言/Bst/Public) | 0x10F5 / 0x10DB / 0x10EB |
| 描述符查找表(7×7B) | 0x10AA |
| SET_CONFIG 端点配置表(6B) | 0x1172 |
| USB init | 0x1916 |
| 标志读取 helper(LDS 0x0102) | 0x1944 |
| USB_General ISR(复位处理) | 0x194A |
| USB_Endpoint ISR(EP0 状态机) | 0x198C-0x1C4C |
| 主循环:发送判定/发送/提交 | 0x186C+0x177C / 0x17A8-0x17BC / 0x17C2 |
| 软件标志:0x0100=协议,0x0102=已配置值 | SRAM |
