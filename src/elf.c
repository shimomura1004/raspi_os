#include <stdint.h>
#include "mm.h"
#include "utils.h"
#include "printf.h"
#include "elf.h"

int elf_check(struct elf_header *header)
{
    if (memcmp(header->id.magic, "\x7f" "ELF", 4)) {
        return -1;
    }

    if (header->id.class != 2) {
        return -1;  // ELF64
    }
    if (header->id.format != 1) {
        return -1;  // little endian
    }
    if (header->id.version != 1) {
        return -1;  // version 1
    }
    if (header->type != 2) {
        return -1;  // executable
    }
    if (header->version != 1) {
        return -1;  // version 1
    }
    if (header->arch != 0xb7) {
        return -1;  // AArch64
    }

    return 0;
}

int elf_load_program(struct elf_header *header)
{
    return 0;
}

// buf の位置に ELF ファイル全体がコピーされている前提で、正しい位置にロードする
uint8_t *elf_load(uint8_t *buf)
{
    struct elf_header *header = (struct elf_header *)buf;

    if (elf_check(header) < 0) {
        return NULL;
    }
    
    if (elf_load_program(header) < 0) {
        return NULL;
    }
    
    // elf ファイル内に書かれたエントリポイントのアドレスを返す
    return (uint8_t *)header->entry_point;
}
