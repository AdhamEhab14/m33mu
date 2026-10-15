CC=arm-none-eabi-gcc
OBJCOPY ?= arm-none-eabi-objcopy

CFLAGS := -mcpu=cortex-m33 -mthumb -Os -ffreestanding -fdata-sections -ffunction-sections -g
LDFLAGS := -nostdlib -T target.ld -Wl,-gc-sections

all: app.bin

app.elf: main.c target.ld
	$(CC) $(CFLAGS) main.c $(LDFLAGS) -o $@

app.bin: app.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f app.elf app.bin

.PHONY: all clean
