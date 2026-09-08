; CC0-1.0 deterministic two-ROM serial fixture for the Integral Link PoC.
; Build with LINK_ROLE=0 for the internal-clock A ROM and LINK_ROLE=1 for
; the external-clock B ROM. Both sides persist received bytes in cartridge
; RAM, which exercises both the local serial bridge and battery receipts.

IF !DEF(LINK_ROLE)
    DEF LINK_ROLE EQU 0
ENDC

DEF rSB EQU $FF01
DEF rSC EQU $FF02
DEF rLCDC EQU $FF40
DEF rBGP EQU $FF47
DEF rIE EQU $FFFF

SECTION "VBlank vector", ROM0[$40]
    reti

SECTION "Entry", ROM0[$100]
    nop
    jp Start

SECTION "Header title", ROM0[$134]
IF LINK_ROLE == 0
    db "ILP SERIAL A", 0
ELSE
    db "ILP SERIAL B", 0
ENDC
SECTION "Header CGB flag", ROM0[$143]
    db $00
SECTION "Header new licensee", ROM0[$144]
    db "00"
SECTION "Header SGB flag", ROM0[$146]
    db $00
SECTION "Header cartridge type", ROM0[$147]
    db $03 ; MBC1 + RAM + battery
SECTION "Header ROM size", ROM0[$148]
    db $00 ; 32 KiB
SECTION "Header RAM size", ROM0[$149]
    db $02 ; 8 KiB
SECTION "Header old licensee", ROM0[$14B]
    db $33

SECTION "Program", ROM0[$150]
Start:
    di
    ld sp, $DFFF
    ld a, $E4
    ldh [rBGP], a
    ld a, $91
    ldh [rLCDC], a

    ld a, $0A
    ld [$0000], a ; enable cartridge RAM
    ld hl, $A000
    ld a, 1
    ld [rIE], a
    ei

.loop:
IF LINK_ROLE == 0
    ld bc, 4
    call WaitFrames
    ld a, $3C
    ldh [rSB], a
    ld a, $81
    ldh [rSC], a
ELSE
    ld a, $A5
    ldh [rSB], a
    ld a, $80
    ldh [rSC], a
ENDC
.waitSerial:
    ldh a, [rSC]
    bit 7, a
    jr nz, .waitSerial
    ldh a, [rSB]
    ld [hli], a
    jr .loop

WaitFrames:
.next:
    halt
    nop
    dec bc
    ld a, b
    or c
    jr nz, .next
    ret
