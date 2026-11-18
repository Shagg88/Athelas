// The CRT's x87 math checks its argument in a helper call and branches on the ZF that call returns. Recompiled code
// does not carry flags across call and ret, so the functions read stale flags and returned NaN at random. Computed on
// the host instead.
#include <cmath>

#include "cpu/det_math.h"
#include "xdk/hle.h"

// each function's body, shared by its stack entry (sin) and its register entry (_CIsin): x in st0, popped by its ret
#define CRT_MATH(name, fn)         \
    X86_FUNC(GUEST(name)) {        \
        (void)base;                \
        X87_ST(0) = fn(X87_ST(0)); \
        ctx.esp += 4;              \
    }
#ifndef SYM_crt_sin
#define SYM_crt_sin FFFFFD00
#define SYM_crt_cos FFFFFD01
#define SYM_crt_tan FFFFFD02
#define SYM_crt_asin FFFFFD03
#define SYM_crt_acos FFFFFD04
#define SYM_crt_atan FFFFFD05
#define SYM_crt_sqrt FFFFFD06
#define SYM_crt_log FFFFFD07
#define SYM_crt_log10 FFFFFD08
#define SYM_crt_pow FFFFFD09
#endif
CRT_MATH(crt_sin, x86_sin)
CRT_MATH(crt_cos, x86_cos)
CRT_MATH(crt_tan, x86_tan)
CRT_MATH(crt_asin, x86_asin)
CRT_MATH(crt_acos, x86_acos)
CRT_MATH(crt_atan, x86_atan)
CRT_MATH(crt_sqrt, std::sqrt)
CRT_MATH(crt_log, x86_log)
CRT_MATH(crt_log10, x86_log10)
#undef CRT_MATH

// x and y above the return address, y in st0
X86_FUNC(GUEST(crt_pow)) {
    X87_ST(0) = x86_pow(double(X86_MEMF64(ctx.esp + 4)), double(X86_MEMF64(ctx.esp + 12)));
    ctx.esp += 4;
}

// configs written before the bodies had names
X86_FUNC(GUEST(_CIacos)) {
    (void)base;
    X87_ST(0) = x86_acos(X87_ST(0));
    ctx.esp += 4;
}

X86_FUNC(GUEST(_CIasin)) {
    (void)base;
    X87_ST(0) = x86_asin(X87_ST(0));
    ctx.esp += 4;
}
