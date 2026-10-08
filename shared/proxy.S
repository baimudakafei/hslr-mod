/* ======================================================================
 * 【流程 1】注入与代理劫持 —— Java 类比：静态代理（Static Proxy）+
 * classpath 抢占。Windows 加载器按搜索顺序在游戏目录里发现了我们这个
 * d3d11.dll，就会优先于 System32 的真货加载它（类似同名 jar 排在
 * classpath 前面）。游戏每次调用 D3D11CreateDevice 等导出函数，实际进入
 * 的是下面 51 个跳板之一。
 *
 * 每个跳板只做三件事：
 *   1. 把 x64 调用约定的寄存器实参（rcx/rdx/r8/r9、xmm0-3、rax）存进栈上
 *      影子空间——相当于把请求参数打包；
 *   2. call proxy_resolve(自身序号) 拿真身函数地址（懒加载+缓存，
 *      见 waitkey.c 的【流程 1b】）；
 *   3. 恢复全部实参寄存器，jmp *%r11 尾跳过去——对调用方完全透明，
 *      等价于 InvocationHandler 里的 method.invoke(target, args)。
 *
 * 本文件不含任何业务逻辑：只是"转发层"，功能全部在 waitkey.c / menu.h。
 * .seh_* 注解让 Windows 栈展开时能正确看懂我们的栈帧。
 * ======================================================================
 */
.text
.globl proxy_0
.seh_proc proxy_0
proxy_0:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $0, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_1
.seh_proc proxy_1
proxy_1:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $1, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_2
.seh_proc proxy_2
proxy_2:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $2, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_3
.seh_proc proxy_3
proxy_3:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $3, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_4
.seh_proc proxy_4
proxy_4:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $4, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_5
.seh_proc proxy_5
proxy_5:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $5, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_6
.seh_proc proxy_6
proxy_6:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $6, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_7
.seh_proc proxy_7
proxy_7:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $7, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_8
.seh_proc proxy_8
proxy_8:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $8, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_9
.seh_proc proxy_9
proxy_9:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $9, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_10
.seh_proc proxy_10
proxy_10:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $10, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_11
.seh_proc proxy_11
proxy_11:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $11, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_12
.seh_proc proxy_12
proxy_12:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $12, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_13
.seh_proc proxy_13
proxy_13:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $13, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_14
.seh_proc proxy_14
proxy_14:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $14, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_15
.seh_proc proxy_15
proxy_15:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $15, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_16
.seh_proc proxy_16
proxy_16:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $16, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_17
.seh_proc proxy_17
proxy_17:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $17, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_18
.seh_proc proxy_18
proxy_18:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $18, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_19
.seh_proc proxy_19
proxy_19:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $19, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_20
.seh_proc proxy_20
proxy_20:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $20, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_21
.seh_proc proxy_21
proxy_21:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $21, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_22
.seh_proc proxy_22
proxy_22:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $22, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_23
.seh_proc proxy_23
proxy_23:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $23, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_24
.seh_proc proxy_24
proxy_24:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $24, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_25
.seh_proc proxy_25
proxy_25:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $25, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_26
.seh_proc proxy_26
proxy_26:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $26, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_27
.seh_proc proxy_27
proxy_27:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $27, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_28
.seh_proc proxy_28
proxy_28:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $28, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_29
.seh_proc proxy_29
proxy_29:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $29, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_30
.seh_proc proxy_30
proxy_30:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $30, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_31
.seh_proc proxy_31
proxy_31:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $31, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_32
.seh_proc proxy_32
proxy_32:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $32, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_33
.seh_proc proxy_33
proxy_33:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $33, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_34
.seh_proc proxy_34
proxy_34:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $34, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_35
.seh_proc proxy_35
proxy_35:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $35, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_36
.seh_proc proxy_36
proxy_36:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $36, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_37
.seh_proc proxy_37
proxy_37:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $37, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_38
.seh_proc proxy_38
proxy_38:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $38, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_39
.seh_proc proxy_39
proxy_39:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $39, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_40
.seh_proc proxy_40
proxy_40:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $40, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_41
.seh_proc proxy_41
proxy_41:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $41, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_42
.seh_proc proxy_42
proxy_42:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $42, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_43
.seh_proc proxy_43
proxy_43:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $43, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_44
.seh_proc proxy_44
proxy_44:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $44, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_45
.seh_proc proxy_45
proxy_45:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $45, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_46
.seh_proc proxy_46
proxy_46:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $46, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_47
.seh_proc proxy_47
proxy_47:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $47, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_48
.seh_proc proxy_48
proxy_48:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $48, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_49
.seh_proc proxy_49
proxy_49:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $49, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
.globl proxy_50
.seh_proc proxy_50
proxy_50:
    subq $0xA8, %rsp
    .seh_stackalloc 0xA8
    .seh_endprologue
    movq %rcx, 0x20(%rsp)
    movq %rdx, 0x28(%rsp)
    movq %r8, 0x30(%rsp)
    movq %r9, 0x38(%rsp)
    movups %xmm0, 0x40(%rsp)
    movups %xmm1, 0x50(%rsp)
    movups %xmm2, 0x60(%rsp)
    movups %xmm3, 0x70(%rsp)
    movq %rax, 0x80(%rsp)
    movl $50, %ecx
    call proxy_resolve
    movq %rax, %r11
    movq 0x20(%rsp), %rcx
    movq 0x28(%rsp), %rdx
    movq 0x30(%rsp), %r8
    movq 0x38(%rsp), %r9
    movups 0x40(%rsp), %xmm0
    movups 0x50(%rsp), %xmm1
    movups 0x60(%rsp), %xmm2
    movups 0x70(%rsp), %xmm3
    movq 0x80(%rsp), %rax
    addq $0xA8, %rsp
    jmp *%r11
.seh_endproc
