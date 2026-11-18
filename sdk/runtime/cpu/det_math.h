// The math the guest's x87 transcendentals and CRT math functions use (generated code, xdk/crt_math.cpp, dsound's
// pitch). Normal play: the C library's, as always. Lockstep mode (online co-op, kernel/lockstep.h): musl's, built into
// the exe (cpu/det_math.cpp), so two PCs compute the same bits whatever their processor or Windows version.
#pragma once

double x86_sin(double x);
double x86_cos(double x);
double x86_tan(double x);
double x86_asin(double x);
double x86_acos(double x);
double x86_atan(double x);
double x86_atan2(double y, double x);
double x86_exp2(double x);
double x86_log2(double x);
double x86_log(double x);
double x86_log10(double x);
double x86_pow(double x, double y);
