; CC0-1.0 deterministic two-ROM CGB infrared fixture for the Integral Link PoC.
; Build with LINK_ROLE=0 for canonical A and LINK_ROLE=1 for canonical B.
; Each side emits an alternating IR pulse after a role-specific VBlank delay,
; samples the peer sensor, and persists the samples in cartridge RAM.

IF !DEF(LINK_ROLE)
    DEF LINK_ROLE EQU 0
ENDC

DEF rRP EQU $FF56
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
    db "ILP IR A", 0
ELSE
    db "ILP IR B", 0
ENDC
SECTION "Header CGB flag", ROM0[$143]
    db $C0
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
    ld bc, 2
ELSE
    ld bc, 4
ENDC
    call WaitFrames

    ; Enable sensing and switch the local LED on. The peer receives the edge
    ; through the Twin engine callback while this core continues locally.
    ld a, $C1
    ldh [rRP], a
    ld bc, 1
    call WaitFrames
    ldh a, [rRP]
    ld [hli], a

    ; Switch the LED off, let the sensor settle, and preserve that sample too.
    ld a, $C0
    ldh [rRP], a
    ld bc, 2
    call WaitFrames
    ldh a, [rRP]
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
