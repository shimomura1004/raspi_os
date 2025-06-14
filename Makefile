.PHONY: all clean debug summary

ARMGNU ?= aarch64-linux-gnu

# ログレベルの設定(デフォルトはINFO)
# make 時に変更する場合は make LOG_LEVEL=LOG_LEVEL_DEBUG などとする
LOG_LEVEL ?= LOG_LEVEL_INFO

COPS = -Wall -nostdlib -nostartfiles -ffreestanding -Iinclude -mgeneral-regs-only
COPS += -g -O0
COPS += -DLOG_LEVEL=$(LOG_LEVEL)
ASMOPS = -Iinclude -g

BUILD_DIR = build
SRC_DIR = src

SUBDIRS = ./example

all: $(SUBDIRS) kernel8.img fs.img

clean: $(SUBDIRS)
	rm -rf $(BUILD_DIR) *.img 

$(SUBDIRS): FORCE
	$(MAKE) -C $@ $(MAKECMDGOALS)
FORCE:

debug: kernel8.img
	gdb-multiarch -ex 'target remote :1234' \
	-ex 'layout asm' \
	-ex 'add-symbol-file build/kernel8.elf'

summary:
	./summary.sh > summary.txt

fs.img: $(SUBDIRS)
	dd if=/dev/zero of=fs.img bs=1M count=64
	mformat -i fs.img -F ::
	-mcopy -i fs.img ./example/echo/echo.bin ::ECHO.BIN
	-mcopy -i fs.img ./example/test_binary/test.bin ::TEST2.BIN
	-mcopy -i fs.img ./example/mini-os/mini-os.bin ::MINI-OS.BIN
	-mcopy -i fs.img ./example/echo/build/kernel8.elf ::ECHO.ELF
	-mcopy -i fs.img ./example/mini-os/build/kernel8.elf ::MINI-OS.ELF
	-mcopy -i fs.img ./example/raspios/kernel8.img ::RASPIOS.BIN
	-mcopy -i fs.img ./example/raspios/build/kernel8.elf ::RASPIOS.ELF
	-mcopy -i fs.img ./example/vmm/build/kernel8.elf ::VMM.ELF

$(BUILD_DIR)/%_c.o: $(SRC_DIR)/%.c
	mkdir -p $(@D)
	$(ARMGNU)-gcc $(COPS) -MMD -c $< -o $@

$(BUILD_DIR)/%_s.o: $(SRC_DIR)/%.S
	$(ARMGNU)-gcc $(ASMOPS) -MMD -c $< -o $@

C_FILES = $(wildcard $(SRC_DIR)/*.c)
ASM_FILES = $(wildcard $(SRC_DIR)/*.S)
OBJ_FILES = $(C_FILES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%_c.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(BUILD_DIR)/%_s.o)

DEP_FILES = $(OBJ_FILES:%.o=%.d)
-include $(DEP_FILES)

# kernel8.img は、kernel8.elf を objcopy で raw binary にしたもの
# raw binary: 実際にメモリに展開される内容がそのままファイルになったもの
#             elf ファイルのようにセクションごとにパースなどをせず全体をコピーするだけでいいので楽
kernel8.img: $(SRC_DIR)/linker.ld $(OBJ_FILES)
	$(ARMGNU)-ld -Map kernel8.map -T $(SRC_DIR)/linker.ld -o $(BUILD_DIR)/kernel8.elf  $(OBJ_FILES)
	$(ARMGNU)-objcopy $(BUILD_DIR)/kernel8.elf -O binary kernel8.img

# elf バイナリを逆アセンブルする
#  aarch64-linux-gnu-objdump -D example/raspios/build/kernel8.elf
# raw binary である kernel8.img を逆アセンブルする
#  aarch64-linux-gnu-objdump -b binary --architecture=aarch64 -D kernel8.img
# raw binary を elf に戻す
#   aarch64-linux-gnu-objcopy -Ibinary -Oelf64-littleaarch64 kernel8.img hoge
# elf を disassemble する (-d ではなく -D を使う)
#   aarch64-linux-gnu-objdump -b binary --target=elf64-littleaarch64 -D hoge
# raw にするとシンボル情報などが落ち、開始・終了位置とサイズだけになる
#   aarch64-linux-gnu-nm hoge

# kernel8.img is a flat binary and has no section information
# use objdmp kernel8.elf to see section names
