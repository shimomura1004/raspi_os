/*
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <inttypes.h>
#include "sd.h"
#include "debug.h"
#include "mm.h"

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

int memcmp(void *s1, void *s2, int n)
{
    unsigned char *a=s1,*b=s2;
    while (n-->0) {
        if (*a!=*b) {
            return *a-*b;
        }
        a++;
        b++;
    }
    return 0;
}

// 最初の FAT パーティションのアドレス
static unsigned int partitionlba = 0;

#define FAT32_BOOT              0
#define FAT32_MAX_FILENAME_LEN  255
#define BLOCKSIZE               512

// the BIOS Parameter Block (in Volume Boot Record)
typedef struct fat32_boot {
    uint8_t     BS_JmpBoot[3];
    uint8_t     BS_OEMName[8];
    uint16_t    BPB_BytsPerSec;
    uint8_t     BPB_SecPerClus;
    uint16_t    BPB_RsvdSecCnt;
    uint8_t     BPB_NumFATs;
    uint16_t    BPB_RootEntCnt;
    uint16_t    BPB_TotSec16;
    uint8_t     BPB_Media;
    uint16_t    BPB_FATSz16;
    uint16_t    BPB_SecPerTrk;
    uint16_t    BPB_NumHeads;
    uint32_t    BPB_HiddSec;
    uint32_t    BPB_TotSec32;
    uint32_t    BPB_FATSz32;
    uint32_t    BPB_ExtFlags;
    uint32_t    BPB_FSVer;
    uint32_t    BPB_RootClus;
    uint16_t    BPB_FSInfo;
    uint16_t    BPB_BkBootSec;
    uint8_t     BPB_Reserved[12];
    uint8_t     BS_DrvNum;
    uint8_t     BS_Reserved1;
    uint8_t     BS_BootSig;
    uint32_t    BS_VolID;
    uint8_t     BS_VolLab[11];
    uint8_t     BS_FilSysType[8];
    uint8_t     BS_BootCode32[420];
    uint16_t    BS_BootSign;
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
} __attribute__((__packed___));

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

/**
 * Get the starting LBA address of the first partition
 * so that we know where our FAT file system starts, and
 * read that volume's BIOS Parameter Block
 */
int fat_getpartition(void)
{
    // 一時的に確保した領域 buf にパーティションテーブルを読み込んで内容を確認する
    // FAT ファイルシステムの開始位置はグローバル変数 partitionlba に保存する
    void *buf = (void *)allocate_page();
    unsigned char *mbr = buf;
    bpb_t *bpb = (bpb_t *)buf;
    // read the partitioning table
    if (sd_readblock(0, buf, 1)) {
        // check magic
        if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
            PANIC("ERROR: Bad magic in MBR");
            free_page(buf);
            return 0;
        }
        // check partition type
        if (mbr[0x1C2] != 0xE/*FAT16 LBA*/ && mbr[0x1C2] != 0xC/*FAT32 LBA*/) {
            PANIC("ERROR: Wrong partition type");
            free_page(buf);
            return 0;
        }
        // should be this, but compiler generates bad code...
        //partitionlba=*((unsigned int*)((unsigned long)&_end+0x1C6));
        partitionlba = mbr[0x1C6] + (mbr[0x1C7]<<8) + (mbr[0x1C8]<<16) + (mbr[0x1C9]<<24);
        // read the boot record
        if (!sd_readblock(partitionlba, buf, 1)) {
            PANIC("ERROR: Unable to read boot record");
            free_page(buf);
            return 0;
        }
        // check file system type. We don't use cluster numbers for that, but magic bytes
        if (!(bpb->fst[0] == 'F' && bpb->fst[1] == 'A' && bpb->fst[2] == 'T') &&
            !(bpb->fst2[0] == 'F' && bpb->fst2[1] == 'A' && bpb->fst2[2] == 'T')) {
            PANIC("ERROR: Unknown file system type");
            free_page(buf);
            return 0;
        }
        free_page(buf);
        return 1;
    }
    free_page(buf);
    return 0;
}

/**
 * Find a file in root directory entries
 */
// fn: file name
unsigned int fat_getcluster(char *fn)
{
    // todo: buf(bpb) はここで新規に割当てた領域だから、別名の bpb に対し bpb->spf などで
    //       アクセスしても、無効な値しか得られないのでは？
    void *buf = (void *)allocate_page();
    bpb_t *bpb = (bpb_t *)buf;
    // FAT パーティションの先頭512バイトには BPB があり、その次に FAT がある
    // https://zenn.dev/hidenori3/articles/3ce349c02e79fa
    fatdir_t *dir = (fatdir_t *)(buf + 512);
    unsigned int root_sec, s;
    // find the root directory's LBA
    // - spf16(BPB_FATSz16?)は、1つの FAT が占めるセクタ数(FAT12,16 の場合のみ有効)
    // - spf32(BPB_FATSz32?)は、1つの FAT が占めるセクタ数(FAT32 の場合のみ有効)
    // - nf(NumFATs)は、FAT テーブルの数(通常は2で、復旧用として二重化されている)
    // - rsc(BPB_RsvdSecCnt)は、BPB を含む FAT パーティションの先頭から
    //   FAT1/FAT2 領域直前までのセクタ数
    // よって root_sec はユーザデータ領域の先頭位置になる
    root_sec = ((bpb->spf16 ? bpb->spf16 : bpb->spf32) * bpb->nf) + bpb->rsc;
    // nr0,nr1(BPB_RootEntCnt)は、ルートディレクトリ直下に存在するディレクトリ数(FAT16 のみ有効)
    // todo: FAT32 の場合どうなるの？
    s = (bpb->nr0 + (bpb->nr1 << 8)) * sizeof(fatdir_t);
    if (bpb->spf16 == 0) {
        // adjust for FAT32
        root_sec += (bpb->rc-2) * bpb->spc;
    }
    // add partition LBA
    // 最初の FAT パーティションのアドレスに、ここまでで計算したオフセットを加算して絶対位置を計算
    root_sec += partitionlba;
    // load the root directory
    if (sd_readblock(root_sec, (unsigned char *)dir, s / 512 + 1)) {
        // iterate on each entry and check if it's the one we're looking for
        for (; dir->name[0] != 0; dir++) {
            // is it a valid entry?
            if (dir->name[0] == 0xE5 || dir->attr[0] == 0xF) {
                continue;
            }
            // filename match?
            if (!memcmp(dir->name, fn, 11)) {
                INFO("FAT File %s starts at cluster: %x",
                    fn, ((unsigned int)dir->ch) << 16 | dir->cl);
                // if so, return starting cluster
                // ch と cl を使って、このファイルの先頭クラスタ番号を取得番号を計算
                return ((unsigned int)dir->ch) << 16 | dir->cl;
            }
        }
        PANIC("ERROR: file not found");
    } else {
        PANIC("ERROR: Unable to load root directory");
    }
    return 0;
}

/**
 * Read a file into memory
 */
// cluster: 読みたいファイルの先頭クラスタ番号
char *fat_readfile(unsigned int cluster)
{
    void *buf = (void *)allocate_page();
    // BIOS Parameter Block
    bpb_t *bpb = (bpb_t *)buf;
    // File allocation tables. We choose between FAT16 and FAT32 dynamically
    // todo: ここも bpb は新規に割当てた領域なので bpb->rsc には無効な値しか入っていないはず
    unsigned int *fat32 = (unsigned int *)(buf + bpb->rsc * 512);
    unsigned short *fat16 = (unsigned short *)fat32;
    // Data pointers
    unsigned int data_sec, s;
    unsigned char *data, *ptr;
    // find the LBA of the first data sector
    // fat_getcluster と同じことをやっている
    data_sec = ((bpb->spf16 ? bpb->spf16 : bpb->spf32) * bpb->nf) + bpb->rsc;
    s = (bpb->nr0 + (bpb->nr1 << 8)) * sizeof(fatdir_t);
    if(bpb->spf16 > 0) {
        // adjust for FAT16
        data_sec += (s + 511) >> 9;
    }
    // add partition LBA
    data_sec += partitionlba;
    // dump important properties
    INFO("FAT Bytes per Sector: %x", bpb->bps0 + (bpb->bps1 << 8));
    INFO("FAT Sectors per Cluster: %x", bpb->spc);
    INFO("FAT Number of FAT: %x", bpb->nf);
    INFO("FAT Sectors per FAT: %x", (bpb->spf16 ? bpb->spf16 : bpb->spf32));
    INFO("FAT Reserved Sectors Count: %x", bpb->rsc);
    INFO("FAT First data sector: %x", data_sec);
    // load FAT table
    // 確保した領域の先頭から 512 バイトのところに FAT 領域を読み込む
    s = sd_readblock(partitionlba + 1, (unsigned char *)buf + 512,
                     (bpb->spf16 ? bpb->spf16 : bpb->spf32) + bpb->rsc);
    // end of FAT in memory
    data = ptr = buf + 512 + s;
    // iterate on cluster chain
    while (cluster > 1 && cluster < 0xFFF8) {
        // load all sectors in a cluster
        sd_readblock((cluster - 2) * bpb->spc + data_sec, ptr, bpb->spc);
        // move pointer, sector per cluster * bytes per sector
        ptr += bpb->spc * (bpb->bps0 + (bpb->bps1 << 8));
        // get the next cluster in chain
        cluster = bpb->spf16 > 0 ? fat16[cluster] : fat32[cluster];
    }
    return (char *)data;
}

