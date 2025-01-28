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
- gdb で VTTBR_EL2 を調べる
    - `info registers VTTBR_EL2`
- Hypervisor 環境と gdb を組み合わせる場合、ブレークポイントは仮想アドレスに対して設定する
    - CPU は MMU を介して常に仮想アドレス上で実行されている
    - 複数のゲスト OS は同じ仮想アドレスを使うので、どのゲスト OS がブレークされたかわからない
        - `info registers VTTBR_EL2` で VMID を見れば、どのゲストがブレークされたか確認できる

# BCM2873
- BCM2873 のメモリマップは以下のマニュアルに書かれている
    - BCM2837-ARM-Peripherals.-.Revised.-.V2-1.pdf
- ![alt text](docs/bcm2873_memmap.png)
    - 右端が仮想アドレスで、MMU を使って CPU の物理アドレス(ARM Physical Adress)に変換される
    - 真ん中が CPU の物理アドレス(ARM Physical Address)で、VC/ARM MMU によってバスアドレス(VC CPU Bus Address)に変換される
    - 左端がバスアドレス(VC CPU Bus Address)で、ボードの内容がそのまま配置されるようなアドレス空間である(合計1GB の SDRAM が不連続に配置されていたりする)
- RPi3b には IOMMU や SMMU はないので、たとえば DMA コントローラでは物理アドレスを直接指定する必要がある
