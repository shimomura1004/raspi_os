#include <inttypes.h>
#include "loader.h"
#include "fat32.h"
#include "mm.h"
#include "utils.h"
#include "debug.h"

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
