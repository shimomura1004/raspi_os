#include <stdint.h>
#include "mm.h"
#include "utils.h"
#include "printf.h"

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
    uint64_t section_header_offset; // セクションヘッダテーブルのいち
    uint32_t flags;                 // 各種フラグ
    uint16_t header_size;           // ELF ヘッダのサイズ
    uint16_t program_header_size;   // プログラムヘッダのサイズ
    uint16_t program_header_num;    // プログラムヘッダの数
    uint16_t section_header_size;   // セクションヘッダのサイズ
    uint16_t section_header_num;    // セクションヘッダの数
    uint16_t section_name_index;    // セクション名を格納するセクションのインデックス
};

// struct elf_program_header {
//     uint32_t type;                  // セグメントの種別
//     uint32_t offset;                // ファイル中の位置
//     uint32_t virtual_addr;          // 論理アドレス(VA)
//     uint32_t physical_addr;         // 物理アドレス(PA)
//     uint32_t file_size;             // ファイル中のサイズ
//     uint32_t memory_size;           // メモリ上でのサイズ
//     uint32_t flags;                 // 各種フラグ
//     uint32_t align;                 // アラインメント
// };

static int elf_check(struct elf_header *header)
{
    if (memcmp(header->id.magic, "\x7f" "ELF", 4)) {
        return -1;
    }

    if (header->id.class != 2) {
        return -1;  // ELF64
    }
    if (header->id.format != 1) {
        return -1;  // little endian
    }
    if (header->id.version != 1) {
        return -1;  // version 1
    }
    if (header->type != 2) {
        return -1;  // executable
    }
    if (header->version != 1) {
        return -1;  // version 1
    }
    if (header->arch != 0xb7) {
        return -1;  // AArch64
    }

    return 0;
}

static int elf_load_program(struct elf_header *header)
{
//     int i;
//     struct elf_program_header *phdr;

//     // プログラムヘッダの個数分(セグメントの個数分)ループする
//     for (i = 0; i < header->program_header_num; i++) {
//         // プログラムヘッダを取得
//         phdr = (struct elf_program_header *)
//             ((char *)header + header->program_header_offset +
//              header->program_header_size * i);

//         // ロード可能なセグメントかを確認
//         if (phdr->type != 1)
//             continue;
        
//         // 物理アドレス: 変数の初期値が格納されるアドレス
//         // 論理アドレス: プログラムが実行時にアクセスするアドレス
//         // プログラムが ROM に書き込まれる場合、実行時には書き換えられないので　物理 != 論理 になる

//         // elf ヘッダの先頭からオフセット分だけずらした位置から、
//         // 物理アドレスの場所に対象のセグメントをロードする
//         memcpy((char *)phdr->physical_addr, (char *)header + phdr->offset,
//                phdr->file_size);
//         // RAM 領域で、セグメントをコピーしたところから後ろをゼロクリアする
//         // bss 領域などは、elf ファイル上は実体は不要だが、RAM 上には領域が必要
//         // よって memory_size と file_size で差がでることがある
//         // ここはゼロクリアする
//         // おそらく elf ファイル上に実体がない領域はセグメントの後ろ側に集められる仕様になっている
//         memset((char *)phdr->physical_addr + phdr->file_size, 0,
//                phdr->memory_size - phdr->file_size);
//     }

    return 0;
}

// buf の位置に ELF ファイル全体がコピーされている前提で、正しい位置にロードする
uint8_t *elf_load(uint8_t *buf)
{
    struct elf_header *header = (struct elf_header *)buf;

    if (elf_check(header) < 0) {
        return NULL;
    }
    
    if (elf_load_program(header) < 0) {
        return NULL;
    }
    
    // elf ファイル内に書かれたエントリポイントのアドレスを返す
    return (uint8_t *)header->entry_point;
}
