#ifndef _HYPERCALL_TYPE_H
#define _HYPERCALL_TYPE_H

// デバッグ出力用
#define HYPERCALL_TYPE_WARN_LU              0   // HVC の番号をデバッグ出力
#define HYPERCALL_TYPE_INFO_LX              1   // 第1引数の整数をデバッグ出力
#define HYPERCALL_TYPE_INFO_LX_LX           2   // 第1,2引数の整数をデバッグ出力
#define HYPERCALL_TYPE_INFO_LX_LX_LX        3   // 第1,2,3引数の整数をデバッグ出力
#define HYPERCALL_TYPE_INFO_LX_LX_LX_LX     4   // 第1,2,3,4引数負整数をデバッグ出力
#define HYPERCALL_TYPE_INFO_STR             10  // 第1引数の文字列をデバッグ出力

// 仮想マシン操作用
#define HYPERCALL_TYPE_CREATE_VM_FROM_ELF   100 // VM を作成する

#endif
