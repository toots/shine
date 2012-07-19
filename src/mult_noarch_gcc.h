
#warning NO MULT FILE FOR ARCHITECTURE - USING GENERIC MATH

/*finish this later */

#define __s64 long long
#define __s32 long

#define mul(a,b)  (__s32) ( ((__s64)(a) * (__s64)(b)) >>32 )

#define muls(a,b) (__s32) ( ((__s64)(a) * (__s64)(b)) >>32 )

#define mulr(a,b) (__s32) ( ( (__s64)((__s64)(a) * (__s64)(b)) +0x80000000 )>>32 )

#define mulsr(a,b) (__s32) ( ( (__s64)((__s64)(a) * (__s64)(b)) +0x80000000 )>>32 )

