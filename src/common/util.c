#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int64_t wall_time(void) { return (int64_t)time(NULL); }

void sleep_us(int64_t us)
{
    if (us <= 0)
        return;
    struct timespec ts = {.tv_sec = us / 1000000, .tv_nsec = (us % 1000000) * 1000};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

/* ------------------------------------------------------------------ logging */

static char g_proc[24] = "plutus";
static int g_log_fd = 2;
static int g_min_level = L_INFO;
static const char *g_level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};

void log_init(const char *proc_name, int fd, int min_level)
{
    snprintf(g_proc, sizeof g_proc, "%s", proc_name);
    g_log_fd = fd;
    g_min_level = min_level;
}

static void vlog(int level, const char *fmt, va_list ap)
{
    if (level < g_min_level)
        return;
    char line[PIPE_BUF];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    int n = snprintf(line, sizeof line, "%02d:%02d:%02d.%03ld %-8s %-5s ", tm.tm_hour, tm.tm_min,
                     tm.tm_sec, ts.tv_nsec / 1000000, g_proc, g_level_names[level]);
    int m = vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    if (m < 0)
        m = 0;
    n += m;
    if (n > (int)sizeof line - 2)
        n = (int)sizeof line - 2;
    line[n++] = '\n';
    /* one write() <= PIPE_BUF is atomic on a pipe */
    ssize_t w = write(g_log_fd, line, (size_t)n);
    (void)w;
}

void plog(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(level, fmt, ap);
    va_end(ap);
}

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(L_ERROR, fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    if (g_log_fd != 2) {
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    }
    va_end(ap);
    exit(1);
}

/* ------------------------------------------------------------------ sbuf */

void sb_init(sbuf_t *sb)
{
    sb->cap = 256;
    sb->len = 0;
    sb->buf = malloc(sb->cap);
    if (!sb->buf)
        die("out of memory");
    sb->buf[0] = '\0';
}

static void sb_reserve(sbuf_t *sb, size_t extra)
{
    if (sb->len + extra + 1 <= sb->cap)
        return;
    while (sb->len + extra + 1 > sb->cap)
        sb->cap *= 2;
    sb->buf = realloc(sb->buf, sb->cap);
    if (!sb->buf)
        die("out of memory");
}

void sb_vprintf(sbuf_t *sb, const char *fmt, va_list ap)
{
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n <= 0)
        return;
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap);
    sb->len += (size_t)n;
}

void sb_printf(sbuf_t *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(sb, fmt, ap);
    va_end(ap);
}

void sb_putc(sbuf_t *sb, char c)
{
    sb_reserve(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

void sb_reset(sbuf_t *sb)
{
    sb->len = 0;
    sb->buf[0] = '\0';
}

void sb_free(sbuf_t *sb)
{
    free(sb->buf);
    sb->buf = NULL;
    sb->len = sb->cap = 0;
}

/* ------------------------------------------------------------------ parsing */

bool parse_i64(const char *s, int64_t *out)
{
    if (!s || !*s)
        return false;
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno || *end)
        return false;
    *out = v;
    return true;
}

bool parse_int(const char *s, int *out)
{
    int64_t v;
    if (!parse_i64(s, &v) || v < INT_MIN || v > INT_MAX)
        return false;
    *out = (int)v;
    return true;
}

bool parse_money(const char *s, int64_t *paise)
{
    if (!s || !*s)
        return false;
    int64_t whole = 0, frac = 0;
    int fdigits = 0;
    const char *p = s;
    if (!isdigit((unsigned char)*p))
        return false;
    for (; isdigit((unsigned char)*p); p++) {
        whole = whole * 10 + (*p - '0');
        if (whole > 100000000000LL) /* 10^11 rupees is far beyond any limit */
            return false;
    }
    if (*p == '.') {
        p++;
        for (; isdigit((unsigned char)*p) && fdigits < 2; p++, fdigits++)
            frac = frac * 10 + (*p - '0');
        if (fdigits == 1)
            frac *= 10;
    }
    if (*p)
        return false;
    *paise = whole * 100 + frac;
    return true;
}

const char *fmt_money(int64_t paise, char *out, size_t n)
{
    bool neg = paise < 0;
    uint64_t v = neg ? (uint64_t)(-paise) : (uint64_t)paise;
    uint64_t rupees = v / 100, ps = v % 100;
    char digits[32];
    int len = snprintf(digits, sizeof digits, "%llu", (unsigned long long)rupees);
    /* Indian grouping: last three digits, then groups of two (12,34,567) */
    char grouped[48];
    int gi = 0;
    for (int i = 0; i < len; i++) {
        int after = len - i - 1; /* digits still to come */
        grouped[gi++] = digits[i];
        if (after >= 3 && (after - 3) % 2 == 0)
            grouped[gi++] = ',';
    }
    grouped[gi] = '\0';
    snprintf(out, n, "%sRs %s.%02llu", neg ? "-" : "", grouped, (unsigned long long)ps);
    return out;
}

bool valid_person_name(const char *s)
{
    if (!s || !*s || strlen(s) > 47)
        return false;
    bool letter = false;
    for (; *s; s++) {
        if (isalpha((unsigned char)*s))
            letter = true;
        else if (*s != ' ' && *s != '.')
            return false;
    }
    return letter;
}

/* ------------------------------------------------------------------ random */

void random_bytes(void *buf, size_t n) { arc4random_buf(buf, n); }
uint32_t rand_below(uint32_t n) { return n ? arc4random_uniform(n) : 0; }

void hex_encode(const uint8_t *in, size_t n, char *out)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = hx[in[i] >> 4];
        out[2 * i + 1] = hx[in[i] & 15];
    }
    out[2 * n] = '\0';
}
