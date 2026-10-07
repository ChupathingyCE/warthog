/*
MATH.H

The console's <math.h> (through port/windows/include/crt/math.h), and the
C99 functions its C runtime does not have (port/xbox360/src/x360_crt.c).
*/

#include_next <math.h>
#include <float.h>

#ifndef __HALO_XBOX360_MATH_H
#define __HALO_XBOX360_MATH_H

long lrintf(float value);
long lrint(double value);
float roundf(float value);
double round(double value);
float truncf(float value);
double trunc(double value);
float fminf(float left, float right);
float fmaxf(float left, float right);
double fmin(double left, double right);
double fmax(double left, double right);
float log2f(float value);
double log2(double value);
float exp2f(float value);
double exp2(double value);
float cbrtf(float value);

#ifndef isnan
#define isnan(value) _isnan(value)
#endif
#ifndef isinf
#define isinf(value) (!_finite(value) && !_isnan(value))
#endif
#ifndef isfinite
#define isfinite(value) _finite(value)
#endif
#ifndef NAN
#define NAN (__builtin_nanf(""))
#endif
#ifndef INFINITY
#define INFINITY (__builtin_inff())
#endif

#endif
