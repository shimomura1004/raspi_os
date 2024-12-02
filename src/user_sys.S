.section ".text.user"

.set HVC_NOTIFY_NUMBER, 0 		// hvc numbers
.set HVC_EXIT_NUMBER, 1
// .set SYS_FORK_NUMBER, 1
//.set SYS_EXIT_NUMBER, 2

// ただのビジーループ
.globl user_delay
user_delay:
	subs x0, x0, #1
	bne user_delay
	ret

// システムコールを呼び出すためのユーザプログラム側のコード

// hvc を呼び出してカーネル空間に入る
.globl call_hvc_notify
call_hvc_notify:
	// svc のハンドラ内で w8 の値を見て処理が分岐する
	// 通常は svc の引数の値で分離すると思うが、RPi OS では w8 の値を使っている
	// w0~w7 はシステムコールの引数に使うのが慣習
	mov w8, #HVC_NOTIFY_NUMBER
	hvc #0
	ret

.globl call_hvc_exit
call_hvc_exit:
	mov w8, #HVC_EXIT_NUMBER
	hvc #0
	ret
