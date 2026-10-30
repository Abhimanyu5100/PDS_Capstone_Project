#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../common/crypto.h"
#include "server.h"

const op_info_t OP_INFO[OP_COUNT] = {
    [OP_REGISTER] = {"REGISTER", 6, 7, false, 'R'},
    [OP_LOGIN] = {"LOGIN", 4, 3, false, 'L'},
    [OP_BALANCE] = {"BALANCE", 1, 1, true, 'b'},
    [OP_DEPOSIT] = {"DEPOSIT", 2, 2, true, 'd'},
    [OP_WITHDRAW] = {"WITHDRAW", 2, 2, true, 'w'},
    [OP_TRANSFER] = {"TRANSFER", 4, 3, true, 'T'},
    [OP_STATEMENT] = {"STATEMENT", 3, 1, true, 's'},
    [OP_FD_QUOTE] = {"FD_QUOTE", 1, 4, false, 'q'},
    [OP_FD_OPEN] = {"FD_OPEN", 5, 4, true, 'F'},
    [OP_FD_LIST] = {"FD_LIST", 2, 1, true, 'f'},
    [OP_FD_CLOSE] = {"FD_CLOSE", 4, 2, true, 'C'},
    [OP_LOAN_QUOTE] = {"LOAN_QUOTE", 2, 6, false, 'q'},
    [OP_LOAN_APPLY] = {"LOAN_APPLY", 8, 6, true, 'A'},
    [OP_LOAN_LIST] = {"LOAN_LIST", 2, 1, true, 'l'},
    [OP_LOAN_DRAW] = {"LOAN_DRAW", 4, 3, true, 'D'},
    [OP_LOAN_REPAY] = {"LOAN_REPAY", 3, 3, true, 'P'},
    [OP_INS_QUOTE] = {"INS_QUOTE", 1, 5, false, 'q'},
    [OP_INS_BUY] = {"INS_BUY", 6, 7, true, 'I'},
    [OP_INS_LIST] = {"INS_LIST", 2, 1, true, 'i'},
    [OP_MONTH_END] = {"MONTH_END", 40, 1, false, 'M'},
    [OP_SYNTHETIC] = {"SYNTHETIC", 1, 1, false, 'x'},
};

int op_lookup(const char *name)
{
    for (int i = 0; i < OP_COUNT; i++)
        if (!strcmp(name, OP_INFO[i].name))
            return i;
    return -1;
}

void shm_name_for(const char *data_dir, char *out, size_t n)
{
    char real[PATH_MAX];
    if (!realpath(data_dir, real))
        snprintf(real, sizeof real, "%s", data_dir);
    /* macOS limits shm names to 31 characters */
    snprintf(out, n, "/plutus-%u-%08x", (unsigned)getuid(), crc32(real, strlen(real)));
}

shm_stats_t *shm_create(const char *data_dir)
{
    char name[64];
    shm_name_for(data_dir, name, sizeof name);
    shm_unlink(name); /* a stale segment from a crashed run */
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return NULL;
    if (ftruncate(fd, sizeof(shm_stats_t)) < 0) {
        close(fd);
        return NULL;
    }
    shm_stats_t *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (s == MAP_FAILED)
        return NULL;
    memset(s, 0, sizeof *s);
    s->magic = SHM_MAGIC;
    s->version = SHM_VERSION;
    return s;
}

shm_stats_t *shm_attach(const char *data_dir, bool writable)
{
    char name[64];
    shm_name_for(data_dir, name, sizeof name);
    int fd = shm_open(name, writable ? O_RDWR : O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    shm_stats_t *s = mmap(NULL, sizeof *s, writable ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (s == MAP_FAILED)
        return NULL;
    if (s->magic != SHM_MAGIC || s->version != SHM_VERSION) {
        munmap(s, sizeof *s);
        errno = EPROTO;
        return NULL;
    }
    return s;
}

void shm_unlink_for(const char *data_dir)
{
    char name[64];
    shm_name_for(data_dir, name, sizeof name);
    shm_unlink(name);
}
