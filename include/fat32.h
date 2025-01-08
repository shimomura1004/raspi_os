#ifndef _FAT32_H
#define _FAT32_H

#include <stddef.h>
#include <inttypes.h>

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
    uint32_t    volume_first;       // FAT32 パーティションの開始セクタ番号
    struct      fat32_file root;    // ルートディレクトリ
};

int fat32_get_handle(struct fat32_fs *);
int fat32_lookup(struct fat32_fs *, const char *, struct fat32_file *);
int fat32_read(struct fat32_file *, void *, unsigned long, size_t);
int fat32_file_size(struct fat32_file *);
int fat32_is_directory(struct fat32_file *);

#endif
