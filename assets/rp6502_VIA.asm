VIA     .equ $FFD0
TXPORT  .equ $FFD1 ;ORA
DDRPORT .equ $FFD3 ;DDRA
T1CL    .equ $FFD4
T1CH    .equ $FFD5
ACR     .equ $FFDB
IFR     .equ $FFDD
IER     .equ $FFDE

; W65C22 REGISTER DEFINITIONS
; (offsets from VIA base)
; $0 Out Register B / In Register B
; $1 Out Register A / In Register A
; $2 Data Direction Register B
; $3 Data Direction Register A
; $4 Timer 1 Counter Low
; $5 Timer 1 Counter High
; $6 Timer 1 Latch Low
; $7 Timer 1 Latch High
; $8 Timer 2 Counter Low
; $9 Timer 2 Counter High
; $A Shift Register
; $B Auxiliary Control Register
; $C Peripheral Control Register
; $D Interrupt Flag Register
; $E Interrupt Enable Register
; $F ORA no handshake 
;    (alias for ORA)

VIA_ORB   .equ $FFD0
VIA_ORA   .equ $FFD1
VIA_DDRB  .equ $FFD2
VIA_DDRA  .equ $FFD3
VIA_T1CL  .equ $FFD4
VIA_T1CH  .equ $FFD5
VIA_T1LL  .equ $FFD6
VIA_T1LH  .equ $FFD7
VIA_T2CL  .equ $FFD8
VIA_T2CH  .equ $FFD9
VIA_SR    .equ $FFDA
VIA_ACR   .equ $FFDB
VIA_PCR   .equ $FFDC
VIA_IFR   .equ $FFDD
VIA_IER   .equ $FFDE
VIA_ORANH .equ $FFDF

TXBYTE  .equ $CB ; byte to send
TXSTATE .equ $CC ; transmission bit 
                 ; counter :
                 ;   0 = start
                 ; 1-8 = data
                 ;   9 = stop
                 ;  10 = done