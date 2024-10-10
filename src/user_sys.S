.section ".text.user"

.set SYS_WRITE_NUMBER, 0 		// syscal numbers 
.set SYS_FORK_NUMBER, 1 	
.set SYS_EXIT_NUMBER, 2 	

// ただのビジーループ
.globl user_delay
user_delay:
	subs x0, x0, #1
	bne user_delay
	ret

// svc を呼び出してカーネル空間に入る
.globl call_sys_write
call_sys_write:
	// svc のハンドラ内で w8 の値を見て処理が分岐する
	// 通常は svc の引数の値で分離すると思うが、RPi OS では w8 の値を使っている
	// w0~w7 はシステムコールの引数に使うのが習慣
	mov w8, #SYS_WRITE_NUMBER	
	svc #0
	ret

.globl call_sys_exit
call_sys_exit:
	mov w8, #SYS_EXIT_NUMBER	
	svc #0
	ret

.globl call_sys_fork
call_sys_fork:
	mov w8, #SYS_FORK_NUMBER	
	svc #0
	ret
