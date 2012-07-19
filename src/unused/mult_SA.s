;
; multiply
; --------
;
; 32 bit fractional multiplication.
; 06/03/01 v0.00 P.Everett
;
; No checks are made for overflow.
; Requires StrongARM for long multiply.
; To assemble, using the 'as' assembler:
;  as s.multiply -gcc -target SA1
;
; All have the following c declaration:
;  long mul(long a1, long a2);
;

        area    |C$$code|, CODE, READONLY
        align   4
; mul:
; ----
; Fractional multiply.
        export  |mul|
|mul|
        smull   a3, a1, a2, a1      ; signed integer long multiply.
        mov     pc, lr              ; return, a1 = msw of result.

; muls:
; -----
; Fractional multiply with single bit left shift.
        export  |muls|
|muls|
        smull   a3, a1, a2, a1      ; signed integer long multiply.
        movs    a3, a3, lsl #1      ; shift lsw 1 bit left,
        adc     a1, a1, a1          ; and msw, for fractional result.
        mov     pc, lr              ; return, a1 = msw of result.

; mulr:
; -----
; Fractional multiply with rounding.
        export  |mulr|
|mulr|
        smull   a3, a1, a2, a1      ; signed integer long multiply.
        adds    a3, a3, #0x80000000 ; add 1 to the bit below,
        adc     a1, a1, #0          ; the msw for rounding.
        mov     pc, lr              ; return, a1 = msw of result.

; mulsr:
; ------
; Fractional multiply with single bit left shift and rounding.
        export  |mulsr|
|mulsr|
        smull   a3, a1, a2, a1      ; signed integer long multiply.
        movs    a3, a3, lsl #1      ; shift lsw 1 bit left,
        adc     a1, a1, a1          ; and msw, for fractional result.
        adds    a3, a3, #0x80000000 ; add 1 to the bit below,
        adc     a1, a1, #0          ; the msw for rounding.
        mov     pc, lr              ; return, a1 = msw of result.

        end

