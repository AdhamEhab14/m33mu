CC=arm-none-eabi-gcc
OBJCOPY ?= arm-none-eabi-objcopy

CFLAGS := -mcpu=cortex-m33 -mthumb -Os -ffreestanding -fdata-sections -ffunction-sections -g
LDFLAGS := -nostdlib -T target.ld -Wl,-gc-sections

all: app.bin app_lockup.bin

app.elf: main.c target.ld
	$(CC) $(CFLAGS) main.c $(LDFLAGS) -o $@

app.bin: app.elf
	$(OBJCOPY) -O binary $< $@

app_lockup.elf: main.c target.ld
	$(CC) $(CFLAGS) -DLOCKUP main.c $(LDFLAGS) -o $@

app_lockup.bin: app_lockup.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f app.elf app.bin app_lockup.elf app_lockup.bin

.PHONY: all clean
