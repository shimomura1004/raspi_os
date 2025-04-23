#ifndef _HYPERCALL_TYPE_H
#define _HYPERCALL_TYPE_H

enum HYPERCALL_TYPE {
    // HVC の番号をデバッグ出力
	HYPERCALL_TYPE_WARN_LU = 0,
    // 第1引数の整数をデバッグ出力
    HYPERCALL_TYPE_INFO_LX,
    // 第1,2引数の整数をデバッグ出力
    HYPERCALL_TYPE_INFO_LX_LX,
    // 第1,2,3引数の整数をデバッグ出力
	HYPERCALL_TYPE_INFO_LX_LX_LX,
    // 第1,2,3,4引数負整数をデバッグ出力
    HYPERCALL_TYPE_INFO_LX_LX_LX_LX,

    HYPERCALL_TYPE_INFO_STR,

    HYPERCALL_TYPE_CREATE_VM,
};

#endif