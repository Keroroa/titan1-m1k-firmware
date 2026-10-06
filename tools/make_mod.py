#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bst 鼠标固件补丁脚本
底版: 2mm_400_1000_1600.hex (原厂 fw1)
改动:
  [1] 默认 DPI 1000 -> 800 cpi
      0x1248: LDI r24,0x09 (89 E0) -> LDI r24,0x07 (87 E0)
  [2] 滚轮方向取反
      0x1866: DEC r15 (FA 94) -> RJMP 0x1C54 (F6 C1)
      0x1C54: DEC r15 (FA 94); NEG r15 (F1 94); RJMP 0x1868 (07 CE)
      (0x1C54 起是原厂固件未使用的尾部空闲区, .data 只到 0x1C53)
      数学: 原方向 = 2*xor-1;  补丁后 = NEG(2*xor-1) = 1-2*xor, 恰好取反
strap 行为(不变): 上电按住 PD0 键=400 档, 按住 PD1 键=1600 档, 都不按=800 档(新默认)
输出: 2mm_400_800_1600_mod.hex
"""
def load_hex(path):
    ext=0; base=0; chunks={}
    for line in open(path):
        line=line.strip()
        if not line.startswith(':'): continue
        d=bytes.fromhex(line[1:])
        cnt,ah,al,rt=d[0],d[1],d[2],d[3]
        if rt==0:
            a=ext*0x10000+base+((ah<<8)|al)
            for k,b in enumerate(d[4:4+cnt]): chunks[a+k]=b
        elif rt==2: ext=int.from_bytes(d[4:6],'big')
        elif rt==4: base=int.from_bytes(d[4:6],'big')<<16
        elif rt==1: break
    hi=max(chunks)
    assert set(chunks)==set(range(hi+1)), "镜像不连续?"
    return bytearray(chunks[i] for i in range(hi+1))

fw1 = load_hex(r"C:\Users\Keroro\Downloads\2mm_400_1000_1600.hex")
assert len(fw1) == 0x1C54, hex(len(fw1))

# ---- 补丁 [1]: 默认 DPI 1000 -> 800 ----
assert fw1[0x1248:0x124A] == bytes.fromhex("89E0"), fw1[0x1248:0x124A].hex()
fw1[0x1248] = 0x87          # LDI r24, 0x07  (= 800 cpi)

# ---- 补丁 [2]: 滚轮取反跳板 ----
assert fw1[0x1864:0x1868] == bytes.fromhex("FF0CFA94"), fw1[0x1864:0x1868].hex()
fw1[0x1866] = 0xF6          # RJMP 0x1C54 : word 0xC33, k = 0xE2A-0xC34 = +0x1F6 -> 0xC1F6
fw1[0x1867] = 0xC1
fw1.extend(bytes(0x1C5A - len(fw1)))
fw1[0x1C54:0x1C5A] = bytes.fromhex("FA94F19407CE")
#   0x1C54 FA94 = DEC r15 ; 0x1C56 F194 = NEG r15 ; 0x1C58 07CE = RJMP 0x1868
#   (word 0xE2C, k = 0xC34-0xE2D = -0x1F9 -> 0xE07 -> 0xCE07)

# ---- 重新生成 Intel HEX ----
data = bytes(fw1)
out = []
for off in range(0, len(data), 16):
    chunk = data[off:off+16]
    rec = bytes([len(chunk), (off>>8)&0xFF, off&0xFF, 0x00]) + chunk
    out.append(':' + rec.hex().upper() + format((-sum(rec)) & 0xFF, '02X'))
out.append(':00000001FF')
dst = r"C:\Users\Keroro\Downloads\2mm_400_800_1600_mod.hex"
open(dst, "w", newline="\n").write("\n".join(out) + "\n")
print("OK ->", dst, f"({len(data)} bytes = 0x{len(data):X})")
