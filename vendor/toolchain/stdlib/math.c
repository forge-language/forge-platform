#include "forge/math.h"
#include <math.h>

int64_t fr_abs_i(int64_t n) {
    return n < 0 ? -n : n;
}

double fr_abs_f(double n) {
    return fabs(n);
}

int64_t fr_min_i(int64_t a, int64_t b) {
    return a < b ? a : b;
}

int64_t fr_max_i(int64_t a, int64_t b) {
    return a > b ? a : b;
}

int64_t fr_clamp_i(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int64_t fr_pow_i(int64_t base, int64_t exp) {
    if (exp < 0) return 0;
    int64_t r = 1;
    while (exp-- > 0) r *= base;
    return r;
}
