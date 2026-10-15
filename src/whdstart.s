; whdstart.s - poczatek programu gry w wersji WHDLoad (make whdload), vasm (skladnia Motoroli).
;
; Slave (tools/whdload/slave.s) wczytuje program pod $1000, rozmieszcza go w pamieci
; (resload_Relocate) i skacze tutaj (pierwszy kod programu) z:
;   a0 = resload (funkcje WHDLoad), a1 = koniec programu (poczatek sterty),
;   d1 = adres ExpMem (0 = brak), d2 = rozmiar ExpMem.
; Wywolania resload zachowuja d2-d7/a2-a6 - tak jak konwencja GCC.

resload_Abort		equ	4
resload_SaveFile	equ	12
resload_GetFileSize	equ	36
resload_LoadFileOffset	equ	76
TDREASON_OK		equ	-1

	section	.text,code		; ta sama nazwa co kod z GCC: jeden hunk, ten plik pierwszy

	xdef	_whd_start
	xref	_main
	xref	_whd_resload
	xref	_whd_heap
	xref	_whd_exp
	xref	_whd_expsize

_whd_start:
	move.l	a0,_whd_resload
	move.l	a1,_whd_heap
	move.l	d1,_whd_exp
	move.l	d2,_whd_expsize
	move.w	#$2000,sr		; nadzorca, przerwania procesora wlaczone (INTENA steruje gra)
	jsr	_main
	; dalej: wyjscie do systemu

	xdef	_whd_exit
_whd_exit:
	pea	TDREASON_OK
	move.l	_whd_resload,a0
	jmp	resload_Abort(a0)

; ULONG whd_getsize(const char *name) - 0 = brak pliku
	xdef	_whd_getsize
_whd_getsize:
	move.l	a2,-(sp)
	move.l	8(sp),a0
	move.l	_whd_resload,a2
	jsr	resload_GetFileSize(a2)
	move.l	(sp)+,a2
	rts

; BOOL whd_loadoffset(ULONG size, ULONG offset, const char *name, APTR dst)
	xdef	_whd_loadoffset
_whd_loadoffset:
	move.l	a2,-(sp)
	movem.l	8(sp),d0/d1/a0/a1
	move.l	_whd_resload,a2
	jsr	resload_LoadFileOffset(a2)
	move.l	(sp)+,a2
	rts

; BOOL whd_savefile(ULONG size, const char *name, const void *src)
	xdef	_whd_savefile
_whd_savefile:
	move.l	a2,-(sp)
	move.l	8(sp),d0
	move.l	12(sp),a0
	move.l	16(sp),a1
	move.l	_whd_resload,a2
	jsr	resload_SaveFile(a2)
	move.l	(sp)+,a2
	rts
