; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -O2 < %s -mtriple=aarch64-linux-gnu                     | FileCheck %s

declare i32 @bcmp(ptr, ptr, i64)

define i1 @bcmp0(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp0:
; CHECK:       // %bb.0:
; CHECK-NEXT:    mov w0, #1 // =0x1
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 0)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp1(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp1:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x1]
; CHECK-NEXT:    cmp w8, w9
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 1)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp2(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp2:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrh w8, [x0]
; CHECK-NEXT:    ldrh w9, [x1]
; CHECK-NEXT:    cmp w8, w9
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 2)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

; or (and (xor a, b), C1), (and (xor c, d), C2)
define i1 @bcmp3(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp3:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrh w8, [x0]
; CHECK-NEXT:    ldrh w9, [x1]
; CHECK-NEXT:    ldrb w10, [x0, #2]
; CHECK-NEXT:    ldrb w11, [x1, #2]
; CHECK-NEXT:    cmp w8, w9
; CHECK-NEXT:    ccmp w10, w11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 3)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp4(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp4:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr w8, [x0]
; CHECK-NEXT:    ldr w9, [x1]
; CHECK-NEXT:    cmp w8, w9
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 4)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

; or (xor a, b), (and (xor c, d), C2)
define i1 @bcmp5(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp5:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr w8, [x0]
; CHECK-NEXT:    ldr w9, [x1]
; CHECK-NEXT:    ldrb w10, [x0, #4]
; CHECK-NEXT:    ldrb w11, [x1, #4]
; CHECK-NEXT:    cmp w8, w9
; CHECK-NEXT:    ccmp w10, w11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 5)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

; or (xor a, b), (and (xor c, d), C2)
define i1 @bcmp6(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp6:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr w8, [x0]
; CHECK-NEXT:    ldr w9, [x1]
; CHECK-NEXT:    ldrh w10, [x0, #4]
; CHECK-NEXT:    ldrh w11, [x1, #4]
; CHECK-NEXT:    cmp w8, w9
; CHECK-NEXT:    ccmp w10, w11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 6)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

; or (xor a, b), (xor c, d)
define i1 @bcmp7(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp7:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr w8, [x0]
; CHECK-NEXT:    ldr w9, [x1]
; CHECK-NEXT:    ldur w10, [x0, #3]
; CHECK-NEXT:    ldur w11, [x1, #3]
; CHECK-NEXT:    cmp w8, w9
; CHECK-NEXT:    ccmp w10, w11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 7)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp8(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 8)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

; or (xor a, b), (and (xor c, d), C2)
define i1 @bcmp9(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp9:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    ldrb w10, [x0, #8]
; CHECK-NEXT:    ldrb w11, [x1, #8]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 9)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp10(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp10:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    ldrh w10, [x0, #8]
; CHECK-NEXT:    ldrh w11, [x1, #8]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 10)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp11(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp11:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    ldur x10, [x0, #3]
; CHECK-NEXT:    ldur x11, [x1, #3]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 11)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp12(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp12:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    ldr w10, [x0, #8]
; CHECK-NEXT:    ldr w11, [x1, #8]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 12)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp13(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp13:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    ldur x10, [x0, #5]
; CHECK-NEXT:    ldur x11, [x1, #5]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 13)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp14(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp14:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    ldur x10, [x0, #6]
; CHECK-NEXT:    ldur x11, [x1, #6]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 14)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp15(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp15:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldr x8, [x0]
; CHECK-NEXT:    ldr x9, [x1]
; CHECK-NEXT:    ldur x10, [x0, #7]
; CHECK-NEXT:    ldur x11, [x1, #7]
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 15)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp16(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp16:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 16)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp20(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp20:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldr w12, [x0, #16]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    ldr w13, [x1, #16]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ccmp x12, x13, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 20)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp24(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp24:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldr x12, [x0, #16]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    ldr x13, [x1, #16]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ccmp x12, x13, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 24)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp28(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp28:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldr x12, [x0, #16]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    ldr x13, [x1, #16]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ldr w8, [x0, #24]
; CHECK-NEXT:    ldr w9, [x1, #24]
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ccmp x12, x13, #0, eq
; CHECK-NEXT:    ccmp x8, x9, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 28)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp33(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp33:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    ldp x12, x13, [x1, #16]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ldp x8, x9, [x0, #16]
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ldrb w10, [x0, #32]
; CHECK-NEXT:    ldrb w11, [x1, #32]
; CHECK-NEXT:    ccmp x8, x12, #0, eq
; CHECK-NEXT:    ccmp x9, x13, #0, eq
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 33)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp38(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp38:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    ldp x12, x13, [x1, #16]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ldp x8, x9, [x0, #16]
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ldur x10, [x0, #30]
; CHECK-NEXT:    ldur x11, [x1, #30]
; CHECK-NEXT:    ccmp x8, x12, #0, eq
; CHECK-NEXT:    ccmp x9, x13, #0, eq
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 38)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp45(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp45:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    ldp x12, x13, [x1, #16]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ldp x8, x9, [x0, #16]
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ldr x10, [x0, #32]
; CHECK-NEXT:    ldr x11, [x1, #32]
; CHECK-NEXT:    ccmp x8, x12, #0, eq
; CHECK-NEXT:    ldur x8, [x0, #37]
; CHECK-NEXT:    ldur x12, [x1, #37]
; CHECK-NEXT:    ccmp x9, x13, #0, eq
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ccmp x8, x12, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 45)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

; Although the large cmp chain may be not profitable on high end CPU, we
; believe it is better on most cpus, so perform the transform now.
; 8 xor + 7 or + 1 cmp only need 6 cycles on a 4 width ALU port machine
;   2 cycle for xor
;   3 cycle for or
;   1 cycle for cmp
define i1 @bcmp64(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp64:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x11, [x1]
; CHECK-NEXT:    ldp x9, x10, [x0]
; CHECK-NEXT:    ldp x12, x13, [x1, #16]
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    ldp x8, x9, [x0, #16]
; CHECK-NEXT:    ccmp x10, x11, #0, eq
; CHECK-NEXT:    ccmp x8, x12, #0, eq
; CHECK-NEXT:    ldp x8, x11, [x0, #32]
; CHECK-NEXT:    ldp x10, x12, [x1, #32]
; CHECK-NEXT:    ccmp x9, x13, #0, eq
; CHECK-NEXT:    ldp x9, x13, [x1, #48]
; CHECK-NEXT:    ccmp x8, x10, #0, eq
; CHECK-NEXT:    ldp x8, x10, [x0, #48]
; CHECK-NEXT:    ccmp x11, x12, #0, eq
; CHECK-NEXT:    ccmp x8, x9, #0, eq
; CHECK-NEXT:    ccmp x10, x13, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 64)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp89(ptr %a, ptr %b) {
; CHECK-LABEL: bcmp89:
; CHECK:       // %bb.0:
; CHECK-NEXT:    str x30, [sp, #-16]! // 8-byte Folded Spill
; CHECK-NEXT:    .cfi_def_cfa_offset 16
; CHECK-NEXT:    .cfi_offset w30, -16
; CHECK-NEXT:    mov w2, #89 // =0x59
; CHECK-NEXT:    bl bcmp
; CHECK-NEXT:    cmp w0, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ldr x30, [sp], #16 // 8-byte Folded Reload
; CHECK-NEXT:    ret
  %cr = call i32 @bcmp(ptr %a, ptr %b, i64 89)
  %r = icmp eq i32 %cr, 0
  ret i1 %r
}

define i1 @bcmp_zext(i32 %0, i32 %1, i8 %2, i8 %3) {
; CHECK-LABEL: bcmp_zext:
; CHECK:       // %bb.0:
; CHECK-NEXT:    and w8, w2, #0xff
; CHECK-NEXT:    and w9, w3, #0xff
; CHECK-NEXT:    cmp w1, w0
; CHECK-NEXT:    ccmp w9, w8, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %5 = xor i32 %1, %0
  %6 = xor i8 %3, %2
  %7 = zext i8 %6 to i32
  %8 = or i32 %5, %7
  %9 = icmp eq i32 %8, 0
  ret i1 %9
}

define i1 @bcmp_i8(i8 %a0, i8 %b0, i8 %a1, i8 %b1, i8 %a2, i8 %b2) {
; CHECK-LABEL: bcmp_i8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    and w8, w1, #0xff
; CHECK-NEXT:    and w9, w2, #0xff
; CHECK-NEXT:    and w10, w3, #0xff
; CHECK-NEXT:    cmp w8, w0, uxtb
; CHECK-NEXT:    and w8, w4, #0xff
; CHECK-NEXT:    and w11, w5, #0xff
; CHECK-NEXT:    ccmp w10, w9, #0, eq
; CHECK-NEXT:    ccmp w11, w8, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %xor0 = xor i8 %b0, %a0
  %xor1 = xor i8 %b1, %a1
  %xor2 = xor i8 %b2, %a2
  %or0 = or i8 %xor0, %xor1
  %or1 = or i8 %or0, %xor2
  %r = icmp eq i8 %or1, 0
  ret i1 %r
}

define i1 @bcmp_i16(i16 %a0, i16 %b0, i16 %a1, i16 %b1, i16 %a2, i16 %b2) {
; CHECK-LABEL: bcmp_i16:
; CHECK:       // %bb.0:
; CHECK-NEXT:    and w8, w1, #0xffff
; CHECK-NEXT:    and w9, w2, #0xffff
; CHECK-NEXT:    and w10, w3, #0xffff
; CHECK-NEXT:    cmp w8, w0, uxth
; CHECK-NEXT:    and w8, w4, #0xffff
; CHECK-NEXT:    and w11, w5, #0xffff
; CHECK-NEXT:    ccmp w10, w9, #0, eq
; CHECK-NEXT:    ccmp w11, w8, #0, eq
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %xor0 = xor i16 %b0, %a0
  %xor1 = xor i16 %b1, %a1
  %xor2 = xor i16 %b2, %a2
  %or0 = or i16 %xor0, %xor1
  %or1 = or i16 %or0, %xor2
  %r = icmp eq i16 %or1, 0
  ret i1 %r
}

define i1 @bcmp_i128(i128 %a0, i128 %b0, i128 %a1, i128 %b1, i128 %a2, i128 %b2) {
; CHECK-LABEL: bcmp_i128:
; CHECK:       // %bb.0:
; CHECK-NEXT:    cmp x2, x0
; CHECK-NEXT:    ldp x10, x8, [sp, #8]
; CHECK-NEXT:    ccmp x3, x1, #0, eq
; CHECK-NEXT:    ldr x9, [sp]
; CHECK-NEXT:    ldr x11, [sp, #24]
; CHECK-NEXT:    ccmp x6, x4, #0, eq
; CHECK-NEXT:    ccmp x7, x5, #0, eq
; CHECK-NEXT:    cset w12, ne
; CHECK-NEXT:    cmp x8, x9
; CHECK-NEXT:    ccmp x11, x10, #0, eq
; CHECK-NEXT:    csinc w0, w12, wzr, eq
; CHECK-NEXT:    ret
  %xor0 = xor i128 %b0, %a0
  %xor1 = xor i128 %b1, %a1
  %xor2 = xor i128 %b2, %a2
  %or0 = or i128 %xor0, %xor1
  %or1 = or i128 %or0, %xor2
  %r = icmp ne i128 %or1, 0
  ret i1 %r
}

define i1 @bcmp_i42(i42 %a0, i42 %b0, i42 %a1, i42 %b1, i42 %a2, i42 %b2) {
; CHECK-LABEL: bcmp_i42:
; CHECK:       // %bb.0:
; CHECK-NEXT:    and x8, x0, #0x3ffffffffff
; CHECK-NEXT:    and x9, x1, #0x3ffffffffff
; CHECK-NEXT:    and x10, x2, #0x3ffffffffff
; CHECK-NEXT:    and x11, x3, #0x3ffffffffff
; CHECK-NEXT:    cmp x9, x8
; CHECK-NEXT:    and x8, x4, #0x3ffffffffff
; CHECK-NEXT:    and x9, x5, #0x3ffffffffff
; CHECK-NEXT:    ccmp x11, x10, #0, eq
; CHECK-NEXT:    ccmp x9, x8, #0, eq
; CHECK-NEXT:    cset w0, ne
; CHECK-NEXT:    ret
  %xor0 = xor i42 %b0, %a0
  %xor1 = xor i42 %b1, %a1
  %xor2 = xor i42 %b2, %a2
  %or0 = or i42 %xor0, %xor1
  %or1 = or i42 %or0, %xor2
  %r = icmp ne i42 %or1, 0
  ret i1 %r
}
