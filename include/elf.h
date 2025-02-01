#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

struct elf_header {
    struct {
        uint8_t magic[4];           // elf のマジックナンバ
        uint8_t class;              // 32/64ビットの区別
        uint8_t format;             // エンディアン
        uint8_t version;            // elf フォーマットのバージョン
        uint8_t abi;                // OS の種別
        uint8_t abi_version;        // OS のバージョン
        uint8_t reserve[7];
    } id;
    uint16_t type;                  // ファイルの種別
    uint16_t arch;                  // CPU の種類
    uint32_t version;               // elf 形式のバージョン
    uint64_t entry_point;           // 実行開始アドレス
    uint64_t program_header_offset; // プログラムヘッダテーブルの位置
    uint64_t section_header_offset; // セクションヘッダテーブルの位置
    uint32_t flags;                 // 各種フラグ
    uint16_t header_size;           // ELF ヘッダのサイズ
    uint16_t program_header_size;   // プログラムヘッダのサイズ
    uint16_t program_header_num;    // プログラムヘッダの数
    uint16_t section_header_size;   // セクションヘッダのサイズ
    uint16_t section_header_num;    // セクションヘッダの数
    uint16_t section_name_index;    // セクション名を格納するセクションのインデックス
};

struct elf_program_header {
    uint32_t type;                  // セグメントの種別
    uint32_t flags;                 // 各種フラグ
    uint64_t offset;                // ファイル中の位置
    uint64_t virtual_addr;          // 論理アドレス(VA)
    uint64_t physical_addr;         // 物理アドレス(PA)
    uint64_t file_size;             // ファイル中のサイズ
    uint64_t memory_size;           // メモリ上でのサイズ
    uint64_t align;                 // アラインメント
};

// uint8_t *elf_load(uint8_t *buf);
int elf_check(struct elf_header *header);
int elf_load_program(struct elf_header *header);

#endif
