#include <inttypes.h>
#include <stddef.h>
#include "sd.h"
#include "debug.h"
#include "mm.h"
#include "utils.h"

// FAT パーティションの先頭512バイトには BPB があり、その次に FAT がある
// https://zenn.dev/hidenori3/articles/3ce349c02e79fa

// BPB: 各パーティションの最初のセクタに存在する領域で
//      ファイルシステムの物理レイアウトやパラメータを記録するもの
// セクタ: ディスク上の最小のデータ単位(通常は512バイト)
// クラスタ: 複数のセクタをまとめたもので、ファイルを格納する単位
//          (4KB(8セクタ) や 32KB(64セクタ) などが多い)
//          ファイルはクラスタ単位で格納されるので、クラスタ内には使われないセクタができうる
// パーティションの先頭に BPB があり、その後ろに FAT1、FAT2、ユーザ領域(クラスタの配列)が続く
// FAT にはクラスタ番号が入った配列があり、インデックスのリンクリストとしてたどれるようになっている
//   ディレクトリの場合は、その中に入っているファイルやディレクトリの名前とクラスタ番号の組が入っている
//   ファイルが大きい場合は、複数のクラスタを使ってひとつのファイルを表すようになっている
//   https://zenn.dev/hidenori3/articles/3ce349c02e79fa

#define FAT32_BOOT              0
#define FAT32_MAX_FILENAME_LEN  255
#define BLOCKSIZE               512

// the BIOS Parameter Block (in Volume Boot Record)
// https://wiki.osdev.org/FAT#FAT_32
struct fat32_boot {
    // FAT12/FAT16 で定義
    uint8_t     BS_JmpBoot[3];      // ジャンプ命令
    uint8_t     BS_OEMName[8];      // OEM 名だが無視されることが多い
    uint16_t    BPB_BytsPerSec;     // セクタあたりのバイト数
    uint8_t     BPB_SecPerClus;     // クラスタあたりのセクタ数
    uint16_t    BPB_RsvdSecCnt;     // 予約領域のセクタ数
    uint8_t     BPB_NumFATs;        // FAT(file allocation table)の数
                                    // 通常は 2 で、多重化されている
    uint16_t    BPB_RootEntCnt;     // ルートディレクトリのエントリ数
    uint16_t    BPB_TotSec16;       // ボリューム内の総セクタ数
                                    // 0 の場合は 65535 以上のセクタがあり、BPB_TotSec32 が使われる
    uint8_t     BPB_Media;          // media descriptor type(メディアのインチ数とか、片面か両面か、とか)
    uint16_t    BPB_FATSz16;        // FAT あたりのセクタ数(FAT12/FAT16 のみ)
    uint16_t    BPB_SecPerTrk;      // トラックあたりのセクタ数
    uint16_t    BPB_NumHeads;       // ヘッド数もしくはサイド数(片面、両面)
    uint32_t    BPB_HiddSec;        // 隠しセクタ数
    uint32_t    BPB_TotSec32;       // Large sector count: TotSec16(2バイト)で足りないときはこちらを使う
    // FAT32 で追加された領域
    uint32_t    BPB_FATSz32;        // FAT あたりのセクタ数(FAT32 のみ)
    uint16_t    BPB_ExtFlags;       // 拡張フラグ(詳細不明)
    uint16_t    BPB_FSVer;          // FAT のバージョン(上位バがメジャー、下位がマイナ番号)
    uint32_t    BPB_RootClus;       // ルートディレクトリのクラスタ番号(通常は 2)
    uint16_t    BPB_FSInfo;         // FSInfo が格納されたセクタのセクタ番号
    uint16_t    BPB_BkBootSec;      // バックアップのブートセクタのセクタ番号
    uint8_t     BPB_Reserved[12];   // 予約領域
    uint8_t     BS_DrvNum;          // ドライブ番号(BIOS の INT 13h で使われるものと同じ定義)
                                    // 0x80 がハードディスク、0x00 がフロッピーディスク
    uint8_t     BS_Reserved1;       // Windows NT 用の予約領域
    uint8_t     BS_BootSig;         // 0x29 か 0x28 が入る
    uint32_t    BS_VolID;           // ボリュームのシリアル番号だが、無視していい
                                    // コンピュータをまたいで一意にして、ドライブを区別するために使う想定
    uint8_t     BS_VolLab[11];      // ボリュームラベル(末尾はスペースで埋める)
    uint8_t     BS_FilSysType[8];   // ファイルシステムの種類だが、常に "FAT32   " が入る
    uint8_t     BS_BootCode32[420]; // ブートストラップのコード
    uint16_t    BS_BootSign;        // 有効なブートセクタである場合 0xaa55 が入る
} __attribute__((__packed__));

// file system info
struct fat32_fsi {
    uint32_t    FSI_LeadSig;
    uint8_t     FSI_Reserved1[480];
    uint32_t    FSI_StrucSig;
    uint32_t    FSI_Free_Count;
    uint32_t    FSI_Nxt_Free;
    uint8_t     FSI_Reserved2[12];
    uint32_t    FSI_TrailSig;
} __attribute__((__packed__));

#define ATTR_READ_ONLY   0x01
#define ATTR_HIDDEN      0x02
#define ATTR_SYSTEM      0x04
#define ATTR_VOLUME_ID   0x08
#define ATTR_DIRECTORY   0x10
#define ATTR_ARCHIVE     0x20
#define ATTR_LONG_NAME   (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

// directory entry
struct fat32_dent {
    uint8_t     DIR_Name[11];
    uint8_t     DIR_Attr;
    uint8_t     DIR_NTRes;
    uint8_t     DIR_CrtTimeTenth;
    uint16_t    DIR_CrtTime;
    uint16_t    DIR_CrtDate;
    uint16_t    DIR_LstAccDate;
    uint16_t    DIR_FstClusHI;
    uint16_t    DIR_WrtTime;
    uint16_t    DIR_WrtDate;
    uint16_t    DIR_FstClusLO;
    uint32_t    DIR_FileSize;
} __attribute__((__packed__));

// long file name entry
struct fat32_lfnent {
    uint8_t     LDIR_Ord;
    uint8_t     LDIR_Name1[10];
    uint8_t     LDIR_Attr;
    uint8_t     LDIR_Type;
    uint8_t     LDIR_Chksum;
    uint8_t     LDIR_Name2[12];
    uint16_t    LDIR_FstClusLO;
    uint8_t     LDIR_Name3[4];
} __attribute__((__packed__));

enum fat32_file_type {
    FAT32_REGULAR,
    FAT32_DIR,
};

struct fat32_file {
    struct fat32_fs *fat32;
    uint8_t attr;
    uint32_t size;
    uint32_t cluster;
};

// todo: 何を表す構造体なのかを把握する
struct fat32_fs {
    struct fat32_boot boot;
    struct fat32_fsi fsi;
    uint32_t fatstart;
    uint32_t fatsectors;
    uint32_t rootstart;
    uint32_t rootsectors;
    uint32_t datastart;
    uint32_t datasectors;
    struct fat32_file root;
};

// クラスタ番号
// 0                         : 未使用クラスタ
// 1                         : Reserved
// 2           - 0x0fff_fff6 : 有効なクラスタ
// 0x0fff_fff7               : 不良クラスタ
// 0x0fff_fff8 - 0x0fff_ffff : クラスタ終端
#define is_active_cluster(c) (0x2 <= (c) && (c) < 0x0ffffff6)
#define is_terminal_cluster(c) (0x0ffffff8 <= (c) && (c) <= 0x0fffffff)
#define dir_cluster(dir) (((dir)->DIR_FstClusHI << 16) | (dir)->DIR_FstClusLO)
#define UNUSED_CLUSTER      0
#define RESERVED_CLUSTER    1
#define BAD_CLUSTER         0x0ffffff7

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int strncmp(const char *s1, const char *s2, size_t n) {
    size_t i;
    for (i = 0; i < n && *s1 && (*s1 == *s2); i++, s1++, s2++)
        ;
    return (i != n) ? (*s1 - *s2) : 0;
}

// todo: 動作を把握する
static uint8_t *alloc_and_readblock(unsigned int lba) {
    uint8_t *buf = (uint8_t *)allocate_page();
    if (sd_readblock(lba, buf, 1)) {
        PANIC("sd_readblock() failed.");
    }
    return buf;
}

// todo: 動作を把握する
static int fat32_is_valid_boot(struct fat32_boot *boot) {
    if (boot->BS_BootSign != 0xaa55) {
        return 0;
    }
    if (boot->BPB_BytsPerSec != BLOCKSIZE) {
        return 0;
    }
    return 1;
}

// todo: 動作を把握する
static void fat32_file_init(struct fat32_fs *fat32, struct fat32_file *fatfile,
                            uint8_t attr, uint32_t size, uint32_t cluster) {
    fatfile->fat32 = fat32;
    fatfile->attr = attr;
    fatfile->size = size;
    fatfile->cluster = cluster;
}

// todo: 動作を把握する
int fat32_get_handle(struct fat32_fs *fat32) {
    uint8_t *bbuf = alloc_and_readblock(FAT32_BOOT);
    fat32->boot = *(struct fat32_boot *)bbuf;
    struct fat32_boot *boot = &(fat32->boot);
    free_page(bbuf);

    fat32->fatstart = boot->BPB_RsvdSecCnt;
    fat32->fatsectors = boot->BPB_FATSz32 * boot->BPB_NumFATs;
    fat32->rootstart = fat32->fatstart + fat32->fatsectors;
    fat32->rootsectors =
        (sizeof(struct fat32_dent) * boot->BPB_RootEntCnt + boot->BPB_BytsPerSec - 1) /
        boot->BPB_BytsPerSec;
    fat32->datastart = fat32->rootstart + fat32->rootsectors;
    fat32->datasectors = boot->BPB_TotSec32 - fat32->datastart;

    // todo: 動作を把握する
    // 65526 = 0xfff6
    if (fat32->datasectors / boot->BPB_SecPerClus < 65526 || !fat32_is_valid_boot(boot)) {
        WARN("Bad fat32 filesystem.");
        return -1;
    }

    fat32_file_init(fat32, &(fat32->root), ATTR_DIRECTORY, 0, fat32->boot.BPB_RootClus);

    return 0;
}

// todo: 動作を把握する
static uint32_t fatent_read(struct fat32_fs *fat32, uint32_t index) {
    struct fat32_boot *boot = &fat32->boot;
    uint32_t sector = fat32->fatstart + (index * 4 / boot->BPB_BytsPerSec);
    uint32_t offset = index * 4 % boot->BPB_BytsPerSec;
    uint8_t *bbuf = alloc_and_readblock(sector);
    uint32_t entry = *((uint32_t *)(bbuf + offset)) & 0x0fffffff;
    free_page(bbuf);
    return entry;
}

// todo: 動作を把握する
static uint32_t walk_cluster_chain(struct fat32_fs *fat32, uint32_t offset, uint32_t cluster) {
    int nlook = offset / (fat32->boot.BPB_SecPerClus * fat32->boot.BPB_BytsPerSec);

    struct fat32_boot *boot = &fat32->boot;
    uint8_t *bbuf = NULL;
    uint32_t prevsector = 0;

    for (int i = 0; i < nlook; i++) {
        uint32_t sector = fat32->fatstart + (cluster * 4 / boot->BPB_BytsPerSec);
        uint32_t offset = cluster * 4 % boot->BPB_BytsPerSec;

        if (prevsector != sector) {
            if (bbuf) {
                free_page(bbuf);
            }
            bbuf = alloc_and_readblock(sector);
        }

        cluster = *((uint32_t *)(bbuf + offset)) & 0x0fffffff;
        if (!is_active_cluster(cluster)) {
            cluster = BAD_CLUSTER;
            goto exit;
        }
    }
exit:
    if (bbuf) {
        free_page(bbuf);
    }
    return cluster;
}

// todo: 動作を把握する
static uint32_t cluster_to_sector(struct fat32_fs *fat32, uint32_t cluster) {
    return fat32->datastart + (cluster - 2) * fat32->boot.BPB_SecPerClus;
}

// todo: 動作を把握する
static uint8_t create_sum(struct fat32_dent *entry) {
    int i;
    uint8_t sum;

    for (i = 0, sum = 0; i < 11; i++) {
        sum = (sum >> 1) + (sum << 7) + entry->DIR_Name[i];
    }

    return sum;
}

// todo: 動作を把握する
// short file name?
static char *get_sfn(struct fat32_dent *sfnent) {
    static char name[13];
    char *ptr = name;
    for (int i = 0; i < 8; i++, ptr++) {
        *ptr = sfnent->DIR_Name[i];
        // todo: 0x05 とは？
        if (*ptr == 0x05) {
            *ptr = 0xe5;
        }
        if (*ptr == ' ') {
            break;
        }
    }

    if (sfnent->DIR_Name[8] != ' ') {
        *ptr++ = '.';
        for (int i = 8; i < 11; i++, ptr++) {
            *ptr = sfnent->DIR_Name[i];
            if (*ptr == 0x05) {
                *ptr = 0xe5;
            }
            if (*ptr == ' ') {
                break;
            }
        }
    }

    *ptr = '\0';
    return name;
}

// todo: 動作を把握する
static char *get_lfn(struct fat32_dent *sfnent, size_t sfnoff, struct fat32_dent *prevblk_dent) {
    struct fat32_lfnent *lfnent = (struct fat32_lfnent *)sfnent;
    uint8_t sum = create_sum(sfnent);
    static char name[256];
    char *ptr = name;
    int seq = 1;
    int is_prev_blk = 0;

    while(1) {
        if (!is_prev_blk && (sfnoff & (BLOCKSIZE - 1)) == 0) {
            // block boundary
            lfnent = (struct fat32_lfnent *)prevblk_dent;
            is_prev_blk = 1;
        }
        else {
            lfnent--;
        }

        if (lfnent == NULL || lfnent->LDIR_Chksum != sum ||
            (lfnent->LDIR_Attr & ATTR_LONG_NAME) != ATTR_LONG_NAME ||
            (lfnent->LDIR_Ord & 0x1f) != seq++) {
            return NULL;
        }

        for (int i = 0; i < 10; i += 2) {
            // UTF16-LE
            *ptr++ = lfnent->LDIR_Name1[i];
        }
        for (int i = 0; i < 12; i += 2) {
            // UTF16-LE
            *ptr++ = lfnent->LDIR_Name2[i];
        }
        for (int i = 0; i < 4; i += 2) {
            // UTF16-LE
            *ptr++ = lfnent->LDIR_Name3[i];
        }
        if (lfnent->LDIR_Ord & 0x40) {
            break;
        }

        sfnoff -= sizeof(struct fat32_dent);
    }

    return name;
}

// todo: 動作を把握する
static uint32_t fat32_firstblk(struct fat32_fs *fat32, uint32_t cluster, size_t file_off) {
    uint32_t secs_per_clus = fat32->boot.BPB_SecPerClus;
    uint32_t remblk = file_off % (secs_per_clus * BLOCKSIZE) / BLOCKSIZE;
    return cluster_to_sector(fat32, cluster) + remblk;
}

// todo: 動作を把握する
static int fat32_nextblk(struct fat32_fs *fat32, int prevblk, uint32_t *cluster) {
  uint32_t secs_per_clus = fat32->boot.BPB_SecPerClus;
  if (prevblk % secs_per_clus != secs_per_clus - 1) {
    return prevblk + 1;
  } else {
    // go over a cluster boundary
    *cluster = fatent_read(fat32, *cluster);
    return fat32_firstblk(fat32, *cluster, 0);
  }
}

// todo: 動作を把握する
static int fat32_lookup_main(struct fat32_file *fatfile, const char *name,
                             struct fat32_file *found) {
    struct fat32_fs *fat32 = fatfile->fat32;
    if (!(fatfile->attr & ATTR_DIRECTORY))
        return -1;
    uint8_t *prevbuf = NULL;
    uint8_t *bbuf = NULL;
    uint32_t current_cluster = fatfile->cluster;
    for (int blkno = fat32_firstblk(fat32, current_cluster, 0);
         is_active_cluster(current_cluster);) {
        bbuf = alloc_and_readblock(blkno);
        for (uint32_t i = 0; i < BLOCKSIZE; i += sizeof(struct fat32_dent)) {
            struct fat32_dent *dent = (struct fat32_dent *)(bbuf + i);
            if (dent->DIR_Name[0] == 0x00)
                break;
            if (dent->DIR_Name[0] == 0xe5)
                continue;
            if (dent->DIR_Attr & (ATTR_VOLUME_ID | ATTR_LONG_NAME))
                continue;
            char *dent_name = NULL;
            dent_name = get_lfn(
                dent, i,
                prevbuf ? prevbuf + (BLOCKSIZE - sizeof(struct fat32_dent))
                        : NULL);
            if (dent_name == NULL)
                dent_name = get_sfn(dent);
            if (strncmp(name, dent_name, FAT32_MAX_FILENAME_LEN) == 0) {
                uint32_t dent_clus =
                    (dent->DIR_FstClusHI << 16) | dent->DIR_FstClusLO;
                if (dent_clus == 0) {
                    // root directory
                    dent_clus = fat32->boot.BPB_RootClus;
                }
                fat32_file_init(fat32, found, dent->DIR_Attr,
                                dent->DIR_FileSize, dent_clus);
                goto file_found;
            }
        }
        if (prevbuf != NULL)
            free_page(prevbuf);
        prevbuf = bbuf;
        bbuf = NULL;
        blkno = fat32_nextblk(fat32, blkno, &current_cluster);
    }
    if (prevbuf != NULL)
        free_page(prevbuf);
    if (bbuf != NULL)
        free_page(bbuf);
    return -1;
file_found:
    if (prevbuf != NULL)
        free_page(prevbuf);
    if (bbuf != NULL)
        free_page(bbuf);
    return 0;
}

// todo: 動作を把握する
int fat32_lookup(struct fat32_fs *fat32, const char *name,
                 struct fat32_file *fatfile) {
    return fat32_lookup_main(&fat32->root, name, &fatfile);
}

// todo: 動作を把握する
int fat32_read(struct fat32_file *fatfile, void *buf, unsigned long offset,
               size_t count) {
    struct fat32_fs *fat32 = fatfile->fat32;
    if (offset < 0)
        return -1;
    uint32_t tail = MIN(count + offset, fatfile->size);
    uint32_t remain = tail - offset;
    if (tail <= offset)
        return 0;
    uint32_t current_cluster =
        walk_cluster_chain(fat32, offset, fatfile->cluster);
    uint32_t inblk_off = offset % BLOCKSIZE;
    for (int blkno = fat32_firstblk(fat32, current_cluster, offset);
         remain > 0 && is_active_cluster(current_cluster);
         blkno = fat32_nextblk(fat32, blkno, &current_cluster)) {
        uint8_t *bbuf = alloc_and_readblock(blkno);
        uint32_t copylen = MIN(BLOCKSIZE - inblk_off, remain);
        memcpy(buf, bbuf + inblk_off, copylen);
        free_page(bbuf);
        buf += copylen;
        remain -= copylen;
        inblk_off = 0;
    }
    uint32_t read_bytes = (tail - offset) - remain;
    return read_bytes;
}

// todo: 動作を把握する
int fat32_file_size(struct fat32_file *fatfile) {
    return fatfile->size;
}

// todo: 動作を把握する
int fat32_is_directory(struct fat32_file *fatfile) {
    return (fatfile->attr & ATTR_DIRECTORY) != 0;
}
