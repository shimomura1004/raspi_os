- [OP-TEE Documentation](https://optee.readthedocs.io/en/latest/architecture/index.html)

# Core
- Secure/Non-secure の切り替えは Secure monitor で行う
- Normal world から Secure world へ切り替えるには、SMC 命令を実行する
- SMC による動機例外は必ず Secure monitor でトラップされる
  - 必要に応じて Secure OS などに eret される
- 同様に、Secore world から Normal world へ戻るときにも SMC を使い、Secure monitor でトラップして Normall world に戻る

- Secure wolrd 用の割込みが入っ場合、Secore OS 側の割込みベクタで処理される
  - もしそのとき Normal world が実行されていたら、Secure monitor が割込みをトラップして Secure OS を起動する
- 同様に、normal world 用の割込みは normal world の割込みベクタで処理される
  - そのとき secure world が動いていたら、secure monitor が一時的に normal world に切り替えて割り込みを処理する

## Core exception vectors
