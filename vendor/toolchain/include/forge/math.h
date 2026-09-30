#ifndef FORGE_MATH_H
#define FORGE_MATH_H

#include <stdint.h>

int64_t fr_abs_i(int64_t n);
double fr_abs_f(double n);
int64_t fr_min_i(int64_t a, int64_t b);
int64_t fr_max_i(int64_t a, int64_t b);
int64_t fr_clamp_i(int64_t v, int64_t lo, int64_t hi);
int64_t fr_pow_i(int64_t base, int64_t exp);

#endif
