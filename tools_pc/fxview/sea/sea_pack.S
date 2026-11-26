/* The sea tab's tool pack (ge007_sea.bin, built by sea_pack.cmake; the game's pack format, gfx_dk_layout.h), embedded in the
 * tool. The assembler finds the file through -Wa,-I<build dir>. */
#if defined(_WIN32)
    .section .rdata, "dr"
#else
    .section .rodata.ge007_sea, "a"
#endif
    .balign 256
    .global ge007_sea_pack
ge007_sea_pack:
    .incbin "ge007_sea.bin"
    .global ge007_sea_pack_end
ge007_sea_pack_end:
    .byte 0
#if !defined(_WIN32)
    .section .note.GNU-stack, "", %progbits
#endif
