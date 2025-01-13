#include "sd.h"
#include "mm.h"
#include "debug.h"
#include "utils.h"
#include "fat32.h"

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

#define FAT32_MAX_FILENAME_LEN  255
#define BLOCKSIZE               512

// DIR_attribute
#define ATTR_READ_ONLY   0x01
#define ATTR_HIDDEN      0x02
#define ATTR_SYSTEM      0x04
#define ATTR_VOLUME_ID   0x08
#define ATTR_DIRECTORY   0x10
#define ATTR_ARCHIVE     0x20
#define ATTR_LONG_NAME   (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

struct mbr {
    uint8_t bootloader[446];
    struct {
        uint8_t bootflag;
        uint8_t first_chs[3];
        uint8_t type;
        uint8_t last_chs[3];
        uint32_t volume_first;
        uint32_t total_sector;
    } __attribute__((__packed__)) partitiontable[4];
    uint8_t bootsig[2];
} __attribute__((__packed__));

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
    uint8_t     LDIR_Ord;           // このエントリが保持する文字列(合計13文字)が何番目かを表す
                                    // FAT では複数の LFN エントリを使って長いファイル名(255文字)を表す
                                    // 上記のシーケンス番号は 1~20 で、 0x40 は最終エントリを表す
    uint8_t     LDIR_Name1[10];     // 最初の5文字(1文字あたり2バイト)
    uint8_t     LDIR_Attr;          // ファイル属性(LFN の場合は常に 0x0F)
    uint8_t     LDIR_Type;          // Long entry type. ネームエントリの場合は 0
    uint8_t     LDIR_Chksum;        // ショートファイル名のチェックサム
    uint8_t     LDIR_Name2[12];     // 次の6文字
    uint16_t    LDIR_FstClusLO;     // 常に 0
    uint8_t     LDIR_Name3[4];      // 最後の2文字
} __attribute__((__packed__));

// LFN エントリの終端を表す
//  LFN エントリの配列は逆順になっているので、このフラグを持つエントリは先頭に置かれるので注意
#define LAST_LONG_ENTRY 0x40

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

// 空きページを確保して、指定された LBA から 1ブロック分のデータを読み込む
static uint8_t *alloc_and_readblock(unsigned int lba) {
    uint8_t *buf = (uint8_t *)allocate_page();
    if (sd_readblock(lba, buf, 1) < 0) {
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
    if (!(boot->BS_FilSysType[0] == 'F' &&
          boot->BS_FilSysType[1] == 'A' &&
          boot->BS_FilSysType[2] == 'T')) {
        return 0;
    }

    return 1;
}

// 指定されたファイルに対し fat32_file 構造体を準備する
static void fat32_file_init(struct fat32_fs *fat32, struct fat32_file *fatfile,
                            uint8_t attr, uint32_t size, uint32_t cluster) {
    fatfile->fat32 = fat32;
    fatfile->attr = attr;
    fatfile->size = size;
    fatfile->cluster = cluster;
}

// ストレージから先頭の1ブロック(BPB)を読み込み、ルートディレクトリのエントリを初期化する
int fat32_get_handle(struct fat32_fs *fat32) {
    // 先頭の BPB を含むブロック(セクタ)をメモリ上に読み込む
    uint8_t *bbuf = alloc_and_readblock(0);

    struct mbr *mbr = (struct mbr *)bbuf;

    if (mbr->bootsig[0] != 0x55 || mbr->bootsig[1] != 0xaa) {
        WARN("invalid boot signature in MBR");
        return -1;
    }

    if (mbr->partitiontable[0].type != 0x0c) {
        WARN("not a FAT32 partition");
        return -1;
    }

    uint32_t volume_first = mbr->partitiontable[0].volume_first;
    free_page(bbuf);

    // 最初のパーティションの BPB(最初の lba)を bbuf に読み込む
    bbuf = alloc_and_readblock(volume_first);

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

    // このパーティションの最初のブロック番号を保存しておく
    fat32->volume_first = volume_first;

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
    uint8_t *bbuf = alloc_and_readblock(sector + fat32->volume_first);
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
            bbuf = alloc_and_readblock(sector + fat32->volume_first);
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
static uint8_t calculate_checksum(struct fat32_direntry *entry) {
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

// LFN エントリから long file name を取得する
// FAT では、ファイル名が長いとき、複数の連続したディレクトリエントリ(LFN)を使ってファイル名を保存する
// 複数の LFN で長いファイル名を保持したあと、その直後には SFN が続く決まりになっている
// よって、最後の SFN から逆順に LFN をたどっていくことで長いファイル名を取得できる
//   長いファイル名自体も逆順に入っているので、LFN を逆順にたどりつつ先頭から順に読めばよい
//   ファイル名が "Long File Name.txt" のときはこうなっている
//     LFN: "e.txt" "      " "  "
//     LFN: "Long " "File N" "am"
//     SFN: "LONG FI~TXT"
static char *get_lfn(struct fat32_direntry *sfnent, size_t sfnoff, struct fat32_direntry *prevblk_dent) {
    struct fat32_lfnentry *lfnent = (struct fat32_lfnentry *)sfnent;
    // ショートファイル名のチェックサムを計算
    uint8_t checksum = calculate_checksum(sfnent);

    // 読み込んだロングファイル名はスタティック変数に格納するので同時に呼び出してはいけない
    static char name[256];
    char *ptr = name;
    int seq = 1;
    int is_prev_blk = 0;

    while(1) {
        // ブロック境界をまたぐときは別途受け取った prevblk_dent から読み込む
        //   ファイル名は255文字以下で2回ブロックをまたぐことはないことを前提にしている
        // BLOCKSIZE が 2 のべき乗であるとき
        // (sfnoff & (BLOCKSIZE - 1)) == 0 は sfnoff % BLOCKSIZE == 0 と同じ
        // つまり sfnoff が BLOCKSIZE の倍数かどうかをチェックしている
        // ビット演算で剰余を高速に計算できる
        if (!is_prev_blk && (sfnoff & (BLOCKSIZE - 1)) == 0) {
            // block boundary
            lfnent = (struct fat32_lfnentry *)prevblk_dent;
            is_prev_blk = 1;
        }
        else {
            // LFN エントリは連続して並んでいるので、単純にポインタをデクリメントすればいい
            lfnent--;
        }

        // ATTR_LONG_NAME
        //   ATTR_LONG_NAME は複数のビットがセットされているので、ビット論理積を取ったあと、
        //   それが ATTR_LONG_NAME と等しいかどうかをチェックしている
        // LDIR_Ord
        //   LFN のシーケンス番号(LDIR_Ord)は 1~20 なので、5ビットでおさまる
        //   自分が管理しているシーケンス番号(seq)と一致しているかどうかをチェックする
        if (lfnent == NULL || lfnent->LDIR_Chksum != checksum ||
            (lfnent->LDIR_Attr & ATTR_LONG_NAME) != ATTR_LONG_NAME ||
            (lfnent->LDIR_Ord & 0x1f) != seq++) {
            return NULL;
        }

        // 3つに分かれた部分を順に読みだして name に格納していく
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
        // LFN エントリの列を読み切ったら抜ける
        if (lfnent->LDIR_Ord & LAST_LONG_ENTRY) {
            break;
        }

        sfnoff -= sizeof(struct fat32_direntry);
    }

    *ptr = '\0';
    return name;
}

// 指定されたクラスタ(ファイル)内の指定されたオフセット位置(file_off)に対応するセクタ番号を返す
// file_off はバイト単位
static uint32_t fat32_firstblk(struct fat32_fs *fat32, uint32_t cluster, size_t file_off) {
    uint32_t secs_per_clus = fat32->boot.BPB_SecPerClus;
    // file_off % (...) は、クラスタ内でのオフセットを計算している
    // それを BLOCKSIZE で割ることで、オフセット位置が含まれるブロックを計算
    uint32_t remblk = file_off % (secs_per_clus * BLOCKSIZE) / BLOCKSIZE;
    return cluster_to_sector(fat32, cluster) + remblk;
}

// 次のブロック(セクタ)番号を返す
// 次のクラスタに続く場合は、そのクラスタの最初のブロック(セクタ)番号を返す
static int fat32_nextblk(struct fat32_fs *fat32, int prevblk, uint32_t *cluster) {
  uint32_t secs_per_clus = fat32->boot.BPB_SecPerClus;
  if (prevblk % secs_per_clus != secs_per_clus - 1) {
      // prevblk がクラスタ内の最後のブロック(セクタ)でない場合は、次のブロック(セクタ)番号を返す
    return prevblk + 1;
  } else {
    // クラスタをまたぐときは、次のクラスタを cluster 変数に読み込み
    // そして、そのクラスタの最初のブロック(セクタ)番号を返す
    // go over a cluster boundary
    *cluster = fatentry_read(fat32, *cluster);
    return fat32_firstblk(fat32, *cluster, 0);
  }
}

// 指定されたディレクトリ(fatfile)ないから特定のファイル名(name)を探す
// 子ディレクトリに対して再帰的に探索することはしない
static int fat32_lookup_main(struct fat32_file *fatfile, const char *name,
                             struct fat32_file *found) {
    struct fat32_fs *fat32 = fatfile->fat32;
    // fatfile がディレクトリでない場合はエラー終了
    if (!(fatfile->attr & ATTR_DIRECTORY)) {
        return -1;
    }

    uint8_t *prevbuf = NULL;
    uint8_t *bbuf = NULL;
    uint32_t current_cluster = fatfile->cluster;

    // ディレクトリの中身を保持するブロック(セクタ)番号を取得
    int blkno = fat32_firstblk(fat32, current_cluster, 0);
    while (is_active_cluster(current_cluster)) {
        // ディレクトリの中身を読み込む
        bbuf = alloc_and_readblock(blkno + fat32->volume_first);

        // ブロックを先頭から順番に見ていく
        for (uint32_t i = 0; i < BLOCKSIZE; i += sizeof(struct fat32_direntry)) {
            struct fat32_direntry *dent = (struct fat32_direntry *)(bbuf + i);
            if (dent->DIR_Name[0] == 0x00) {
                // 未使用エントリがきたら、このディレクトリにはこれ以上ファイルがない
                // Microsoft Extensible Firmware Initiative FAT32 File System Specification
                //   If DIR_Name[0] == 0x00, then the directory entry is free (same as for 0xE5),
                //   and there are no allocated directory entries after this one (all of
                //   the DIR_Name[0] bytes in all of the entries after this one are also set to 0).
                //   The special 0 value, rather than the 0xE5 value, indicates to FAT file system
                //   driver code that the rest of the entries in this directory do not need to be
                //   examined because they are all free.
                break;
            }
            if (dent->DIR_Name[0] == 0xe5) {
                // 削除済みエントリ
                continue;
            }
            if (dent->DIR_Attr & (ATTR_VOLUME_ID | ATTR_LONG_NAME)) {
                // ボリューム ID やロングファイル名のエントリは無視
                continue;
            }

            char *dent_name = NULL;
            // この LFN エントリの列がブロック境界をまたぐ可能性があるので
            // 前のブロック(prevbuf)の末尾のエントリへのポインタを渡す
            dent_name = get_lfn(
                dent, i,
                prevbuf ? (struct fat32_direntry *)(prevbuf + (BLOCKSIZE - sizeof(struct fat32_direntry)))
                        : NULL);
            if (dent_name == NULL) {
                dent_name = get_sfn(dent);
            }

            if (strncmp(name, dent_name, FAT32_MAX_FILENAME_LEN) == 0) {
                // ファイル名が一致した場合は、そのファイルのクラスタ番号を計算
                uint32_t dent_clus =
                    (dent->DIR_FstClusHI << 16) | dent->DIR_FstClusLO;
                if (dent_clus == 0) {
                    // root directory
                    dent_clus = fat32->boot.BPB_RootClus;
                }
                // 見つけたエントリに対し fat32_file 構造体を準備して終了
                fat32_file_init(fat32, found, dent->DIR_Attr,
                                dent->DIR_FileSize, dent_clus);
                goto file_found;
            }
        }
        // ブロックを読み切ったが見つからなかった場合は次のブロックに移動して繰り返す
        if (prevbuf != NULL) {
            free_page(prevbuf);
        }
        prevbuf = bbuf;
        bbuf = NULL;
        blkno = fat32_nextblk(fat32, blkno, &current_cluster);
    }

    // ファイルが見つからなかった場合はエラー終了
    if (prevbuf != NULL) {
        free_page(prevbuf);
    }
    if (bbuf != NULL) {
        free_page(bbuf);
    }
    return -1;

file_found:
    if (prevbuf != NULL) {
        free_page(prevbuf);
    }
    if (bbuf != NULL) {
        free_page(bbuf);
    }
    return 0;
}

// ルートディレクトリからファイルを探す
int fat32_lookup(struct fat32_fs *fat32, const char *name,
                 struct fat32_file *fatfile) {
    return fat32_lookup_main(&fat32->root, name, fatfile);
}

// 指定されたファイル(fatfile)から buf にデータを読み込む
int fat32_read(struct fat32_file *fatfile, void *buf, unsigned long offset,
               size_t count) {
    struct fat32_fs *fat32 = fatfile->fat32;
    if (offset < 0) {
        return -1;
    }

    uint32_t tail = MIN(count + offset, fatfile->size);
    uint32_t remain = tail - offset;
    if (tail <= offset) {
        return 0;
    }

    uint32_t current_cluster =
        walk_cluster_chain(fat32, offset, fatfile->cluster);
    uint32_t inblk_off = offset % BLOCKSIZE;

    for (int blkno = fat32_firstblk(fat32, current_cluster, offset);
         remain > 0 && is_active_cluster(current_cluster);
         blkno = fat32_nextblk(fat32, blkno, &current_cluster)) {
        uint8_t *bbuf = alloc_and_readblock(blkno + fat32->volume_first);
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

// 指定したファイルのファイルサイズを返す
int fat32_file_size(struct fat32_file *fatfile) {
    return fatfile->size;
}

// 指定したファイルがディレクトリかどうかを返す
int fat32_is_directory(struct fat32_file *fatfile) {
    return (fatfile->attr & ATTR_DIRECTORY) != 0;
}
