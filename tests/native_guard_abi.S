// Independent caller: all callee-saved GPRs, real stack canaries and dirty
// upper halves of C unsigned arguments. No runtime implementation here.
.text
.p2align 2
.globl _guard_abi_probe
_guard_abi_probe:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #112
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    mov x19, #19
    mov x20, #20
    mov x21, #21
    mov x22, #22
    mov x23, #23
    mov x24, #24
    mov x25, #25
    mov x26, #26
    mov x27, #27
    mov x28, #28
    stp x19, x28, [sp, #80]
    str x29, [sp, #96]
    mov x0, #0
    mov x1, #-1
    mov x2, #0x80000000
    movk x2, #0xbeef, lsl #48
    mov x3, #0
    bl _zan_rt_guard_fail3
    mov x0, #1
    cmp x19, #19
    b.ne Ldone
    cmp x20, #20
    b.ne Ldone
    cmp x21, #21
    b.ne Ldone
    cmp x22, #22
    b.ne Ldone
    cmp x23, #23
    b.ne Ldone
    cmp x24, #24
    b.ne Ldone
    cmp x25, #25
    b.ne Ldone
    cmp x26, #26
    b.ne Ldone
    cmp x27, #27
    b.ne Ldone
    cmp x28, #28
    b.ne Ldone
    ldp x9, x10, [sp, #80]
    cmp x9, #19
    b.ne Ldone
    cmp x10, #28
    b.ne Ldone
    ldr x9, [sp, #96]
    cmp x9, x29
    b.ne Ldone
    add x9, sp, #112
    cmp x9, x29
    b.ne Ldone
    // Same site with clean upper halves must be deduplicated.
    mov x0, #0
    mov w1, #-1
    mov w2, #0x80000000
    mov x3, #0
    bl _zan_rt_guard_fail3
    mov x0, #0
Ldone:
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    add sp, sp, #112
    ldp x29, x30, [sp], #16
    ret
