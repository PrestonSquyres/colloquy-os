CC      := gcc
CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -no-pie \
           -mno-red-zone -mgeneral-regs-only -mcmodel=small -fno-builtin -nostdlib -Ikernel
UCFLAGS := -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -no-pie \
           -mgeneral-regs-only -fno-builtin -nostdlib -Ikernel -Iuser
LDFLAGS := -n -T linker.ld -nostdlib -z max-page-size=0x1000 -z noexecstack --no-warn-rwx-segments
ULDFLAGS:= -T user/user.ld -nostdlib -z max-page-size=0x1000 -z noexecstack --no-warn-rwx-segments
BUILD   := build
C_SRC   := $(wildcard kernel/*.c)
OBJS    := $(BUILD)/boot.o $(BUILD)/isr.o $(patsubst kernel/%.c,$(BUILD)/%.o,$(C_SRC))
PROGRAMS:= shell clock scribe hog probe
UELFS   := $(patsubst %,$(BUILD)/user/%.elf,$(PROGRAMS))
ULIB    := $(BUILD)/user/crt0.o $(BUILD)/user/lib.o

all: $(BUILD)/colloquy.iso

$(BUILD)/%.o: kernel/%.c kernel/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	nasm -f elf64 $< -o $@
$(BUILD)/isr.o: boot/isr.asm | $(BUILD)
	nasm -f elf64 $< -o $@
$(BUILD)/colloquy.elf: $(OBJS) linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/user/%.o: user/%.c user/*.h kernel/abi.h kernel/types.h | $(BUILD)/user
	$(CC) $(UCFLAGS) -c $< -o $@
$(BUILD)/user/crt0.o: user/crt0.asm | $(BUILD)/user
	nasm -f elf64 $< -o $@
$(BUILD)/user/%.elf: $(BUILD)/user/%.o $(ULIB) user/user.ld
	ld $(ULDFLAGS) -o $@ $(ULIB) $<

$(BUILD)/colloquy.iso: $(BUILD)/colloquy.elf $(UELFS) grub.cfg
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(BUILD)/colloquy.elf $(UELFS) $(BUILD)/iso/boot/
	cp grub.cfg $(BUILD)/iso/boot/grub/
	grub-mkrescue -o $@ $(BUILD)/iso 2>/dev/null
$(BUILD) $(BUILD)/user:
	mkdir -p $(BUILD)/user

run: $(BUILD)/colloquy.iso
	qemu-system-x86_64 -cdrom $< -m 128M -nographic -no-reboot

test: $(BUILD)/colloquy.iso
	cc -O2 -Ikernel tests/sha256_test.c kernel/sha256.c -o $(BUILD)/sha256_test && $(BUILD)/sha256_test
	python3 tests/qemu_test.py $<

clean:
	rm -rf $(BUILD)
.PHONY: all run test clean
.SECONDARY:
