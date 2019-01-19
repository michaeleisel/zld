	.section	__TEXT,__text,regular,pure_instructions
	.ios_version_min 9, 0	sdk_version 13, 2
	.globl	_main                   ; -- Begin function main
	.p2align	2
_main:                                  ; @main
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #16             ; =16
	.cfi_def_cfa_offset 16
	adrp	x8, _staticbigarray6@PAGE
	add	x8, x8, _staticbigarray6@PAGEOFF
	adrp	x9, _staticbigarray5@PAGE
	add	x9, x9, _staticbigarray5@PAGEOFF
	adrp	x10, _staticbigarray4@PAGE
	add	x10, x10, _staticbigarray4@PAGEOFF
	adrp	x11, _staticbigarray3@PAGE
	add	x11, x11, _staticbigarray3@PAGEOFF
	adrp	x12, _staticbigarray2@PAGE
	add	x12, x12, _staticbigarray2@PAGEOFF
	adrp	x13, _staticbigarray1@PAGE
	add	x13, x13, _staticbigarray1@PAGEOFF
	adrp	x14, _bigarray99@GOTPAGE
	ldr	x14, [x14, _bigarray99@GOTPAGEOFF]
	adrp	x15, _bigarray11@GOTPAGE
	ldr	x15, [x15, _bigarray11@GOTPAGEOFF]
	adrp	x16, _bigarray10@GOTPAGE
	ldr	x16, [x16, _bigarray10@GOTPAGEOFF]
	adrp	x17, _bigarray9@GOTPAGE
	ldr	x17, [x17, _bigarray9@GOTPAGEOFF]
	adrp	x0, _bigarray8@GOTPAGE
	ldr	x0, [x0, _bigarray8@GOTPAGEOFF]
	adrp	x1, _bigarray7@GOTPAGE
	ldr	x1, [x1, _bigarray7@GOTPAGEOFF]
	adrp	x2, _bigarray5@GOTPAGE
	ldr	x2, [x2, _bigarray5@GOTPAGEOFF]
	str	wzr, [sp, #12]
	orr	w3, wzr, #0x4
	str	w3, [x2, #40]
	str	w3, [x1, #40]
	str	w3, [x0, #40]
	str	w3, [x17, #40]
	str	w3, [x16, #40]
	str	w3, [x15, #40]
	str	w3, [x14, #40]
	str	w3, [x13, #40]
	str	w3, [x12, #40]
	str	w3, [x11, #40]
	str	w3, [x10, #40]
	str	w3, [x9, #40]
	str	w3, [x8, #40]
	mov	w3, #0
	mov	x0, x3
	add	sp, sp, #16             ; =16
	ret
	.cfi_endproc
                                        ; -- End function
	.comm	_bigarray5,10240000,2   ; @bigarray5
	.comm	_bigarray7,67108868,2   ; @bigarray7
	.comm	_bigarray8,134217732,2  ; @bigarray8
	.comm	_bigarray9,268435460,2  ; @bigarray9
	.comm	_bigarray10,536870916,2 ; @bigarray10
	.comm	_bigarray11,1073741828,2 ; @bigarray11
	.comm	_bigarray99,2147483644,2 ; @bigarray99
.zerofill __DATA,__bss,_staticbigarray1,1024,2 ; @staticbigarray1
.zerofill __DATA,__bss,_staticbigarray2,10240,2 ; @staticbigarray2
.zerofill __DATA,__bss,_staticbigarray3,102400,2 ; @staticbigarray3
.zerofill __DATA,__bss,_staticbigarray4,1024000,2 ; @staticbigarray4
.zerofill __DATA,__bss,_staticbigarray5,10240000,2 ; @staticbigarray5
.zerofill __DATA,__bss,_staticbigarray6,102400000,2 ; @staticbigarray6
	.comm	_bigarray1,1024,2       ; @bigarray1
	.comm	_bigarray2,10240,2      ; @bigarray2
	.comm	_bigarray3,102400,2     ; @bigarray3
	.comm	_bigarray4,1024000,2    ; @bigarray4

.subsections_via_symbols
