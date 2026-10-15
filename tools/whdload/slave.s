; slave.s - instalacja WHDLoad gry Speedway Party (vasm, skladnia Motoroli; make whdload).
;
; WHDLoad wylacza system, daje 2 MB pamieci Chip od adresu 0 (BaseMem) i 512 KB ExpMem,
; i wywoluje GameLoader. Ten wczytuje z katalogu data/ program gry w wersji WHDLoad
; (data/SpeedwayParty, budowany z -DWHDLOAD: pliki i pamiec przez WHDLoad, bez systemu),
; rozmieszcza go pod $1000 (resload_Relocate) i skacze do niego. Program wraca do systemu
; przez resload_Abort (F10 albo ESC na ekranie wyboru), zapisujac rekordy (halloffame.dat).

resload_Abort		equ	4
resload_LoadFile	equ	8
resload_Relocate	equ	80
TDREASON_FAILMSG	equ	43

WHDLF_Req68020		equ	$0010
WHDLF_ReqAGA		equ	$0020
WHDLF_NoKbd		equ	$0040	; gra czyta klawiature z CIA sama (bez przerwania PORTS)
WHDLF_ClearMem		equ	$1000

BASEMEM			equ	$200000
EXPMEM			equ	$80000
LOADADDR		equ	$1000

	section	slave,code

base:
	moveq	#-1,d0			; ws_Security - uruchomiony jak program: nic nie robi
	rts
	dc.b	"WHDLOADS"		; ws_ID
	dc.w	10			; ws_Version
	dc.w	WHDLF_Req68020|WHDLF_ReqAGA|WHDLF_NoKbd|WHDLF_ClearMem	; ws_Flags
	dc.l	BASEMEM			; ws_BaseMemSize
	dc.l	0			; ws_ExecInstall
	dc.w	gameloader-base		; ws_GameLoader
	dc.w	dir-base		; ws_CurrentDir
	dc.w	0			; ws_DontCache
	dc.b	0			; ws_keydebug
	dc.b	$59			; ws_keyexit: F10
expmem:	dc.l	EXPMEM			; ws_ExpMem (WHDLoad wpisuje tu adres)
	dc.w	name-base		; ws_name
	dc.w	copy-base		; ws_copy
	dc.w	info-base		; ws_info

name:	dc.b	"Speedway Party",0
copy:	dc.b	"2026 b4rtiga",0
info:	dc.b	"instalacja: b4rtiga (z pomoca Claude)",-1
	dc.b	"F10 - wyjscie, rekordy w halloffame.dat",0
dir:	dc.b	"data",0
exe:	dc.b	"SpeedwayParty",0
fail:	dc.b	"brak pliku data/SpeedwayParty",0
	dc.b	"$VER: SpeedwayParty.slave 0.85",0
	even

gameloader:				; a0 = resload
	move.l	a0,a2
	lea	exe(pc),a0
	lea	LOADADDR,a1
	jsr	resload_LoadFile(a2)	; d0 = dlugosc pliku (0 = blad)
	tst.l	d0
	beq.s	.fail
	lea	LOADADDR,a0
	sub.l	a1,a1			; bez tagow: wszystkie hunki na miejscu (Chip)
	jsr	resload_Relocate(a2)	; d0 = rozmiar programu po rozmieszczeniu (z BSS)
	lea	LOADADDR,a1
	add.l	d0,a1			; a1 = poczatek sterty
	move.l	expmem(pc),d1		; d1 = ExpMem
	move.l	#EXPMEM,d2
	move.l	a2,a0			; a0 = resload
	jmp	LOADADDR		; whdstart.s: _whd_start

.fail:	pea	fail(pc)
	pea	TDREASON_FAILMSG
	jmp	resload_Abort(a2)
