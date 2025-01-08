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

    return 0;
}
