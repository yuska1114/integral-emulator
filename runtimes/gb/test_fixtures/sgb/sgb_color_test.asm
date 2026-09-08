DEF rP1 EQU $FF00
DEF rLCDC EQU $FF40
DEF rBGP EQU $FF47
DEF rIE EQU $FFFF

SECTION "VBlank vector", ROM0[$40]
    reti

SECTION "Entry", ROM0[$100]
    nop
    jp Start

SECTION "Header title", ROM0[$134]
    db "SGB COLOR TEST", 0
SECTION "Header CGB flag", ROM0[$143]
    db $00
SECTION "Header new licensee", ROM0[$144]
    db "00"
SECTION "Header SGB flag", ROM0[$146]
    db $03
SECTION "Header old licensee", ROM0[$14B]
    db $33

SECTION "Program", ROM0[$150]
Start:
    di
    ld sp, $DFFF
    xor a
    ldh [rLCDC], a

    ld hl, $8000
    ld b, 16
.tile0:
    ld [hli], a
    dec b
    jr nz, .tile0
    ld b, 8
.tile1:
    ld a, $FF
    ld [hli], a
    xor a
    ld [hli], a
    dec b
    jr nz, .tile1

    ld hl, $9800
    ld bc, 32 * 18
    ld a, 1
.map:
    ld [hli], a
    dec bc
    ld a, b
    or c
    ld a, 1
    jr nz, .map

    ld a, $E4
    ldh [rBGP], a
    ld a, $91
    ldh [rLCDC], a

    ld hl, PaletteA
    call SendPacket
    ld hl, Attributes
    call SendPacket
    ld a, 1
    ld [rIE], a
    ei

.loop:
    ld bc, 180
    call WaitFrames
    ld hl, PaletteB
    call SendPacket
    ld bc, 180
    call WaitFrames
    ld hl, PaletteA
    call SendPacket
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

SendPacket:
    xor a
    ldh [rP1], a
    call PulseDelay
    ld a, $30
    ldh [rP1], a
    call PulseDelay
    ld b, 16
.byte:
    ld a, [hli]
    ld c, 8
.bit:
    rra
    push af
    ld a, $20
    jr nc, .write
    ld a, $10
.write:
    ldh [rP1], a
    call PulseDelay
    ld a, $30
    ldh [rP1], a
    call PulseDelay
    pop af
    dec c
    jr nz, .bit
    dec b
    jr nz, .byte
    ld a, $20
    ldh [rP1], a
    call PulseDelay
    ld a, $30
    ldh [rP1], a
    call PulseDelay
    ret

PulseDelay:
    nop
    nop
    nop
    nop
    ret

PaletteA:
    db $01
    dw $7FFF, $001F, $03E0, $0000
    dw $7C00, $03FF, $0000
    db 0

PaletteB:
    db $01
    dw $7FFF, $03E0, $7C00, $0000
    dw $001F, $7C1F, $0000
    db 0

Attributes:
    db $21, 2
    db 1, 0, 0, 0, 9, 17
    db 1, 1, 10, 0, 19, 17
    db 0, 0
