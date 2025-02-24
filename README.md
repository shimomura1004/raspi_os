# Link
- [https://github.com/matsud224/raspvisor](https://github.com/matsud224/raspvisor)

# Memo
- RPi3 の DRAM(+デバイス) は、物理アドレスの 0 から 1GB にマップされている
    - 0x0000_0000_0000_0000 から 0x0000_0000_3fff_ffff
- RPi OS のカーネルのアドレス空間では、上記の 1GB のページ全体がリニアにマップされている
    - 0xffff_0000_0000_0000 から 0xffff_0000_3fff_ffff
- aarch64 では
    - アドレスの上位12ビットが 0 の場合は TTBR0_el1 で指定した変換テーブルで仮想アドレスを変換する(ユーザプロセス用)
    - アドレスの上位12ビットが 1 の場合は TTBR1_el1 で指定した変換テーブルで仮想アドレスを変換する(カーネル用)
    - 二段階アドレス変換を行う場合、 VTTBR_EL2 で指定した変換テーブルで中間物理アドレスを変換する
- mrs: copy register from status register
    - mrs x0, esr_el2
- msr: copy status register from register
    - msr esr_el2, x0

- ゲスト OS (kernel8.elf)は 0xffff_... に配置されるようにコンパイルされているが、objcopy で 0x0000_... に配置するように変換されている
- その結果、ゲスト OS がロードされる位置が本来 EL0 アプリ用の位置になっており、かつ TTBR0_EL1 が設定されていないのでクラッシュしていた
- elf ファイルをロードすれば正しく配置できると考えられる

- aarch64 で扱える論理アドレスは48ビットまで
    - EL1 では 0xffff... で始まるアドレスを TTBR1_EL1 で指定した変換テーブルで変換できる
    - EL2 にはそのような仕組みはないので扱うことができない(上位16ビットは単に無視される)
- EL1 の IPA は VTTBR_EL2 で PA に変換される(EL2 の VA に変換されるわけではない)
- EL2 の VA は TTBR0_EL2 で PA に変換される

- synchronous 例外が発生した場合、PC が 0x200 にジャンプする

- qemu を `-kernel` オプション付きで実行すると EL2 で起動される

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
- gdb で現在の EL を見るには CPSR レジスタを読む
    - `i r cpsr`
    - CPSR[3:2] が Exception Level なので、たとえば 0x3c5 の場合は3~0 ビットが 0b0101 なので EL1 であることがわかる
- MMU が有効かどうか確認するには SCTLR を読む
    - `i r SCTLR`
    - 0 ビット目が 1 なら MMU は有効

# BCM2873
- BCM2873 のメモリマップは以下のマニュアルに書かれている
    - BCM2837-ARM-Peripherals.-.Revised.-.V2-1.pdf
- ![alt text](docs/bcm2873_memmap.png)
    - 右端が仮想アドレスで、MMU を使って CPU の物理アドレス(ARM Physical Adress)に変換される
    - 真ん中が CPU の物理アドレス(ARM Physical Address)で、VC/ARM MMU によってバスアドレス(VC CPU Bus Address)に変換される
    - 左端がバスアドレス(VC CPU Bus Address)で、ボードの内容がそのまま配置されるようなアドレス空間である(合計1GB の SDRAM が不連続に配置されていたりする)
- RPi3b には IOMMU や SMMU はないので、たとえば DMA コントローラでは物理アドレスを直接指定する必要がある

# Memory Mapping
## 二段階アドレス変換
- https://www.starlab.io/blog/deep-dive-mmu-virtualization-with-xen-on-arm
![alt text](docs/memory_mapping.png)
- AArch64 のハイパーバイザ環境では、ゲスト OS のプロセスが扱う仮想アドレスは、ゲスト OS によって中間物理アドレス IPA に変換される
    - このとき使われるテーブルは、ネイティブ環境の場合と同じで TTBR0_EL1 もしくは TTBR1_EL1 で指定されたもの
- この IPA は、ハイパーバイザによって準備される stage2 の変換テーブルによって物理アドレス PA に変換される
    - このとき使われるテーブルは、VTTBR0_EL2 という特別なもの
- またハイパーバイザ自身の仮想アドレスの変換は TTBR0_EL2 という別のレジスタで指定されるテーブルを使う
- テーブルが別なので、ゲストの IPA とハイパーバイザの VA が同じアドレスを使っても問題ない
- ゲスト OS の MMU が無効になっている場合は、VA=IPA となり、その場合でも Stage2 アドレス変換は実行される

## TTBR0_EL1 と TTBR1_EL1 と VTTBR_EL2
- TTBR0_EL1/TTBR1_EL1 はどちらも VA を PA もしくは IPA に変換するためのテーブルだが、担当するアドレスの範囲が異なる
    - TTBR0_EL1 は 0x0000_0000_0000_0000 から 0x0000_ffff_ffff_ffff まで
        - 通常はユーザプロセス用に使う
    - TTBR1_EL1 は 0xffff_0000_0000_0000 から 0xffff_ffff_ffff_ffff まで
        - 通常はカーネル用に使う
- 併用する場合は異なる変換テーブルを用意しないといけない
- VTTBR_EL2 は IPA を PA に変換するためのテーブルで、上記とは完全に別のアドレス空間になる
    - VTTBR により VM の IPA がホストの VA にマップされたりするわけではない

- ゲストの起動直後はまだ MMU が MMU は無効なので、ゲストの VA=IPA となる
- そのため PC が 0xffff000000000000 の場合、VTTBR_EL2 で IPA の 0xffff000000000000 を PA に変換できないといけない

# Multicore
- aarch64 では、多くのレジスタは CPU コアごとに独立して存在している
- armv8.1 以降の CPU では、いわゆる compare-and-swap 命令である casa/casal が使える
- それ以前の CPU の場合は ldaxr/stlxr を使う
    - ldaxr x1, [x0]: アドレス x0 が指す値を x1 に読み込みつつ、排他モニタを有効にする
        - 他の CPU がモニタ中のアドレスに書き込むと、モニタがクリアされる
    - stxr w3, x2, [x0]: 実行するときに排他モニタがクリアされていると書き込みに失敗する
        - 失敗した場合はアドレス x0 の内容は更新されず、w3 に 1 がセットされる
