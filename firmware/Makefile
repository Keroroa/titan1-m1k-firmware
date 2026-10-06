MCU      = atmega32u4
F_CPU    = 16000000
TARGET   = bstmouse2
SRC      = main.c usb.c
CC       = avr-gcc
OBJCOPY  = avr-objcopy
SIZE     = avr-size

CFLAGS  = -std=gnu11 -Os -Wall -Wextra -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL \
          -ffunction-sections -fdata-sections -fno-strict-aliasing \
          -funsigned-char -funsigned-bitfields
LDFLAGS = -Wl,--gc-sections -Wl,--relax

all: $(TARGET).hex

$(TARGET).elf: $(SRC) usb.h srom_3360_0x05.h
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@
	$(SIZE) $<
	@echo "flash 上限 28672 字节(0x7000,bootloader 前)"

clean:
	rm -f $(TARGET).elf $(TARGET).hex

.PHONY: all clean
