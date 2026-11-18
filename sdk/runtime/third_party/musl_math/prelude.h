// Athelas: before a group of musl's files (third_party/musl_*.cpp, inside namespace det): the system headers they
// expect, the same operations on every PC (no fused multiply-add), and musl's own warnings quietened
#pragma once
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <float.h>
#include <math.h>
#include <stdint.h>
#define hidden  // musl: symbols internal to its library (the data headers use it before libm.h)
#pragma clang fp contract(off)
#undef __FP_FAST_FMA
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-macros"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wparentheses"
#pragma clang diagnostic ignored "-Wmissing-braces"
#pragma clang diagnostic ignored "-Wc99-designator"
#pragma clang diagnostic ignored "-Wunknown-pragmas"
// musl initialises its tables by field name in C order; clang does the same in C++, with this warning
#pragma clang diagnostic ignored "-Wreorder-init-list"
