/* wasm_shim.c — minimal freestanding libc for the BradDevices WASM build.
 *
 * Provides exactly the subset of libc that the BradVector reference
 * toolchain needs (bradc.c / bvbc.c / bvrt.c), so bradc+bvrt can be
 * compiled to WebAssembly with no Emscripten / WASI dependency.
 *
 * Memory note: allocations come from a fixed 6 MB arena (zero-filled
 * .bss, so it costs nothing in the binary).  The arena high-water is
 * reset at the start of each exported bridge call, and bvrt's
 * device+backing memory is the only large consumer, so a single
 * freestanding bump allocator is sufficient.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* ---------- math (native wasm opcodes) ---------- */

float sqrtf(float x) { return __builtin_sqrtf(x); }
float fabsf(float x) { return __builtin_fabsf(x); }

/* ---------- transcendentals (polynomial; no libm in freestanding) ----------
 * The reference ISA special functions (EXP2/LOG2/SIN/COS) need these in
 * BVRT.  Accuracy is ~1e-6..1e-5, adequate for the reference model's
 * interactive demo; the native build uses the platform libm. */

static float shim_ldexpf(float x, int n)
{
    union { float f; uint32_t u; } v;
    v.f = x;
    v.u = (v.u & 0x807FFFFFu) | ((uint32_t)(n + 127) << 23);
    return v.f;
}

float exp2f(float x)
{
    float n = __builtin_floorf(x);
    float f = x - n;                       /* f in [0,1) */
    /* 2^f = sum (f*ln2)^k / k!, degree 6 */
    float p = 1.5403530e-4f;
    p = p * f + 1.3333558e-3f;
    p = p * f + 9.6181291e-3f;
    p = p * f + 5.5504109e-2f;
    p = p * f + 2.4022651e-1f;
    p = p * f + 6.9314718e-1f;
    p = p * f + 1.0f;
    int ni = (int)n;
    if (ni < -126) return 0.0f;
    if (ni > 127) return 3.4028235e38f;
    return shim_ldexpf(p, ni);
}

float log2f(float x)
{
    if (x <= 0.0f) return -3.4028235e38f;
    union { float f; uint32_t u; } v;
    v.f = x;
    int e = (int)((v.u >> 23) & 0xFFu) - 127;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;   /* m in [1,2) */
    float m = v.f;
    float z = (m - 1.0f) / (m + 1.0f);         /* z in [0,1/3) */
    float z2 = z * z;
    float p = 1.0f / 7.0f;
    p = p * z2 + 1.0f / 5.0f;
    p = p * z2 + 1.0f / 3.0f;
    p = p * z2 + 1.0f;
    return (float)e + 2.8853901f * (z * p);    /* 2/ln2 * atanh(z) */
}

static float sin_poly(float r)   /* |r| <= pi/4 */
{
    float r2 = r * r;
    float p = -1.0f / 5040.0f;
    p = p * r2 + 1.0f / 120.0f;
    p = p * r2 - 1.0f / 6.0f;
    p = p * r2 + 1.0f;
    return r * p;
}

static float cos_poly(float r)
{
    float r2 = r * r;
    float p = 1.0f / 40320.0f;
    p = p * r2 - 1.0f / 720.0f;
    p = p * r2 + 1.0f / 24.0f;
    p = p * r2 - 0.5f;
    p = p * r2 + 1.0f;
    return p;
}

float sinf(float x)
{
    float q = __builtin_floorf(x * 0.6366197723675814f + 0.5f); /* nearest 2/pi */
    float r = x - q * 1.5707963267948966f;
    int qi = ((int)q) & 3;
    if (qi < 0) qi += 4;
    float s = sin_poly(r), c = cos_poly(r);
    switch (qi) {
    case 0: return s;
    case 1: return c;
    case 2: return -s;
    default: return -c;
    }
}

float cosf(float x)
{
    float q = __builtin_floorf(x * 0.6366197723675814f + 0.5f);
    float r = x - q * 1.5707963267948966f;
    int qi = ((int)q) & 3;
    if (qi < 0) qi += 4;
    float s = sin_poly(r), c = cos_poly(r);
    switch (qi) {
    case 0: return c;
    case 1: return -s;
    case 2: return -c;
    default: return s;
    }
}

/* ---------- memory ---------- */

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        size_t i = n;
        while (i > 0) { i--; d[i] = s[i]; }
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

/* ---------- strings ---------- */

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i]) { d[i] = s[i]; i++; }
    while (i < n) { d[i] = 0; i++; }
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *strchr(const char *s, int ch)
{
    char c = (char)ch;
    while (*s) { if (*s == c) return (char *)s; s++; }
    return 0;
}

char *strrchr(const char *s, int ch)
{
    char c = (char)ch;
    const char *r = 0;
    while (*s) { if (*s == c) r = s; s++; }
    return (char *)r;
}

char *strstr(const char *h, const char *n)
{
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return 0;
}

size_t strcspn(const char *s, const char *rej)
{
    size_t n = 0;
    while (s[n]) {
        const char *r = rej;
        while (*r) { if (*r == s[n]) return n; r++; }
        n++;
    }
    return n;
}

size_t strspn(const char *s, const char *acc)
{
    size_t n = 0;
    while (s[n]) {
        const char *r = acc;
        int hit = 0;
        while (*r) { if (*r == s[n]) { hit = 1; break; } r++; }
        if (!hit) break;
        n++;
    }
    return n;
}

/* ---------- stdlib ---------- */

long strtol(const char *s, char **end, int base)
{
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') p++;
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0') { base = 8; }  /* '0' is a digit, not a prefix */
        else base = 10;
    } else if (base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    }
    unsigned long acc = 0;
    int any = 0;
    for (;; p++) {
        int d;
        char c = *p;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        any = 1;
        acc = acc * (unsigned long)base + (unsigned long)d;
    }
    if (end) *end = any ? (char *)p : (char *)s;
    if (!any) return 0;
    return neg ? -(long)acc : (long)acc;
}

static unsigned long g_rand_state = 1;

void srand(unsigned s) { g_rand_state = s ? s : 1; }
int rand(void)
{
    g_rand_state = g_rand_state * 1103515245UL + 12345UL;
    return (int)((g_rand_state >> 16) & 0x7FFF);
}

/* ---------- arena allocator ---------- */

static unsigned char g_arena[6U * 1024U * 1024U] __attribute__((aligned(16)));
static size_t g_hb = 0;

void wasm_reset_arena(void) { g_hb = 0; }

void *malloc(size_t n)
{
    n = (n + 15UL) & ~15UL;
    if (g_hb + n > sizeof(g_arena)) return 0;
    void *r = g_arena + g_hb;
    g_hb += n;
    return r;
}

void *calloc(size_t n, size_t s)
{
    size_t t = n * s;
    void *p = malloc(t);
    if (p) memset(p, 0, t);
    return p;
}

void free(void *p) { (void)p; }

/* ---------- printf family (subset: c,d,i,u,x,X,o,s,%) ---------- */

static size_t ufmt(char *b, unsigned long v, int base, int upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char t[24];
    int i = 0;
    if (v == 0) t[i++] = '0';
    while (v) { t[i++] = digits[v % (unsigned long)base]; v /= (unsigned long)base; }
    size_t n = 0;
    while (i > 0) b[n++] = t[--i];
    return n;
}

/* Fixed-point decimal of a non-negative double with `prec` digits. */
static size_t ffmt(char *b, size_t n, double v, int prec)
{
    if (prec < 0) prec = 0;
    if (prec > 9) prec = 9;
    double scale = 1.0;
    for (int i = 0; i < prec; i++) scale *= 10.0;
    unsigned long long r = (unsigned long long)(v * scale + 0.5);
    unsigned long long ip = r / (unsigned long long)scale;
    unsigned long long fp = r % (unsigned long long)scale;

    char t[32];
    int ti = 0;
    if (ip == 0) t[ti++] = '0';
    while (ip) { t[ti++] = '0' + (unsigned)(ip % 10); ip /= 10; }

    size_t o = 0;
    for (int k = ti - 1; k >= 0 && o + 1 < n; k--) b[o++] = t[k];
    if (prec > 0 && o + 1 < n) b[o++] = '.';
    unsigned long long d = 1;
    for (int i = 0; i < prec - 1; i++) d *= 10;
    for (int k = 0; k < prec && o + 1 < n; k++) {
        b[o++] = '0' + (unsigned)((fp / d) % 10);
        if (d > 1) d /= 10;
    }
    return o;
}

int vsnprintf(char *out, size_t sz, const char *fmt, va_list ap)
{
    if (sz == 0) return 0;
    char *o = out;
    size_t rem = sz;

    for (const char *f = fmt; *f; f++) {
        if (*f != '%') {
            if (rem > 1) { *o++ = *f; rem--; }
            continue;
        }
        f++;
        if (*f == '%') {
            if (rem > 1) { *o++ = '%'; rem--; }
            continue;
        }
        int left = 0, zero = 0, width = 0;
        while (*f == '-') { left = 1; f++; }
        while (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        int prec = 6;
        if (*f == '.') {
            f++;
            prec = 0;
            while (*f >= '0' && *f <= '9') { prec = prec * 10 + (*f - '0'); f++; }
        }
        while (*f == 'l' || *f == 'z' || *f == 'h') f++;

        char tmp[128];
        size_t tn = 0;
        int sg = 0;
        switch (*f) {
        case 'd': case 'i': {
            int v = va_arg(ap, int);
            if (v < 0) { sg = 1; tn = ufmt(tmp, (unsigned long)(-(long)v), 10, 0); }
            else tn = ufmt(tmp, (unsigned long)v, 10, 0);
            break;
        }
        case 'u': tn = ufmt(tmp, (unsigned long)va_arg(ap, unsigned), 10, 0); break;
        case 'x': case 'X': tn = ufmt(tmp, (unsigned long)va_arg(ap, unsigned), 16, *f == 'X'); break;
        case 'o': tn = ufmt(tmp, (unsigned long)va_arg(ap, unsigned), 8, 0); break;
        case 'c': tmp[0] = (char)va_arg(ap, int); tn = 1; break;
        case 'f': {
            double v = va_arg(ap, double);
            if (v < 0.0) { sg = 1; v = -v; }
            tn = ffmt(tmp, sizeof(tmp), v, prec);
            break;
        }
        case 's': {
            const char *v = va_arg(ap, const char *);
            if (!v) v = "(null)";
            tn = strlen(v);
            if (tn > sizeof(tmp) - 1) tn = sizeof(tmp) - 1;
            for (size_t i = 0; i < tn; i++) tmp[i] = v[i];
            break;
        }
        default:
            if (*f) {
                if (rem > 1) { *o++ = '%'; rem--; }
                if (rem > 1) { *o++ = *f; rem--; }
            }
            continue; /* %d consumed already */
        }

        int lead = 0;
        if ((int)(tn + (sg ? 1 : 0)) < width)
            lead = width - (int)(tn + (sg ? 1 : 0));

        if (!left && lead > 0) {
            for (; lead > 0 && rem > 1; lead--) { *o++ = (zero && !sg) ? '0' : ' '; rem--; }
        }
        if (sg && rem > 1) { *o++ = '-'; rem--; }
        for (size_t i = 0; i < tn && rem > 1; i++) { *o++ = tmp[i]; rem--; }
        if (left && lead > 0) {
            for (; lead > 0 && rem > 1; lead--) { *o++ = ' '; rem--; }
        }
    }

    if (rem > 0) *o = 0;
    else out[sz - 1] = 0;
    return (int)(o - out);
}

int snprintf(char *out, size_t sz, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, sz, fmt, ap);
    va_end(ap);
    return n;
}

/* printf — formatted output is discarded in the freestanding build;
 * bradgdb's optional instruction trace uses it only when tracing is on
 * (never for the exported demo/typed paths). */
int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char tmp[256];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    return n;
}

/* ---------- 64-bit division helpers (wasm32 has no i64 div opcodes) ---------- */

static uint64_t udivmod(uint64_t a, uint64_t b, int *is_div)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        unsigned char bit = (unsigned char)((a >> i) & 1);
        r = (r << 1) | bit;
        if (r >= b) { r -= b; q |= (1ULL << i); }
    }
    return *is_div ? q : r;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) { int d = 1; return udivmod(a, b, &d); }
uint64_t __umoddi3(uint64_t a, uint64_t b) { int d = 0; return udivmod(a, b, &d); }
int64_t __divdi3(int64_t a, int64_t b)
{
    int neg = (a < 0) != (b < 0);
    uint64_t ua = a < 0 ? (uint64_t)(-(uint64_t)a) : (uint64_t)a;
    uint64_t ub = b < 0 ? (uint64_t)(-(uint64_t)b) : (uint64_t)b;
    uint64_t q = __udivdi3(ua, ub);
    return neg ? (int64_t)(0 - q) : (int64_t)q;
}
int64_t __moddi3(int64_t a, int64_t b)
{
    int neg = a < 0;
    uint64_t ua = a < 0 ? (uint64_t)(-(uint64_t)a) : (uint64_t)a;
    uint64_t ub = b < 0 ? (uint64_t)(-(uint64_t)b) : (uint64_t)b;
    uint64_t r = __umoddi3(ua, ub);
    return neg ? (int64_t)(0 - r) : (int64_t)r;
}