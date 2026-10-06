#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ATmega32U4 固件反汇编器(鼠标逆向专用)。
用法: python avrdump.py <firmware.hex> <out.dis>
输出: 带注释反汇编 + .bin 原始镜像 + I/O 访问汇总(stdout)
"""
import sys
from collections import defaultdict

def load_hex(path):
    ext = 0; base = 0; chunks = {}
    for line in open(path, encoding='ascii', errors='ignore'):
        line = line.strip()
        if not line.startswith(':'): continue
        d = bytes.fromhex(line[1:])
        cnt, ah, al, rt = d[0], d[1], d[2], d[3]
        if rt == 0:
            a = ext*0x10000 + base + ((ah << 8) | al)
            for k, b in enumerate(d[4:4+cnt]):
                chunks[a+k] = b
        elif rt == 2:
            ext = int.from_bytes(d[4:6], 'big')
        elif rt == 4:
            base = int.from_bytes(d[4:6], 'big') << 16
        elif rt == 1:
            break
    hi = max(chunks)
    return bytes(chunks[i] for i in range(hi+1))

# SRAM 地址 -> 寄存器名 (I/O 指令地址 = SRAM - 0x20)
SRAM_NAMES = {
    0x23:'PINB',0x24:'DDRB',0x25:'PORTB',0x26:'PINC',0x27:'DDRC',0x28:'PORTC',
    0x29:'PIND',0x2A:'DDRD',0x2B:'PORTD',0x2C:'PINE',0x2D:'DDRE',0x2E:'PORTE',
    0x2F:'PINF',0x30:'DDRF',0x31:'PORTF',
    0x35:'TIFR0',0x36:'TIFR1',0x3B:'PCIFR',0x3C:'EIFR',0x3D:'EIMSK',0x3E:'GPIOR0',
    0x3F:'EECR',0x40:'EEDR',0x41:'EEARL',0x42:'EEARH',0x43:'GTCCR',
    0x44:'TCCR0A',0x45:'TCCR0B',0x46:'TCNT0',0x47:'OCR0A',0x48:'OCR0B',
    0x49:'PLLCSR',0x4A:'GPIOR1',0x4B:'GPIOR2',
    0x4C:'SPCR',0x4D:'SPSR',0x4E:'SPDR',
    0x50:'ACSR',0x51:'OCDR',0x52:'PLLFRQ',0x53:'SMCR',0x54:'MCUSR',0x55:'MCUCR',
    0x57:'SPMCSR',0x5B:'RAMPZ',0x5D:'SPL',0x5E:'SPH',0x5F:'SREG',
    0x60:'WDTCSR',0x61:'CLKPR',0x64:'PRR0',0x65:'PRR1',0x66:'OSCCAL',
    0x68:'PCICR',0x69:'EICRA',0x6A:'EICRB',0x6B:'PCMSK0',
    0x6E:'TIMSK0',0x6F:'TIMSK1',0x71:'TIMSK3',0x72:'TIMSK4',
    0x78:'ADCL',0x79:'ADCH',0x7A:'ADCSRA',0x7B:'ADCSRB',0x7C:'ADMUX',
    0x7E:'DIDR0',0x7F:'DIDR1',
    0x80:'TCCR1A',0x81:'TCCR1B',0x82:'TCCR1C',0x84:'TCNT1L',0x85:'TCNT1H',
    0x86:'ICR1L',0x87:'ICR1H',0x88:'OCR1AL',0x89:'OCR1AH',
    0x8A:'OCR1BL',0x8B:'OCR1BH',0x8C:'OCR1CL',0x8D:'OCR1CH',
    0x90:'TCCR3A',0x91:'TCCR3B',0x92:'TCCR3C',0x94:'TCNT3L',0x95:'TCNT3H',
    0x96:'ICR3L',0x97:'ICR3H',0x98:'OCR3AL',0x99:'OCR3AH',
    0x9A:'OCR3BL',0x9B:'OCR3BH',0x9C:'OCR3CL',0x9D:'OCR3CH',
    0xC0:'TCCR4A',0xC1:'TCCR4B',0xC2:'TCCR4C',0xC3:'TCCR4D',0xC4:'TCCR4E',
    0xC5:'CLKSEL0',0xC6:'CLKSEL1',0xC7:'CLKSTA',
    0xC8:'UCSR1A',0xC9:'UCSR1B',0xCA:'UCSR1C',0xCC:'UBRR1L',0xCD:'UBRR1H',0xCE:'UDR1',
    0xCF:'OCR4A',0xD0:'OCR4B',0xD1:'OCR4C',0xD2:'OCR4D',0xD4:'DT4',
    0xF8:'UHWCON',0xF9:'USBCON',0xFA:'USBSTA',0xFB:'USBINT',
    0x100:'UDCON',0x101:'UDINT',0x102:'UDIEN',0x103:'UDADDR',
    0x104:'UDFNUML',0x105:'UDFNUMH',0x106:'UDMFN',
    0x108:'UEINTX',0x109:'UENUM',0x10A:'UERST',0x10B:'UECONX',
    0x10C:'UECFG0X',0x10D:'UECFG1X',0x10E:'UESTA0X',0x10F:'UESTA1X',
    0x110:'UEIENX',0x111:'UEDATX',0x112:'UEBCLX',0x113:'UEBCHX',0x114:'UEINT',
}

PORT_LETTERS = "BCDEF"

def port_kind(io):
    """I/O 地址 3..0x11 -> (字母, 'PIN'|'DDR'|'PORT') 或 None"""
    if 3 <= io <= 0x11:
        idx, kind = (io-3)//3, (io-3)%3
        return PORT_LETTERS[idx], ("PIN","DDR","PORT")[kind]
    return None

BRBS = {0:'BRCS',1:'BREQ',2:'BRMI',3:'BRVS',4:'BRLT',5:'BRHS',6:'BRTS',7:'BRIE'}
BRBC = {0:'BRCC',1:'BRNE',2:'BRPL',3:'BRVC',4:'BRGE',5:'BRHC',6:'BRTC',7:'BRID'}

def sgn(v, bits):
    return v - (1 << bits) if v >= (1 << (bits-1)) else v

def disassemble(img):
    nwords = len(img)//2
    def W(i): return img[2*i] | (img[2*i+1] << 8)
    info = [None]*nwords        # (mn, size, dests, srcs, kind, addr_arg)
    xrefs = defaultdict(list)
    i = 0
    while i < nwords:
        w = W(i)
        mn = None; size = 1; dests = []; srcs = []; kind = 'c'; arg = None
        if w == 0x0000:
            mn = 'NOP'
        elif (w & 0xFF00) == 0x0100:
            d = (w>>4)&0xF; r = w&0xF
            mn = f"MOVW r{2*d}:r{2*d+1}, r{2*r}:r{2*r+1}"
            dests = [2*d, 2*d+1]; srcs = [2*r, 2*r+1]
        elif (w & 0xFF00) == 0x0200:
            d = 16+((w>>4)&0xF); r = 16+(w&0xF)
            mn = f"MULS r{d}, r{r}"; dests = [0,1]; srcs = [d,r]
        elif (w & 0xFF88) == 0x0300:
            d = 16+((w>>4)&7); r = (16 if (w & 0x80) else 0)+(w&7)
            names = {0x00:'FMULSU',0x08:'FMUL',0x80:'MULSU',0x88:'FMULS'}
            mn = f"{names[w & 0x88]} r{d}, r{r}"; dests = [0,1]
        elif (w & 0xFC00) == 0xF000:
            s = w&7; k = sgn((w>>3)&0x7F, 7)
            mn = f"{BRBS[s]} 0x{(i+1+k)*2:04X}"; xrefs[i+1+k].append(i)
        elif (w & 0xFC00) == 0xF400:
            s = w&7; k = sgn((w>>3)&0x7F, 7)
            mn = f"{BRBC[s]} 0x{(i+1+k)*2:04X}"; xrefs[i+1+k].append(i)
        elif (w & 0xFE08) == 0xFC00:
            mn = f"SBRC r{(w>>4)&0x1F}, {w&7}"
        elif (w & 0xFE08) == 0xFE08:
            mn = f"SBRS r{(w>>4)&0x1F}, {w&7}"
        elif (w & 0xFF00) == 0x9600:
            K = ((w>>6)&3)<<4 | (w&0xF); d = 24+2*((w>>4)&3)
            mn = f"ADIW r{d}:r{d+1}, 0x{K:02X}"; dests = [d,d+1]
        elif (w & 0xFF00) == 0x9700:
            K = ((w>>6)&3)<<4 | (w&0xF); d = 24+2*((w>>4)&3)
            mn = f"SBIW r{d}:r{d+1}, 0x{K:02X}"; dests = [d,d+1]
        elif (w & 0xFF00) == 0x9A00:
            A = (w>>3)&0x1F; mn = f"SBI 0x{A:02X}, {w&7}"; arg = ('sbi', A, w&7)
        elif (w & 0xFF00) == 0x9800:
            A = (w>>3)&0x1F; mn = f"CBI 0x{A:02X}, {w&7}"; arg = ('cbi', A, w&7)
        elif (w & 0xFF00) == 0x9900:
            A = (w>>3)&0x1F; mn = f"SBIC 0x{A:02X}, {w&7}"; arg = ('sbic', A, w&7)
        elif (w & 0xFF00) == 0x9B00:
            A = (w>>3)&0x1F; mn = f"SBIS 0x{A:02X}, {w&7}"; arg = ('sbis', A, w&7)
        elif (w & 0xFE0F) == 0x920F:
            r = (w>>4)&0x1F; mn = f"PUSH r{r}"; srcs = [r]
        elif (w & 0xFE0F) == 0x900F:
            r = (w>>4)&0x1F; mn = f"POP r{r}"; dests = [r]
        elif (w & 0xFE0F) == 0x9000:
            r = (w>>4)&0x1F; k = W(i+1); size = 2
            mn = f"LDS r{r}, 0x{k:04X}"; dests = [r]; arg = ('mem_ld', k)
        elif (w & 0xFE0F) == 0x9200:
            r = (w>>4)&0x1F; k = W(i+1); size = 2
            mn = f"STS 0x{k:04X}, r{r}"; srcs = [r]; arg = ('mem_st', k)
        elif (w & 0xFE0F) in (0x9001,0x9002,0x9004,0x9005,0x9006,0x9007,0x9008,0x9009,0x900A,0x900C,0x900D,0x900E):
            r = (w>>4)&0x1F
            names = {1:'LD r{}, Z',2:'LD r{}, -Z',4:'LPM r{}, Z',5:'LPM r{}, Z+',
                     6:'ELPM r{}, Z',7:'ELPM r{}, Z+',8:'LD r{}, Y',9:'LD r{}, Y+',
                     10:'LD r{}, -Y',12:'LD r{}, X',13:'LD r{}, X+',14:'LD r{}, -X'}
            mn = names[w & 0xF].format(r); dests = [r]
        elif (w & 0xFE0F) in (0x9201,0x9202,0x9208,0x9209,0x920A,0x920C,0x920D,0x920E):
            r = (w>>4)&0x1F
            names = {1:'ST Z, r{}',2:'ST -Z, r{}',8:'ST Y, r{}',9:'ST Y+, r{}',
                     10:'ST -Y, r{}',12:'ST X, r{}',13:'ST X+, r{}',14:'ST -X, r{}'}
            mn = names[w & 0xF].format(r); srcs = [r]
        elif (w & 0xF800) == 0xB000:
            A = ((w>>9)&3)<<4 | (w&0xF); r = (w>>4)&0x1F
            mn = f"IN r{r}, 0x{A:02X}"; dests = [r]; arg = ('in', A)
        elif (w & 0xF800) == 0xB800:
            A = ((w>>9)&3)<<4 | (w&0xF); r = (w>>4)&0x1F
            mn = f"OUT 0x{A:02X}, r{r}"; srcs = [r]; arg = ('out', A)
        elif (w & 0xF000) == 0xE000:
            d = 16+((w>>4)&0xF); K = ((w>>4)&0xF0) | (w&0xF)
            mn = f"LDI r{d}, 0x{K:02X}"; dests = [d]
        elif (w & 0xF000) in (0x3000,0x4000,0x5000,0x6000,0x7000):
            d = 16+((w>>4)&0xF); K = ((w>>4)&0xF0) | (w&0xF)
            nm = {0x3000:'CPI',0x4000:'SBCI',0x5000:'SUBI',0x6000:'ORI',0x7000:'ANDI'}[w & 0xF000]
            mn = f"{nm} r{d}, 0x{K:02X}"
            if nm != 'CPI':
                dests = [d]; srcs = [d]
            else:
                srcs = [d]
        elif (w & 0xFC00) == 0x9C00:
            d = (w>>4)&0x1F; r = ((w>>5)&0x10)|(w&0xF)
            mn = f"MUL r{d}, r{r}"; dests = [0,1]; srcs = [d,r]
        elif (w & 0xF000) == 0xC000:
            k = sgn(w & 0xFFF, 12)
            mn = f"RJMP 0x{(i+1+k)*2:04X}"; xrefs[i+1+k].append(i)
        elif (w & 0xF000) == 0xD000:
            k = sgn(w & 0xFFF, 12)
            mn = f"RCALL 0x{(i+1+k)*2:04X}"; xrefs[i+1+k].append(i)
        elif (w & 0xFE0E) == 0x940C:
            k = ((((w>>4)&0x1F)<<1)|(w&1))<<16 | W(i+1); size = 2
            mn = f"JMP 0x{k*2:04X}"; xrefs[k].append(i)
        elif (w & 0xFE0E) == 0x940E:
            k = ((((w>>4)&0x1F)<<1)|(w&1))<<16 | W(i+1); size = 2
            mn = f"CALL 0x{k*2:04X}"; xrefs[k].append(i)
        elif w == 0x9409: mn = 'IJMP'
        elif w == 0x9509: mn = 'ICALL'
        elif w == 0x9419: mn = 'EIJMP'
        elif w == 0x9519: mn = 'EICALL'
        elif w == 0x9508: mn = 'RET'
        elif w == 0x9518: mn = 'RETI'
        elif w == 0x9588: mn = 'SLEEP'
        elif w == 0x9598: mn = 'BREAK'
        elif w == 0x95A8: mn = 'WDR'
        elif w == 0x95C8: mn = 'LPM'
        elif (w & 0xFF8F) == 0x9408: mn = f"BSET {w&7}"
        elif (w & 0xFF8F) == 0x9488: mn = f"BCLR {w&7}"
        elif (w & 0xFE0F) == 0x9400:
            d = (w>>4)&0x1F; s = w&0xF
            names = {0:'COM',1:'NEG',2:'SWAP',3:'INC',5:'ASR',6:'LSR',7:'ROR'}
            if s in names:
                mn = f"{names[s]} r{d}"; dests = [d]; srcs = [d]
        elif (w & 0xFE0F) == 0x94A0:
            d = (w>>4)&0x1F; mn = f"DEC r{d}"; dests = [d]; srcs = [d]
        else:
            hi6 = w & 0xFC00
            if hi6 in (0x0400,0x0800,0x0C00,0x1000,0x1400,0x1800,0x1C00,0x2000,0x2400,0x2800,0x2C00):
                d = (w>>4)&0x1F; r = ((w>>5)&0x10)|(w&0xF)
                names = {0x0400:'CPC',0x0800:'SBC',0x0C00:'ADD',0x1000:'CPSE',0x1400:'CP',
                         0x1800:'SUB',0x1C00:'ADC',0x2000:'AND',0x2400:'EOR',0x2800:'OR',0x2C00:'MOV'}
                mn = f"{names[hi6]} r{d}, r{r}"
                if hi6 not in (0x0400,0x1000,0x1400): dests = [d]
                srcs = [d,r]
            elif (w & 0xD208) == 0x8000:
                q = ((w>>8)&0x20)|((w>>7)&0x18)|(w&7)
                mn = f"LDD r{(w>>4)&0x1F}, Z+{q}"; dests = [(w>>4)&0x1F]
            elif (w & 0xD208) == 0x8008:
                q = ((w>>8)&0x20)|((w>>7)&0x18)|(w&7)
                mn = f"LDD r{(w>>4)&0x1F}, Y+{q}"; dests = [(w>>4)&0x1F]
            elif (w & 0xD208) == 0x8200:
                q = ((w>>8)&0x20)|((w>>7)&0x18)|(w&7)
                mn = f"STD Z+{q}, r{(w>>4)&0x1F}"; srcs = [(w>>4)&0x1F]
            elif (w & 0xD208) == 0x8208:
                q = ((w>>8)&0x20)|((w>>7)&0x18)|(w&7)
                mn = f"STD Y+{q}, r{(w>>4)&0x1F}"; srcs = [(w>>4)&0x1F]
        if mn is None:
            info[i] = (f".word 0x{w:04X}", 1, [], [], 'd', None)
        else:
            info[i] = (mn, size, dests, srcs, kind, arg)
        for j in range(i+1, i+size):
            if j < nwords: info[j] = ('', 0, [], [], 'c2', None)
        i += size
    return info, xrefs, nwords

def main():
    hexpath, dispath = sys.argv[1], sys.argv[2]
    img = load_hex(hexpath)
    open(dispath.rsplit('.',1)[0] + '.bin', 'wb').write(img)
    info, xrefs, nwords = disassemble(img)

    # 立即数回溯注释: LDI rN, #imm 之后 12 条内 OUT/STS 用到 rN -> 标注 ≈
    imm = {}; seq = 0; anns = {}
    io_log = []   # (addr, line) I/O 访问记录
    for i in range(nwords):
        mn, size, dests, srcs, kind, arg = info[i]
        if not mn: continue
        seq += 1
        for d in dests: imm.pop(d, None)
        if mn.startswith('LDI'):
            r = int(mn.split('r')[1].split(',')[0])
            v = int(mn.split('0x')[1], 16)
            imm[r] = (v, seq)
        note = ""
        if arg:
            op = arg[0]
            if op in ('out','in'):
                A = arg[1]
                nm = SRAM_NAMES.get(A + 0x20)
                if nm:
                    pp = port_kind(A)
                    if op == 'out' and srcs and srcs[0] in imm and seq - imm[srcs[0]][1] <= 12:
                        v = imm[srcs[0]][0]
                        note = f"; {nm} ≈ 0x{v:02X}"
                        if pp and pp[1] in ('DDR','PORT') and pp[0] in "BCDEF":
                            bits = ", ".join(f"P{pp[0]}{b}" for b in range(8) if v >> b & 1)
                            if bits: note += f" ({'输出' if pp[1]=='DDR' else '电平/上拉'}: {bits})"
                            else: note += " (无)"
                    elif op == 'in':
                        note = f"; 读 {nm}"
                    io_log.append((i*2, f"{mn}  {note}".strip()))
            elif op in ('sbi','cbi','sbic','sbis'):
                A, b = arg[1], arg[2]
                nm = SRAM_NAMES.get(A + 0x20)
                if nm:
                    pp = port_kind(A)
                    pn = f"P{pp[0]}{b}" if pp else f"bit{b}"
                    verb = {'sbi':'置1','cbi':'清0','sbic':'跳过若=0','sbis':'跳过若=1'}[op]
                    note = f"; {nm}.{pn} {verb}"
                    io_log.append((i*2, f"{mn}  {note}"))
            elif op in ('mem_ld','mem_st'):
                k = arg[1]
                nm = SRAM_NAMES.get(k)
                if nm:
                    note = f"; {'读' if op=='mem_ld' else '写'} {nm}"
                    pp = None
                    if 0x23 <= k <= 0x31:
                        io = k - 0x20; pp = port_kind(io)
                    io_log.append((i*2, f"{mn}  {note}"))
        anns[i] = note

    # 汇总输出
    with open(dispath, 'w', encoding='utf-8') as f:
        f.write(f"; 反汇编: {hexpath}  共 {len(img)} 字节\n")
        f.write("; 注释中 ≈ 表示由回溯的 LDI 立即数推断(启发式)\n")
        prev_zero = 0; prev_data = 0
        for i in range(nwords):
            mn, size, dests, srcs, kind, arg = info[i]
            if mn == '':
                continue
            a = i*2
            lbl = ""
            if i in xrefs:
                srcs_s = ", ".join(f"0x{s*2:04X}" for s in xrefs[i][:5])
                f.write(f"L{a:04X}:                    ; <- {srcs_s}\n")
            if mn == 'NOP':
                prev_zero += 1
                if prev_zero <= 2:
                    f.write(f"{a:04X}: {img[a]:02X} {img[a+1] if a+1<len(img) else 0:02X}    NOP\n")
                continue
            else:
                if prev_zero > 2:
                    f.write(f"      ; ... ({prev_zero} x NOP)\n")
                prev_zero = 0
            if mn.startswith('.word'):
                prev_data += 1
                if prev_data <= 3:
                    f.write(f"{a:04X}: {img[a]:02X} {img[a+1]:02X}    {mn}\n")
                continue
            else:
                if prev_data > 3:
                    f.write(f"      ; ... 数据区? ({prev_data} 词连续无法解码)\n")
                prev_data = 0
            bs = f"{img[a]:02X} {img[a+1]:02X}" + (f" {img[a+2]:02X} {img[a+3]:02X}" if size==2 else "")
            f.write(f"{a:04X}: {bs:<11} {mn:<30} {anns.get(i,'')}\n")

    # I/O 汇总
    print(f"== {hexpath}: {nwords} 词, .word 比例 {sum(1 for x in info if x[4]=='d')}/{nwords}")
    agg = defaultdict(list)
    for addr, line in io_log:
        nm = line.split(';')[1].strip().split('.')[0].split(' ')[0] if ';' in line else '?'
        agg[nm].append((addr, line))
    for nm in sorted(agg):
        rows = agg[nm]
        print(f"  [{nm}] {len(rows)} 次访问, 首见 0x{rows[0][0]:04X}")
    with open(dispath.rsplit('.',1)[0] + '.io.txt', 'w', encoding='utf-8') as f:
        for addr, line in io_log:
            f.write(f"0x{addr:04X}: {line}\n")
    print(f"  清单: {dispath}\n  I/O 日志: {dispath.rsplit('.',1)[0]}.io.txt  bin: {dispath.rsplit('.',1)[0]}.bin")

if __name__ == '__main__':
    main()
