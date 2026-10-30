/* util.h - time, logging, money formatting and small string helpers. */
#ifndef PLUTUS_UTIL_H
#define PLUTUS_UTIL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* ---- time ---- */
int64_t now_ns(void);      /* monotonic clock */
int64_t wall_time(void);   /* seconds since epoch */
void sleep_us(int64_t us);

/* ---- logging ----
 * Every process in the daemon writes log lines to a pipe owned by the logger
 * process. Each line is emitted with a single write() no larger than PIPE_BUF,
 * so lines from different processes never interleave. */
enum { L_DEBUG, L_INFO, L_WARN, L_ERROR };
void log_init(const char *proc_name, int fd, int min_level);
void plog(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define LOGD(...) plog(L_DEBUG, __VA_ARGS__)
#define LOGI(...) plog(L_INFO, __VA_ARGS__)
#define LOGW(...) plog(L_WARN, __VA_ARGS__)
#define LOGE(...) plog(L_ERROR, __VA_ARGS__)
void die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

/* ---- growable string buffer ---- */
typedef struct {
    char *buf;
    size_t len, cap;
} sbuf_t;
void sb_init(sbuf_t *sb);
void sb_printf(sbuf_t *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sb_vprintf(sbuf_t *sb, const char *fmt, va_list ap);
void sb_putc(sbuf_t *sb, char c);
void sb_reset(sbuf_t *sb);
void sb_free(sbuf_t *sb);

/* ---- parsing / formatting ---- */
bool parse_i64(const char *s, int64_t *out);
bool parse_int(const char *s, int *out);
bool parse_money(const char *s, int64_t *paise); /* "1234.5" -> 123450 */
const char *fmt_money(int64_t paise, char *out, size_t n); /* "Rs 1,23,456.78" */
bool valid_person_name(const char *s);

/* ---- randomness (arc4random is seeded by the kernel) ---- */
void random_bytes(void *buf, size_t n);
uint32_t rand_below(uint32_t n);
void hex_encode(const uint8_t *in, size_t n, char *out);

#endif
