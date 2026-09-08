_Head:
	/* Set _Entry address */
	adr	x1, _Payload

	/* Jump to the real code */
	b	_Start

	/* Fake Linux kernel header */

_StackBase0: //has to be 0 on some devices
	/* Text Offset */
	.quad 0

_StackSize0:
	/* Image Size */
	.quad 0

	/* Flags */
	.quad 0

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* ARM64 Magic */
	.ascii "ARM\x64"

	/* Reserved */
	.long 0

	.ascii "EDK2-MSM"

_StackBase:
	/* Text Offset */
	.quad UEFI_BASE

_StackSize:
	/* Image Size */
	.quad UEFI_SIZE

_Start:
#ifdef FUJISAN_BRINGUP
	/* Stackless, single-entry trace. Never probe guessed framebuffer RAM.
	 * Preserve x0 (the bootloader FDT) and x1 (payload) throughout. */
	mov	x7, #0xbfc00000
	adr	x8, _FujisanTrace
	mov	x9, #(_FujisanTraceEnd - _FujisanTrace)
	str	wzr, [x7]             /* invalidate old record before updating */
	str	w9, [x7, #4]         /* persistent_ram next-write position */
	str	w9, [x7, #8]
	add	x10, x7, #12
_FujisanTraceCopy:
	ldrb	w11, [x8], #1
	strb	w11, [x10], #1
	subs	x9, x9, #1
	b.ne	_FujisanTraceCopy
	mov	w11, #0x4244
	movk	w11, #0x4347, lsl #16
	str	w11, [x7]
	/* Record fits in the first two minimum-sized AArch64 cache lines. */
	dc	cvac, x7
	add	x7, x7, #64
	dc	cvac, x7
	dsb	sy
#endif
	mov	x4, x1
	ldr	x5, _StackBase
	cmp	x4, x5
	beq	_Entry
	ldr	x6, _StackSize
_CopyLoop:
	ldp	x2, x3, [x4], #16
	stp	x2, x3, [x5], #16
	subs	x6, x6, #16
	b.ne	_CopyLoop
	ldr	x5, _StackBase
_Entry:
	/* Boot protocol supplies caches/MMU off. Order relocation stores before
	 * executing the FV; PrePi performs its own cache/MMU initialization. */
	dsb	sy
	isb
	br	x5
#ifdef FUJISAN_BRINGUP
_FujisanTrace:
	.ascii "====0.0\nFJ-UEFI-20260908-E BOOTSHIM\n"
_FujisanTraceEnd:
#endif

_Dead:
	/* We should never get here */
	b	_Dead

.text
.align 4

_Payload:
	/* Your code will get ran right after this binary */
