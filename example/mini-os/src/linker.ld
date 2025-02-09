ENTRY("_start")
SECTIONS
{
	/* カーネル空間に配置するので 0xffff... に配置する */
	. = 0xffff000000000000;
	.text.boot : { *(.text.boot) }
	. = ALIGN(0x00001000);
	user_begin = .;
	.text.user : { build/user* (.text) }
	.rodata.user : { build/user* (.rodata) }
	.data.user : { build/user* (.data) }
	.bss.user : { build/user* (.bss) }
	user_end = .;
	.text :  { *(.text) }
	.rodata : { *(.rodata) }
	.data : { *(.data) }
	. = ALIGN(0x8);
	bss_begin = .;
	.bss : { *(.bss*) } 
	bss_end = .;
	. = ALIGN(0x00001000);
	/* もし TTBR0_EL1 と TTBR1_EL1 を使い分けるなら、pg_dir を2つ用意しないといけない */
	/* todo: 0xffff000000000000 に配置しているのに TTBR0_EL1 が必要になるのは
	         flat binary を使っているからで、ELF をロードすれば不要になるはず */
	pg_dir = .;
	.data.pgd : { . += (3 * (1 << 12)); }
	/*
	pg_dir_high = .;
	.data.pgd_high : { . += (3 * (1 << 12)); }
	*/
}

