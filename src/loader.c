#include <inttypes.h>
#include "loader.h"
#include "fat32.h"
#include "mm.h"
#include "utils.h"
#include "debug.h"
#include "elf.h"
#include "arm/mmu.h"

int load_file_to_memory(struct task_struct *tsk, const char *name, unsigned long va) {
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
        uint8_t *buf = (uint8_t *)allocate_task_page(tsk, current_va);
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

    return 0;
}

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
    *pc = header->entry_point;

    // INFO("program header num: %d", program_header_num);
    // INFO("program header offset: %lx", program_header_offset);
    // INFO("program header size: %lx", program_header_size);

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
        uint64_t physical_addr = phdr->physical_addr;
        uint64_t file_size = phdr->file_size;
        uint64_t memory_size = phdr->memory_size;

        // INFO("offset: %lx", offset);
        INFO("virtual addr: %lx", virtual_addr);
        INFO("physical addr: %lx", physical_addr);
        // INFO("file size: %lx", file_size);
        // INFO("memory size: %lx", memory_size);

// ここから、メモリアクセスがうまくいってない
        // 指定されたアドレスにセグメントをコピーする(ページ単位のコピーをループする)
        // todo: 関数化
        // todo: memory_size > 0 にして、後半はゼロクリアするコードを入れる？
        while (file_size > 0) {
            // コピー先となるゲストのメモリ空間にページを確保する
            // todo: 中途半端なアドレスな場合、うまく動かないかも
            uint8_t *vm_buf = (uint8_t *)allocate_task_page(current, virtual_addr);

            // コピー元のデータをハイパーバイザのメモリ空間に読み込む
            int actualsize = fat32_read(&file, vm_buf, program_header_offset + offset, PAGE_SIZE);
INFO("actualsize: %d", actualsize);
            if (actualsize != PAGE_SIZE) {
                // todo: 足りなかったらゼロクリアする処理を入れる
            }

INFO("first bytes: %02x %02x %02x %02x  %02x %02x %02x %02x",
     vm_buf[0], vm_buf[1], vm_buf[2], vm_buf[3], vm_buf[4], vm_buf[5], vm_buf[6], vm_buf[7]);

            file_size = file_size < PAGE_SIZE ? 0 : file_size - PAGE_SIZE;
            virtual_addr += PAGE_SIZE;
            offset += PAGE_SIZE;
INFO("file_size: %d, virtual_addr: %lx, offset: %d", file_size, virtual_addr, offset);
        }
    }

    *sp = loader_args->sp;
INFO("pc: %lx, sp: %lx", *pc, *sp);
    current->name = loader_args->filename;

    free_page(buf);
    return 0;
}

int raw_binary_loader(void *args, unsigned long *pc, unsigned long *sp) {
    struct raw_binary_loader_args *loader_args = (struct raw_binary_loader_args *)args;

    if (load_file_to_memory(current, loader_args->filename, loader_args->loader_addr) < 0) {
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
// ここでは VM 用(EL1)のメモリマッピングを行う

// カーネル内に埋め込まれた EL1 用のコードを取り出し、EL1 用のメモリ空間にコピーする
int test_program_loader(void *arg, unsigned long *pc, unsigned long *sp) {
    extern unsigned long el1_test_1;
    extern unsigned long el1_test_2;

	// el1_test_begin/el1_test_end はリンカスクリプトで指定されたアドレス
	// ユーザプログラムのコードやデータ領域の先頭と末尾のアドレスを指す
    extern unsigned long el1_test_begin;
    extern unsigned long el1_test_end;

    unsigned long begin = (unsigned long)&el1_test_begin;
    unsigned long end = (unsigned long)&el1_test_end;
    unsigned long size = end - begin;
    unsigned long func = (unsigned long)&el1_test_1;

    switch ((unsigned long)arg) {
    case 1:
        func = (unsigned long)&el1_test_1;
        break;
    case 2:
        func = (unsigned long)&el1_test_2;
        break;
    }
    unsigned long entry_point = func - begin;

	// 現在実行中のタスクのページテーブルにマッピングを追加
	// current タスク用のアドレス空間にページを追加するので、仮想アドレスは任意の値でいい
    unsigned long code_page = allocate_task_page(current, 0);
    if (code_page == 0) {
        return -1;
    }
    memcpy((void *)code_page, (void *)begin, size);

    *pc = entry_point;
    *sp = 2 * PAGE_SIZE;

    return 0;
}
