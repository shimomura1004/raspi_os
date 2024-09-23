.globl memcpy
memcpy:
	ldr x3, [x1], #8
	str x3, [x0], #8
	subs x2, x2, #8
	b.gt memcpy
	ret

.globl memzero
// x0: 開始アドレス
// x1: バイト数
memzero:
	// xzr は常に 0 を保持するレジスタ
	// x0 が指すアドレスに 0 を書き込み、 x0 を 8 バイト進める
	str xzr, [x0], #8
	// x1 = x1 - 8
	subs x1, x1, #8
	// x1 が 0 より大きい場合はもう一度
	b.gt memzero
	ret
