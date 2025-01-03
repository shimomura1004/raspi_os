#include <inttypes.h>
#include <stddef.h>
#include "sd.h"
#include "debug.h"
#include "mm.h"
#include "utils.h"

// FAT パーティションの先頭512バイトには BPB があり、その次に FAT がある
// https://zenn.dev/hidenori3/articles/3ce349c02e79fa
// 上記の図には書かれていないが、BPB のあとには予約領域として一定数のセクタが確保されている
// https://us.mrtlab.com/filesystem/FAT-summary-two_2.html
// ユーザデータ領域だけでなく、BPB や FAT といった領域もすべてセクタ単位で管理されている
// FAT ではセクタをいくつかまとめてクラスタとしており、一対一対応する
// ファイルシステムはクラスタの上に構築され、単一ファイルでも物理的には不連続になりうる

// BPB: 各パーティションの最初のセクタに存在する領域で
//      ファイルシステムの物理レイアウトやパラメータを記録するもの
// セクタ: ディスク上の最小のデータ単位(通常は512バイト)
// クラスタ: 複数のセクタをまとめたもので、ファイルを格納する単位
//          (4KB(8セクタ) や 32KB(64セクタ) などが多い)
//          ファイルはクラスタ単位で格納され、大きいファイルでは複数のクラスタが使われる
//          また、クラスタ内には使われないセクタができうる
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
    uint32_t    FSI_LeadSig;        // 0x41615252
    uint8_t     FSI_Reserved1[480]; // 予約領域
    uint32_t    FSI_StrucSig;       // 0x61417272
    uint32_t    FSI_Free_Count;     // ボリューム内で未使用のクラスタ数(0xffffffff なら不明)
                                    // 間違っている可能性があるので信じず検証したほうがいい
    uint32_t    FSI_Nxt_Free;       // ドライバが次の空きクラスタを探すときに使うべきクラスタ番号
                                    // 0xffffffff なら不明なので 2(root) から探し始めないといけない
    uint8_t     FSI_Reserved2[12];  // 予約領域
    uint32_t    FSI_TrailSig;       // 0xaa550000
} __attribute__((__packed__));

// DIR_attribute
#define ATTR_READ_ONLY   0x01
#define ATTR_HIDDEN      0x02
#define ATTR_SYSTEM      0x04
#define ATTR_VOLUME_ID   0x08
#define ATTR_DIRECTORY   0x10
#define ATTR_ARCHIVE     0x20
#define ATTR_LONG_NAME   (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

// directory entry
struct fat32_direntry {
    uint8_t     DIR_Name[11];       // ファイル名(8文字) + 拡張子(3文字)
    uint8_t     DIR_Attr;           // ファイル属性
    uint8_t     DIR_NTRes;          // Windows NT 用の予約領域
    uint8_t     DIR_CrtTimeTenth;   // ファイル作成時のタイムスタンプ(1/100 秒)
                                    // ただし Microsoft による仕様では 1/10 秒となっている
    uint16_t    DIR_CrtTime;        // ファイル作成時のタイムスタンプ
                                    // 時 5bit
                                    // 分 6bit
                                    // 秒 5bit(ただし2倍する)
    uint16_t    DIR_CrtDate;        // ファイル作成時の日付
                                    // 年 7bit (1980年からの差分)
                                    // 月 4bit
                                    // 日 5bit
    uint16_t    DIR_LstAccDate;     // 最終アクセス日(フォーマットは DIR_CrtDate と同じ)
    uint16_t    DIR_FstClusHI;      // このエントリの先頭のクラスタ番号の上位16ビット
    uint16_t    DIR_WrtTime;        // 最終更新時間(フォーマットは DIR_CrtTime と同じ)
    uint16_t    DIR_WrtDate;        // 最終更新日(フォーマットは DIR_CrtDate と同じ)
    uint16_t    DIR_FstClusLO;      // このエントリの先頭のクラスタ番号の下位16ビット
    uint32_t    DIR_FileSize;       // ファイルサイズ(バイト単位)
} __attribute__((__packed__));

// long file name entry
struct fat32_lfnentry {
    uint8_t     LDIR_Ord;           // このエントリの文字列(合計13文字)の位置を表す
                                    // 複数のエントリを組み合わせて長いファイル名(255文字)を表す
                                    // 255文字のうちこのエントリの13文字をどこに置くか？を表す
    uint8_t     LDIR_Name1[10];     // 最初の5文字(1文字あたり2バイト)
    uint8_t     LDIR_Attr;          // ファイル属性(LFN の場合は常に 0x0F)
    uint8_t     LDIR_Type;          // Long entry type. ネームエントリの場合は 0
    uint8_t     LDIR_Chksum;        // ショートファイル名のチェックサム
    uint8_t     LDIR_Name2[12];     // 次の6文字
    uint16_t    LDIR_FstClusLO;     // 常に 0
    uint8_t     LDIR_Name3[4];      // 最後の2文字
} __attribute__((__packed__));

enum fat32_file_type {
    FAT32_REGULAR,
    FAT32_DIR,
};

// ファイルを表す構造体
struct fat32_file {
    struct      fat32_fs *fat32;    // このファイルが所属するファイルシステム
    uint8_t     attr;               // ファイル属性
    uint32_t    size;               // ファイルサイズ
    uint32_t    cluster;            // ファイルの先頭クラスタ
};

// ファイルシステムを表す構造体
struct fat32_fs {
    struct      fat32_boot boot;    // BPB
    struct      fat32_fsi fsi;      // FSInfo
    uint32_t    fatstart;           // FAT 領域の先頭(セクタ単位のオフセット)
    uint32_t    fatsectors;         // FAT 領域のセクタ数
    uint32_t    rootstart;          // ルートディレクトリのエントリの開始位置(セクタ単位のオフセット)
    uint32_t    rootsectors;        // ルートディレクトリのエントリに使われているセクタ数
    uint32_t    datastart;          // (セクタ単位のオフセット)
    uint32_t    datasectors;        //
    struct      fat32_file root;    // ルートディレクトリ
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

// 空きページを確保して、指定された LBA から 1ブロック分のデータを読み込む
static uint8_t *alloc_and_readblock(unsigned int lba) {
    uint8_t *buf = (uint8_t *)allocate_page();
    if (sd_readblock(lba, buf, 1)) {
        PANIC("sd_readblock() failed.");
    }
    return buf;
}

// BPB が正しいかどうかをチェックする
static int fat32_is_valid_boot(struct fat32_boot *boot) {
    if (boot->BS_BootSign != 0xaa55) {
        return 0;
    }
    if (boot->BPB_BytsPerSec != BLOCKSIZE) {
        return 0;
    }
    return 1;
}

// 指定されたファイルシステムにファイルを作る
static void fat32_file_init(struct fat32_fs *fat32, struct fat32_file *fatfile,
                            uint8_t attr, uint32_t size, uint32_t cluster) {
    fatfile->fat32 = fat32;
    fatfile->attr = attr;
    fatfile->size = size;
    fatfile->cluster = cluster;
}

// ストレージから先頭の1ブロック(BPB)を読み込み、ルートディレクトリのエントリを初期化する
int fat32_get_handle(struct fat32_fs *fat32) {
    // 先頭の BPB を一時的にメモリ上に読み込む
    uint8_t *bbuf = alloc_and_readblock(FAT32_BOOT);
    // boot 構造体を値コピーして一時的に確保した領域は解放
    fat32->boot = *(struct fat32_boot *)bbuf;
    struct fat32_boot *boot = &(fat32->boot);
    free_page(bbuf);

    // BPB 領域のあと、いくつかのセクタが予約領域として確保されており、そのあとに FAT 領域がある
    fat32->fatstart = boot->BPB_RsvdSecCnt;
    // FAT 領域は隙間なく並んでいるので単純に FAT のセクタ数を FAT の数で掛ける
    fat32->fatsectors = boot->BPB_FATSz32 * boot->BPB_NumFATs;
    // FAT 領域の直後からユーザデータ領域が始まる
    fat32->rootstart = fat32->fatstart + fat32->fatsectors;
    // ルートディレクトリのエントリ数から、必要なセクタ数を計算
    //   データ長 size とブロックの大きさ block_size から、必要なブロック数 count を計算
    //   count = (size + block_size - 1) / block_size
    // これと同じことをやっている
    fat32->rootsectors =
        (sizeof(struct fat32_direntry) * boot->BPB_RootEntCnt + boot->BPB_BytsPerSec - 1) /
        boot->BPB_BytsPerSec;
    // ルートディレクトリ用のエントリのあとにある部分を data と呼ぶ？
    fat32->datastart = fat32->rootstart + fat32->rootsectors;
    // 残りはすべてデータ領域
    fat32->datasectors = boot->BPB_TotSec32 - fat32->datastart;

    // BPB が無効だったり、データセクタ数が 65526 未満だったりしたらエラー
    // todo: なぜデータのセクタ数が少ないとエラーになる？
    if (fat32->datasectors / boot->BPB_SecPerClus < 65526 || !fat32_is_valid_boot(boot)) {
        WARN("Bad fat32 filesystem.");
        return -1;
    }

    fat32_file_init(fat32, &(fat32->root), ATTR_DIRECTORY, 0, fat32->boot.BPB_RootClus);

    return 0;
}

// ファイルシステム fs の index 番目の FAT エントリを読み込む
static uint32_t fatentry_read(struct fat32_fs *fat32, uint32_t index) {
    struct fat32_boot *boot = &fat32->boot;
    // FAT 領域も当然セクタで管理されているので、読みたい FAT エントリが入っているセクタを計算する
    // FAT32 ではエントリ1つが4バイトなので、セクタあたりのバイト数で割ることで
    // FAT 領域内のどのセクタに入っているかを計算できる
    // そこに FAT 領域自体が入っているセクタ数(fatstart)を足すことで読み込むべきセクタ番号がわかる
    uint32_t sector = fat32->fatstart + (index * 4 / boot->BPB_BytsPerSec);
    // 見つけたセクタ内でのエントリの位置は、上記の計算式の剰余
    uint32_t offset = index * 4 % boot->BPB_BytsPerSec;

    // まずストレージからセクタを1つ分読み取る
    uint8_t *bbuf = alloc_and_readblock(sector);
    // セクタ内のオフセットを考慮し、狙ったエントリを読み取る
    // ただし FAT32 では上位4ビットは予約されており 0 にする必要があるので 0x0fffffff でマスクする
    uint32_t entry = *((uint32_t *)(bbuf + offset)) & 0x0fffffff;
    free_page(bbuf);
    return entry;
}

// クラスタのリンクリストをたどって、指定されたオフセット位置に対応するクラスタ番号を返す
static uint32_t walk_cluster_chain(struct fat32_fs *fat32, uint32_t offset, uint32_t cluster) {
    // オフセット位置が何クラスタ目にあるか(何回クラスタをたどらなくてはいけないか)を計算
    int clusters_to_traverse =
        offset / (fat32->boot.BPB_SecPerClus * fat32->boot.BPB_BytsPerSec);

    struct fat32_boot *boot = &fat32->boot;
    uint8_t *bbuf = NULL;
    uint32_t prevsector = 0;

    for (int i = 0; i < clusters_to_traverse; i++) {
        // 今見ているクラスタの FAT エントリが含まれるクラスタ番号と、その中でのオフセットを計算
        uint32_t sector = fat32->fatstart + (cluster * 4 / boot->BPB_BytsPerSec);
        uint32_t offset = cluster * 4 % boot->BPB_BytsPerSec;

        if (prevsector != sector) {
            // 直前に見ていたセクタと異なる場合は、新たにセクタを読み込む
            if (bbuf) {
                free_page(bbuf);
            }
            bbuf = alloc_and_readblock(sector);
        }

        // 次のクラスタ番号を取得
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

// クラスタ番号を受け取り、セクタ番号(ストレージ上の物理アドレス)を返す
// クラスタ番号とセクタ番号は線形に対応しているので単純な計算式になる
// (ファイルは複数の不連続なクラスタにまたがって作られる)
static uint32_t cluster_to_sector(struct fat32_fs *fat32, uint32_t cluster) {
    return fat32->datastart + (cluster - 2) * fat32->boot.BPB_SecPerClus;
}

// ショートファイル名(8文字 + 拡張子3文字)のチェックサムを計算する
static uint8_t create_checksum(struct fat32_direntry *entry) {
    int i;
    uint8_t checksum;

    // 11 = 8 + 3
    for (i = 0, checksum = 0; i < 11; i++) {
        checksum = (checksum >> 1) + (checksum << 7) + entry->DIR_Name[i];
    }

    return checksum;
}

// short file name を取得する
// FAT では、ファイル名の先頭バイトには特別な意味がある
//   0x00: 未使用のエントリ
//   0xe5: ファイルが削除され、現在は未使用となっているエントリ
//   0x05: 日本語のエンコーディングによっては 0xe5 が通常の文字として使われることがある
//         これがファイル削除のマーカと間違えられないよう、0xe5 のかわりに 0x05 が使われる
//         よってファイル名として読む場合には 0x05 は 0xe5 に置き換える必要がある
static char *get_sfn(struct fat32_direntry *sfnent) {
    static char name[13];
    char *ptr = name;
    // ファイル名
    for (int i = 0; i < 8; i++, ptr++) {
        *ptr = sfnent->DIR_Name[i];
        if (*ptr == 0x05) {
            *ptr = 0xe5;
        }
        if (*ptr == ' ') {
            break;
        }
    }

    if (sfnent->DIR_Name[8] != ' ') {
        *ptr++ = '.';
        // 拡張子
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
