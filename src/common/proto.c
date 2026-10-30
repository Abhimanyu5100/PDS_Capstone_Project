#include "proto.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

void conn_init(conn_t *c, int fd)
{
    c->fd = fd;
    c->rlen = 0;
}

int conn_read_line(conn_t *c, char *out, size_t cap)
{
    for (;;) {
        char *nl = memchr(c->rbuf, '\n', c->rlen);
        if (nl) {
            size_t n = (size_t)(nl - c->rbuf);
            if (n >= cap)
                return -1;
            memcpy(out, c->rbuf, n);
            out[n] = '\0';
            if (n && out[n - 1] == '\r')
                out[--n] = '\0';
            size_t rest = c->rlen - (size_t)(nl - c->rbuf) - 1;
            memmove(c->rbuf, nl + 1, rest);
            c->rlen = rest;
            return n ? (int)n : -2;
        }
        if (c->rlen == sizeof c->rbuf)
            return -1;
        ssize_t r = read(c->fd, c->rbuf + c->rlen, sizeof c->rbuf - c->rlen);
        if (r == 0)
            return 0;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        c->rlen += (size_t)r;
    }
}

bool conn_write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

int proto_split(char *line, char *fields[], int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        fields[n++] = p;
        char *t = strchr(p, '\t');
        if (!t)
            break;
        *t = '\0';
        p = t + 1;
    }
    return n;
}

int proto_call(conn_t *c, sbuf_t *body, char *status, size_t status_cap, int nfields,
               const char *const fields[])
{
    sbuf_t req;
    sb_init(&req);
    for (int i = 0; i < nfields; i++) {
        if (i)
            sb_putc(&req, '\t');
        sb_printf(&req, "%s", fields[i]);
    }
    sb_putc(&req, '\n');
    bool ok = conn_write_all(c->fd, req.buf, req.len);
    sb_free(&req);
    if (!ok)
        return -1;
    if (body)
        sb_reset(body);
    char line[PROTO_MAX_LINE];
    for (;;) {
        int n = conn_read_line(c, line, sizeof line);
        if (n == -2)
            continue; /* blank line */
        if (n <= 0)
            return -1;
        if (line[0] == '|') {
            if (body)
                sb_printf(body, "%s\n", line[1] == ' ' ? line + 2 : line + 1);
            continue;
        }
        snprintf(status, status_cap, "%s", line);
        return strncmp(line, "OK", 2) == 0 ? 1 : 0;
    }
}

/* sun_path holds only about 104 bytes, and a data directory can be deeper than
 * that. A longer path is bound or connected by its short final name from
 * inside its directory, then the old working directory is restored. The
 * working directory belongs to the whole process, so the switch is serialised
 * here; nothing in this project opens relative paths from other threads
 * meanwhile (the server resolves its data directory to an absolute path). */
static pthread_mutex_t g_cwd_lock = PTHREAD_MUTEX_INITIALIZER;

static int sock_addr(int fd, const char *path, bool do_bind)
{
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    if (strlen(path) < sizeof sa.sun_path) {
        strcpy(sa.sun_path, path);
        return do_bind ? bind(fd, (struct sockaddr *)&sa, sizeof sa) : connect(fd, (struct sockaddr *)&sa, sizeof sa);
    }
    const char *slash = strrchr(path, '/');
    char dir[PATH_MAX];
    size_t dlen = slash == path ? 1 : (size_t)(slash - path);
    if (!slash || strlen(slash + 1) >= sizeof sa.sun_path || dlen >= sizeof dir) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(sa.sun_path, slash + 1);
    memcpy(dir, path, dlen);
    dir[dlen] = '\0';

    pthread_mutex_lock(&g_cwd_lock);
    int rc = -1, e = 0;
    int here = open(".", O_RDONLY | O_CLOEXEC);
    if (here < 0 || chdir(dir) != 0) {
        e = errno;
    } else {
        rc = do_bind ? bind(fd, (struct sockaddr *)&sa, sizeof sa) : connect(fd, (struct sockaddr *)&sa, sizeof sa);
        e = errno;
        if (fchdir(here) != 0) { /* cannot get back: do not leave a half-working socket */
            e = errno;
            rc = -1;
        }
    }
    if (here >= 0)
        close(here);
    pthread_mutex_unlock(&g_cwd_lock);
    errno = e;
    return rc;
}

int unix_listen(const char *path, int mode)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    unlink(path);
    if (sock_addr(fd, path, true) < 0 || chmod(path, (mode_t)mode) < 0 || listen(fd, 128) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

int unix_connect(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (sock_addr(fd, path, false) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}
