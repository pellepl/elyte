#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include "minio.h"
#include "utils.h"

static volatile noinit_t _noinit __attribute__((section(".noinit")));

float strtof(const char *s)
{
    const char *comma = strstr(s, ".");
    int inte = strtol(s, NULL, 0);
    int frac = 0;
    int div = 10;
    if (comma)
    {
        const char *sfrac = comma + 1;
        char *endptr;
        frac = strtol(sfrac, &endptr, 0);
        while (++sfrac < endptr - 1)
            div *= 10;
    }
    frac = inte < 0 ? -frac : frac;
    return (float)inte + (float)frac / (float)div;
}

#define MAX_FLOATS_IN_A_PRINTF 8
static struct
{
    char bufs[MAX_FLOATS_IN_A_PRINTF][24];
    uint8_t cur_buf;
} fstrbuf;

static inline uint32_t ufrac_f1000(float a)
{
    float aa = abs_f(a - (int32_t)a);
    uint32_t uf = (uint32_t)((aa + 0.0005f) * 1000);
    return min_u32(uf, 999);
}
static inline uint32_t ufrac_f10(float a)
{
    float aa = abs_f(a - (int32_t)a);
    uint32_t uf = (uint32_t)((aa + 0.05f) * 10);
    return min_u32(uf, 9);
}
const char *ftostr(float x)
{
    if (isnanf(x))
        return "NAN";
    fstrbuf.cur_buf = (fstrbuf.cur_buf + 1) % MAX_FLOATS_IN_A_PRINTF;
    char *buf_p = fstrbuf.bufs[fstrbuf.cur_buf];
    char *p = buf_p;
    if (x < 0)
    {
        *p++ = '-';
        x = -x;
    }
    p += snprintf(p, 22, "%d.%03d", int_f(x), ufrac_f1000(x));
    *p = '\0';
    return buf_p;
}

const char *ftostr1(float x)
{
    if (isnanf(x))
        return "NAN";
    fstrbuf.cur_buf = (fstrbuf.cur_buf + 1) % MAX_FLOATS_IN_A_PRINTF;
    char *buf_p = fstrbuf.bufs[fstrbuf.cur_buf];
    char *p = buf_p;
    if (x < 0)
    {
        *p++ = '-';
        x = -x;
    }
    p += snprintf(p, 22, "%d.%01d", int_f(x), ufrac_f10(x));
    *p = '\0';
    return buf_p;
}

bool begins_with(const char *str, const char *prefix)
{
    char c1;
    while ((c1 = *str++) != 0)
    {
        char c2 = *prefix++;
        if (c2 == 0)
            return true;
        if (c1 != c2)
            break;
    }
    return false;
}

volatile noinit_t *noinit(void) {
    return &_noinit;
}