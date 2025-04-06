# QEMU で起動するようにする
- カーネルイメージ kernel8.ig を -kernel オプションで指定してもダメ
    - -kernel オプションを使うと EL1 で動作してしまうため
- 以下のように -device を使うのがよい
    - > DISPLAY=:0 qemu-system-aarch64 -m 1024 -M raspi3b -device loader,file=./kernel8.img,addr=0x0 -nographic -serial null -serial mon:stdio -s
- gdb でデバッグする場合は -S オプションをつける
    - gdb が接続されるまで実行開始されないのでブート時の動きを追うときに便利
    - > $ gdb-multiarch
    - > (gdb) target remote localhost:1234
- アセンブリ1命令をステップ実行するには si を使う

# EL1 で動いていた OS を EL2 で動くようにする
- 発生した割込みを EL2 に飛ばすように設定する
    - hcr_el2 を設定する
- 元々 vbar_el1 にセットしていた割込みベクタを vbar_el2 にセットする
    - 割込みが EL2 に入るようになるのでハンドラも移動させる
- 元々 svc を使って EL1 に遷移していたものを hvc を使うようにする
    - svc: supervisor call
    - hvc: hypervisor call
