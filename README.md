# Link
- [https://github.com/matsud224/raspvisor](raspvisor)

# Memo
- RPi3 の DRAM(+デバイス) は、物理アドレスの 0 から 1GB にマップされている
    - 0x0000_0000_0000_0000 から 0x0000_0000_3fff_ffff
- RPi OS のカーネルのアドレス空間では、上記の 1GB のページ全体がリニアにマップされている
    - 0xffff_0000_0000_0000 から 0xffff_0000_3fff_ffff
- aarch64 では
    - アドレスの上位12ビットが 0 の場合は TTBR0 で指定した変換テーブルで仮想アドレスを変換する(ユーザプロセス用)
    - アドレスの上位12ビットが 1 の場合は TTBR1 で指定した変換テーブルで仮想アドレスを変換する(カーネル用)
- ハイパーバイザでは、Stage 1(VA->IPA)は通常のマッピング、Stage 2(IPA->PA) はリニアマッピング
- mrs: copy register from status register
    - mrs x0, esr_el2
- msr: copy status register from register
    - msr esr_el2, x0

# Debug
- flat binary にデバッグ情報を残すこともできるようだが、objcopy がうまくデバッグ情報を認識してくれない
- 以下のように gdb で kernel8.elf を追加で読み込むほうが楽
    - `(gdb) add-symbol-file build/kernel8.elf`
- 状況に応じて `layout asm` もしくは `layout src` で表示を切り替える
- デバッグ時は毎回 target remote するのは大変なので以下のようにコマンドラインで指定するといい
    - `$ gdb-multiarch -ex 'target remote :1234'`
