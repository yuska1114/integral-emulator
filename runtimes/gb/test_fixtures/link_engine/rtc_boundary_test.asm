; CC0-1.0 deterministic MBC3 RTC date-boundary fixture for Integral Link.
; Both roles initialize 23:59:55 on day 0, then sample the running RTC into
; battery-backed RAM. Ten emulated seconds must cross into day 1.

DEF rLCDC EQU $FF40
DEF rBGP EQU $FF47
DEF rIE EQU $FFFF

SECTION "VBlank vector", ROM0[$40]
    reti

SECTION "Entry", ROM0[$100]
    nop
    jp Start

SECTION "Header title", ROM0[$134]
    db "ILP RTC EDGE", 0
SECTION "Header CGB flag", ROM0[$143]
    db $80
SECTION "Header new licensee", ROM0[$144]
    db "00"
SECTION "Header SGB flag", ROM0[$146]
    db $00
SECTION "Header cartridge type", ROM0[$147]
    db $10 ; MBC3 + timer + RAM + battery
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
    ld [$0000], a ; enable RAM and RTC

    ; Set the canonical boundary snapshot: day 0, 23:59:55, running.
    ld a, $08
    ld [$4000], a
    ld a, 55
    ld [$A000], a
    ld a, $09
    ld [$4000], a
    ld a, 59
    ld [$A000], a
    ld a, $0A
    ld [$4000], a
    ld a, 23
    ld [$A000], a
    ld a, $0B
    ld [$4000], a
    xor a
    ld [$A000], a
    ld a, $0C
    ld [$4000], a
    xor a
    ld [$A000], a

    ld hl, $A000
    ld a, 1
    ld [rIE], a
    ei

.loop:
    ld bc, 30
    call WaitFrames
    call SampleRTC
    jr .loop

SampleRTC:
    ; Latch the five RTC registers into a stable read snapshot.
    xor a
    ld [$6000], a
    inc a
    ld [$6000], a

    ld a, $08
    ld [$4000], a
    ld a, [$A000]
    ld [$C000], a
    ld a, $09
    ld [$4000], a
    ld a, [$A000]
    ld [$C001], a
    ld a, $0A
    ld [$4000], a
    ld a, [$A000]
    ld [$C002], a
    ld a, $0B
    ld [$4000], a
    ld a, [$A000]
    ld [$C003], a
    ld a, $0C
    ld [$4000], a
    ld a, [$A000]
    ld [$C004], a

    ; Return to RAM bank 0 and append the sample to the battery candidate.
    xor a
    ld [$4000], a
    ld de, $C000
    ld b, 5
.copy:
    ld a, [de]
    ld [hli], a
    inc de
    dec b
    jr nz, .copy
    ret

WaitFrames:
.next:
    halt
    nop
    dec bc
    ld a, b
    or c
    jr nz, .next
    ret
