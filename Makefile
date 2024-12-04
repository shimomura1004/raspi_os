.PHONY: all clean debug

ARMGNU ?= aarch64-linux-gnu

COPS = -Wall -nostdlib -nostartfiles -ffreestanding -Iinclude -mgeneral-regs-only
COPS += -g -O0
ASMOPS = -Iinclude -g

BUILD_DIR = build
SRC_DIR = src

all : kernel8.img

clean :
	rm -rf $(BUILD_DIR) *.img 

debug : kernel8.img
	gdb-multiarch -ex 'target remote :1234' \
	-ex 'layout asm' \
	-ex 'add-symbol-file build/kernel8.elf'

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
