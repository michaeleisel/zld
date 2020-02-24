	.section	__TEXT,__text,regular,pure_instructions
	.build_version macos, 10, 14	sdk_version 10, 14
	.globl	_main                   ## -- Begin function main
	.p2align	4, 0x90
_main:                                  ## @main
	.cfi_startproc
## %bb.0:
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	xorl	%eax, %eax
	movq	_bigarray99@GOTPCREL(%rip), %rcx
	movq	_bigarray11@GOTPCREL(%rip), %rdx
	movq	_bigarray10@GOTPCREL(%rip), %rsi
	movq	_bigarray9@GOTPCREL(%rip), %rdi
	movq	_bigarray8@GOTPCREL(%rip), %r8
	movq	_bigarray7@GOTPCREL(%rip), %r9
	movq	_bigarray5@GOTPCREL(%rip), %r10
	movl	$0, -4(%rbp)
	movl	$4, 40(%r10)
	movl	$4, 40(%r9)
	movl	$4, 40(%r8)
	movl	$4, 40(%rdi)
	movl	$4, 40(%rsi)
	movl	$4, 40(%rdx)
	movl	$4, 40(%rcx)
	movl	$4, _staticbigarray1+40(%rip)
	movl	$4, _staticbigarray2+40(%rip)
	movl	$4, _staticbigarray3+40(%rip)
	movl	$4, _staticbigarray4+40(%rip)
	movl	$4, _staticbigarray5+40(%rip)
	movl	$4, _staticbigarray6+40(%rip)
	popq	%rbp
	retq
	.cfi_endproc
                                        ## -- End function
	.comm	_bigarray5,10240000,4   ## @bigarray5
	.comm	_bigarray7,67108868,4   ## @bigarray7
	.comm	_bigarray8,134217732,4  ## @bigarray8
	.comm	_bigarray9,268435460,4  ## @bigarray9
	.comm	_bigarray10,536870916,4 ## @bigarray10
	.comm	_bigarray11,1073741828,4 ## @bigarray11
	.comm	_bigarray99,8589934588,4 ## @bigarray99
.zerofill __DATA,__bss,_staticbigarray1,1024,4 ## @staticbigarray1
.zerofill __DATA,__bss,_staticbigarray2,10240,4 ## @staticbigarray2
.zerofill __DATA,__bss,_staticbigarray3,102400,4 ## @staticbigarray3
.zerofill __DATA,__bss,_staticbigarray4,1024000,4 ## @staticbigarray4
.zerofill __DATA,__bss,_staticbigarray5,10240000,4 ## @staticbigarray5
.zerofill __DATA,__bss,_staticbigarray6,102400000,4 ## @staticbigarray6
	.comm	_bigarray1,1024,4       ## @bigarray1
	.comm	_bigarray2,10240,4      ## @bigarray2
	.comm	_bigarray3,102400,4     ## @bigarray3
	.comm	_bigarray4,1024000,4    ## @bigarray4

.subsections_via_symbols
