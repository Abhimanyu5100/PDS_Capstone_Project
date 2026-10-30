/* proto.h - the Plutus wire protocol over Unix domain stream sockets.
 *
 * Request:  one line, fields separated by TAB, terminated by '\n'.
 *           e.g. "TRANSFER\t100004217\t2500.00\n"
 * Response: zero or more body lines, each starting with "| ", then exactly one
 *           status line that starts with "OK" or "ERR".
 *           e.g. "| Balance : Rs 12,500.00\nOK\t1250000\n"
 *
 * Text framing keeps the protocol debuggable with `nc -U plutus.sock`. */
#ifndef PLUTUS_PROTO_H
#define PLUTUS_PROTO_H

#include <stdbool.h>
#include <stddef.h>

#include "util.h"

#define PROTO_MAX_LINE 4096
#define PROTO_MAX_FIELDS 16

typedef struct {
    int fd;
    char rbuf[PROTO_MAX_LINE * 2];
    size_t rlen;
} conn_t;

void conn_init(conn_t *c, int fd);
/* Reads one line (without '\n') into out. Returns its length, -2 for an empty
 * line, 0 on EOF, -1 on error or an over-long line. */
int conn_read_line(conn_t *c, char *out, size_t cap);
bool conn_write_all(int fd, const void *buf, size_t n);

/* Splits line in place on TAB. Returns the field count. */
int proto_split(char *line, char *fields[], int max);

/* Client side: send a request built from fields, collect body lines into body
 * (with the "| " prefix stripped) and the status line into status. Returns
 * 1 for OK, 0 for ERR, -1 for a broken connection. */
int proto_call(conn_t *c, sbuf_t *body, char *status, size_t status_cap, int nfields,
               const char *const fields[]);

/* Unix-socket helpers */
int unix_listen(const char *path, int mode);
int unix_connect(const char *path);

#endif
