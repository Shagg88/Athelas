// the switch between the C library's math and musl's (third_party/musl_math, MIT, compiled into namespace det so it
// never meets the C library's functions of the same name) - cpu/det_math.h
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <float.h>
#include <math.h>
#include <stdint.h>

#include "cpu/det_math.h"
#include "kernel/lockstep.h"

// musl's functions, compiled in third_party/musl_*.cpp
namespace det {
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
double exp2(double);
double log2(double);
double log(double);
double log10(double);
double pow(double, double);
}  // namespace det

#define X86_MATH1(name)                                                \
    double x86_##name(double x) {                                      \
        return kernel::ls::g_on ? det::name(x) : std::name(x);         \
    }
X86_MATH1(sin)
X86_MATH1(cos)
X86_MATH1(tan)
X86_MATH1(asin)
X86_MATH1(acos)
X86_MATH1(atan)
X86_MATH1(exp2)
X86_MATH1(log2)
X86_MATH1(log)
X86_MATH1(log10)
double x86_atan2(double y, double x) { return kernel::ls::g_on ? det::atan2(y, x) : std::atan2(y, x); }
double x86_pow(double x, double y) { return kernel::ls::g_on ? det::pow(x, y) : std::pow(x, y); }
