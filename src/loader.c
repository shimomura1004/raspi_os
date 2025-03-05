#include <inttypes.h>
#include "loader.h"
#include "fat32.h"
#include "mm.h"
#include "utils.h"
#include "debug.h"
#include "elf.h"
#include "arm/mmu.h"
#include "spinlock.h"

// todo: 初期化処理を呼んで初期化するようにする
struct spinlock loader_lock = {0, 0, -1};

int load_file_to_memory(struct vm_struct *tsk, const char *name, unsigned long va) {
    // todo: ロックの単位が大きいのでもっと細分化する
    acquire_lock(&loader_lock);

    struct fat32_fs hfat;
    if (fat32_get_handle(&hfat) < 0) {
        WARN("failed to find fat32 file system");
        return -1;
    }

    struct fat32_file file;
    if (fat32_lookup(&hfat, name, &file) < 0) {
        WARN("requested file (%s) is not found", name);
        return -1;
    }

    int remain = fat32_file_size(&file);
    int offset = 0;
    unsigned long current_va = va & PAGE_MASK;

    while (remain > 0) {
        uint8_t *buf = (uint8_t *)allocate_vm_page(tsk, current_va);
        int readsize = MIN(PAGE_SIZE, remain);
        int actualsize = fat32_read(&file, buf, offset, readsize);

        if (readsize != actualsize) {
            WARN("failed to read file");
            return -1;
        }

        remain -= readsize;
        offset += readsize;
        current_va += PAGE_SIZE;
    }

    tsk->name = name;

    release_lock(&loader_lock);
    return 0;
}

// todo: 丸ごと elf.c に移す？
int elf_binary_loader(void *args, unsigned long *pc, unsigned long *sp) {
    struct raw_binary_loader_args *loader_args = (struct raw_binary_loader_args *)args;

    struct fat32_fs hfat;
    if (fat32_get_handle(&hfat) < 0) {
        WARN("failed to find fat32 file system");
        return -1;
    }

    struct fat32_file file;
    if (fat32_lookup(&hfat, loader_args->filename, &file) < 0) {
        WARN("requested file (%s) is not found", loader_args->filename);
        return -1;
    }

    // ハイパーバイザのメモリ空間に ELF ヘッダ分を読み込む(1ページで十分)
    uint8_t *buf = (uint8_t *)allocate_page();
    int readsize = MIN(PAGE_SIZE, sizeof(struct elf_header));
    int actualsize = fat32_read(&file, buf, 0, readsize);

    if (readsize != actualsize) {
        WARN("failed to read file");
        return -1;
    }

    // ELF ヘッダのチェック
    struct elf_header *header = (struct elf_header *)buf;
    if (elf_check(header) < 0) {
        WARN("wrong ELF format");
        free_page(buf);
        return -1;
    }

    // ELF ヘッダを格納したメモリは別用途で使ってしまうので、必要な情報を退避する
    uint16_t program_header_num = header->program_header_num;
    uint64_t program_header_offset = header->program_header_offset;
    uint16_t program_header_size = header->program_header_size;
    *pc = header->entry_point & 0xffffffffffff;

    // セグメントを順番にロード
    for (int i = 0; i < program_header_num; i++) {
        // ハイパーバイザのメモリ空間にプログラムヘッダを読み込む(1ページで十分)
        // todo: ここを関数として抜き出し
        int readsize = MIN(PAGE_SIZE, program_header_size);
        int actualsize = fat32_read(&file, buf, program_header_offset + program_header_size * i, readsize);

        if (readsize != actualsize) {
            WARN("failed to read file");
            return -1;
        }

        struct elf_program_header *phdr = (struct elf_program_header *)buf;

        // ロード可能なセグメントかを確認
        if (phdr->type != 1) {
            INFO("skipping unloadable segment %d", i);
            continue;
        }
        INFO("loading segment %d", i);

        // プログラムヘッダを格納したメモリは別用途で使ってしまうので、必要な情報を退避する
        uint64_t offset = phdr->offset;
        uint64_t virtual_addr = phdr->virtual_addr;
        // uint64_t physical_addr = phdr->physical_addr;
        uint64_t file_size = phdr->file_size;
        uint64_t memory_size = phdr->memory_size;
        INFO("file_size/memory_size: 0x%lx/0x%lx", file_size, memory_size);

        // 指定されたアドレスにセグメントをコピーする(ページ単位のコピーをループする)
        // todo: 関数化
        while (memory_size > 0) {
            // コピー先となるゲストのメモリ空間にページを確保する
            // allocate_vm_page の中の map_stage2_page で stage2 テーブルを更新している
            // todo: 中途半端なアドレスな場合、うまく動かないかも
            uint8_t *vm_buf = (uint8_t *)allocate_vm_page(current(), virtual_addr);

            // コピー元のデータをハイパーバイザのメモリ空間に読み込む
            int actualsize = fat32_read(&file, vm_buf, offset, PAGE_SIZE);
            // ゼロクリアする領域があるので、file_size より memory_size のほうが大きい
            // file からコピーするデータがなくなったら、残りは 0 で埋める
            if (actualsize != PAGE_SIZE) {
                memzero(vm_buf + actualsize, PAGE_SIZE - actualsize);
            }

            memory_size = memory_size < PAGE_SIZE ? 0 : memory_size - PAGE_SIZE;
            virtual_addr += PAGE_SIZE;
            offset += PAGE_SIZE;
        }
    }

    *sp = loader_args->sp;
    INFO("pc: 0x%lx in 48bit, sp: 0x%lx(0x%lx in 48bit)", *pc & 0xffffffffffff, *sp, *sp & 0xffffffffffff);
    current()->name = loader_args->filename;

    free_page(buf);
    return 0;
}

int raw_binary_loader(void *args, unsigned long *pc, unsigned long *sp) {
    struct raw_binary_loader_args *loader_args = (struct raw_binary_loader_args *)args;

    if (load_file_to_memory(current(), loader_args->filename, loader_args->loader_addr) < 0) {
        return -1;
    }

    *pc = loader_args->entry_point;
    *sp = loader_args->sp;

    return 0;
}

// 元々(raspberry-pi-os)は
//   カーネルの仮想メモリ空間(VA)と物理メモリ(PA)がリニアマッピング(boot.S で設定)
//   ユーザプロセスの仮想メモリ空間(VA)と物理メモリ(PA)は任意のマッピング(適宜設定)
// ハイパーバイザ化により
//   ハイパーバイザの仮想メモリ空間(IPA)と物理メモリ(PA)がリニアマッピング(boot.S で設定)
//   VM の仮想メモリ空間(VA)とハイパーバイザのメモリ空間(IPA)は任意のマッピング(適宜設定)
//   これとは別に、ホストの自身の VA も PA にリニアマッピングされる
