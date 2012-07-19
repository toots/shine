;
; multiply
; --------
;
; 32 bit fractional multiplication.
; 06/03/01 v0.00 P.Everett
;
; No checks are made for overflow.
; To assemble, using the 'as' assembler:
;  as s.multiply -gcc -target SA1
;
; All have the following c declaration:
;  long mul(long a1, long a2);
;
        area    |C$$code|, CODE, READONLY
        align   4
;
; Signed fractional multiply by partial products.
; For use on non StrongARM machines. 
;

; mul:
; ----
; Fractional multiply.
        export  |mul|
|mul|
        stmfd   sp!,{v1,v2,v3,lr}
; strip signs
        cmps    a1, #0              ; check sign of input
        rsbmi   a1, a1, #0          ; create abs of input
        mvnmi   a3, #0              ; save sign,
        movpl   a3, #0              ; of input
        cmps    a2, #0              ; check sign of other input
        rsbmi   a2, a2, #0          ; create abs of other input
        mvnmi   a4, #0              ; save sign,
        movpl   a4, #0              ; of other input
        eor     v3, a3, a4          ; combine signs and save
; unsigned long multiply
        mov     a3, a1, lsr #16     ; Split input ,
        eor     v1, a1, a3, lsl #16 ; into two halves.
        mov     a4, a2, lsr #16     ; Split other input,
        eor     v2, a2, a4, lsl #16 ; into two halves.
        mul     a1, a3, a4          ; Produce,
        mul     a2, v1, v2          ; the,
        mul     a3, v2, a3          ; partial,
        mul     a4, v1, a4          ; products.
        adds    a2, a2, a4, lsl #16 ; Combine,
        adc     a1, a1, a4, lsr #16 ; the,
        adds    a2, a2, a3, lsl #16 ; partial,
        adc     a1, a1, a3, lsr #16 ; products.
; make result signed
        cmps    v3, #0              ; check result sign
        beq     |exit_a|            ; don't negate result if positive
        rsbs    a2, a2, #0          ; else negate,
        rsc     a1, a1, #0          ; the result.
|exit_a|
        ldmfd   sp!,{v1,v2,v3,pc}^  ; return, a1 = msw of result.

; muls:
; -----
; Fractional multiply with single bit left shift.
        export  |muls|
|muls|
        stmfd   sp!,{v1,v2,v3,lr}
; strip signs
        cmps    a1, #0              ; check sign of input
        rsbmi   a1, a1, #0          ; create abs of input
        mvnmi   a3, #0              ; save sign,
        movpl   a3, #0              ; of input
        cmps    a2, #0              ; check sign of other input
        rsbmi   a2, a2, #0          ; create abs of other input
        mvnmi   a4, #0              ; save sign,
        movpl   a4, #0              ; of other input
        eor     v3, a3, a4          ; combine signs and save
; unsigned long multiply
        mov     a3, a1, lsr #16     ; Split input ,
        eor     v1, a1, a3, lsl #16 ; into two halves.
        mov     a4, a2, lsr #16     ; Split other input,
        eor     v2, a2, a4, lsl #16 ; into two halves.
        mul     a1, a3, a4          ; Produce,
        mul     a2, v1, v2          ; the,
        mul     a3, v2, a3          ; partial,
        mul     a4, v1, a4          ; products.
        adds    a2, a2, a4, lsl #16 ; Combine,
        adc     a1, a1, a4, lsr #16 ; the,
        adds    a2, a2, a3, lsl #16 ; partial,
        adc     a1, a1, a3, lsr #16 ; products.
; make result signed
        cmps    v3, #0              ; check result sign
        beq     |exit_b|            ; don't negate result if positive
        rsbs    a2, a2, #0          ; else negate,
        rsc     a1, a1, #0          ; the result.
|exit_b|
; shift and round
        movs    a2, a2, lsl #1      ; shift lsw 1 bit left,
        adc     a1, a1, a1          ; and msw, for fractional result.
        ldmfd   sp!,{v1,v2,v3,pc}^  ; return, a1 = msw of result.

; mulr:
; -----
; Fractional multiply with rounding.
        export  |mulr|
|mulr|
        stmfd   sp!,{v1,v2,v3,lr}
; strip signs
        cmps    a1, #0              ; check sign of input
        rsbmi   a1, a1, #0          ; create abs of input
        mvnmi   a3, #0              ; save sign,
        movpl   a3, #0              ; of input
        cmps    a2, #0              ; check sign of other input
        rsbmi   a2, a2, #0          ; create abs of other input
        mvnmi   a4, #0              ; save sign,
        movpl   a4, #0              ; of other input
        eor     v3, a3, a4          ; combine signs and save
; unsigned long multiply
        mov     a3, a1, lsr #16     ; Split input ,
        eor     v1, a1, a3, lsl #16 ; into two halves.
        mov     a4, a2, lsr #16     ; Split other input,
        eor     v2, a2, a4, lsl #16 ; into two halves.
        mul     a1, a3, a4          ; Produce,
        mul     a2, v1, v2          ; the,
        mul     a3, v2, a3          ; partial,
        mul     a4, v1, a4          ; products.
        adds    a2, a2, a4, lsl #16 ; Combine,
        adc     a1, a1, a4, lsr #16 ; the,
        adds    a2, a2, a3, lsl #16 ; partial,
        adc     a1, a1, a3, lsr #16 ; products.
; make result signed
        cmps    v3, #0              ; check result sign
        beq     |exit_c|            ; don't negate result if positive
        rsbs    a2, a2, #0          ; else negate,
        rsc     a1, a1, #0          ; the result.
|exit_c|
; shift and round
        adds    a2, a2, #0x80000000 ; add 1 to the bit below,
        adc     a1, a1, #0          ; the msw for rounding.
        ldmfd   sp!,{v1,v2,v3,pc}^  ; return, a1 = msw of result.

; mulsr:
; ------
; Fractional multiply with single bit left shift and rounding.
        export  |mulsr|
|mulsr|
        stmfd   sp!,{v1,v2,v3,lr}
; strip signs
        cmps    a1, #0              ; check sign of input
        rsbmi   a1, a1, #0          ; create abs of input
        mvnmi   a3, #0              ; save sign,
        movpl   a3, #0              ; of input
        cmps    a2, #0              ; check sign of other input
        rsbmi   a2, a2, #0          ; create abs of other input
        mvnmi   a4, #0              ; save sign,
        movpl   a4, #0              ; of other input
        eor     v3, a3, a4          ; combine signs and save
; unsigned long multiply
        mov     a3, a1, lsr #16     ; Split input ,
        eor     v1, a1, a3, lsl #16 ; into two halves.
        mov     a4, a2, lsr #16     ; Split other input,
        eor     v2, a2, a4, lsl #16 ; into two halves.
        mul     a1, a3, a4          ; Produce,
        mul     a2, v1, v2          ; the,
        mul     a3, v2, a3          ; partial,
        mul     a4, v1, a4          ; products.
        adds    a2, a2, a4, lsl #16 ; Combine,
        adc     a1, a1, a4, lsr #16 ; the,
        adds    a2, a2, a3, lsl #16 ; partial,
        adc     a1, a1, a3, lsr #16 ; products.
; make result signed
        cmps    v3, #0              ; check result sign
        beq     |exit_d|            ; don't negate result if positive
        rsbs    a2, a2, #0          ; else negate,
        rsc     a1, a1, #0          ; the result.
|exit_d|
; shift and round
        movs    a2, a2, lsl #1      ; shift lsw 1 bit left,
        adc     a1, a1, a1          ; and msw, for fractional result.
        adds    a2, a2, #0x80000000 ; add 1 to the bit below,
        adc     a1, a1, #0          ; the msw for rounding.
        ldmfd   sp!,{v1,v2,v3,pc}^  ; return, a1 = msw of result.

        end

