/*
 * InfiniBand BMI handy utilities that are not really core functions.
 *
 * Copyright (C) 2003-6 Pete Wyckoff <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>

#define __util_c
#include "ib.h"

/*
 * Utility functions.
 */
void __attribute__((noreturn,format(printf,1,2))) __hidden
error(const char *fmt, ...)
{
    char s[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    gossip_err("Error: %s.\n", s);
    gossip_backtrace();
    exit(1);
}

void __attribute__((noreturn,format(printf,1,2))) __hidden
error_errno(const char *fmt, ...)
{
    char s[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    gossip_err("Error: %s: %s.\n", s, strerror(errno));
    exit(1);
}

void __attribute__((noreturn,format(printf,2,3))) __hidden
error_xerrno(int errnum, const char *fmt, ...)
{
    char s[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    gossip_err("Error: %s: %s.\n", s, strerror(errnum));
    exit(1);
}

void __attribute__((format(printf,1,2))) __hidden
warning(const char *fmt, ...)
{
    char s[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    gossip_err("Warning: %s.\n", s);
}

void __attribute__((format(printf,1,2))) __hidden
warning_errno(const char *fmt, ...)
{
    char s[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    gossip_err("Warning: %s: %s.\n", s, strerror(errno));
}

void __attribute__((format(printf,2,3))) __hidden
warning_xerrno(int errnum, const char *fmt, ...)
{
    char s[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    gossip_err("Warning: %s: %s.\n", s, strerror(errnum));
}

void * __attribute__((malloc)) __hidden
bmi_ib_malloc(unsigned long n)
{
    char *x;

    if (n == 0)
	error("%s: alloc 0 bytes", __func__);
    x = malloc(n);
    if (!x)
	error("%s: malloc %ld bytes failed", __func__, n);
    return x;
}

/*
 * Grab the first item and delete it from the list.
 */
void * __hidden
qlist_del_head(struct qlist_head *list)
{
    struct qlist_head *h;
    bmi_ib_assert(!qlist_empty(list), "%s: empty list %p", __func__, list);
    h = list->next;
    qlist_del(h);
    return h;
}

void * __hidden
qlist_try_del_head(struct qlist_head *list)
{
    struct qlist_head *h;
    if (qlist_empty(list)) return 0;
    h = list->next;
    qlist_del(h);
    return h;
}

/*
 * Debugging printf for sendq and recvq state names.
 */
static const char *
name_lookup(name_t *a, int num)
{
    while (a->num) {
	if (a->num == num)
	    return a->name;
	++a;
    }
    return "(unknown)";
}

const char *
sq_state_name(sq_state_t num)
{
    return name_lookup(sq_state_names, (int) num);
}
const char *
rq_state_name(rq_state_t num)
{
    return name_lookup(rq_state_names, (int) num);
}
const char *
msg_type_name(msg_type_t num)
{
    return name_lookup(msg_type_names, (int) num);
}

/*
 * Walk buflist, copying one way or the other, but no more than len
 * even if buflist could handle it.
 */
void
memcpy_to_buflist(ib_buflist_t *buflist, const void *buf, bmi_size_t len)
{
    int i;
    const char *cp = buf;

    for (i=0; i<buflist->num && len > 0; i++) {
	bmi_size_t bytes = buflist->len[i];
	if (bytes > len)
	    bytes = len;
	memcpy(buflist->buf.recv[i], cp, bytes);
	cp += bytes;
	len -= bytes;
    }
}

void
memcpy_from_buflist(ib_buflist_t *buflist, void *buf)
{
    int i;
    char *cp = buf;

    for (i=0; i<buflist->num; i++) {
	memcpy(cp, buflist->buf.send[i], (size_t) buflist->len[i]);
	cp += buflist->len[i];
    }
}

/*
 * Loop over reading until everything arrives.
 * Like bsend/brecv but without the fcntl messing.
 */
int
read_full(int fd, void *buf, size_t num)
{
    int i, offset = 0;

    while (num > 0) {
	i = read(fd, (char *)buf + offset, num);
	if (i < 0)
	    return i;
	if (i == 0)
	    break;
	num -= i;
	offset += i;
    }
    return offset;
}

/*
 * Keep looping until all bytes have been accepted by the kernel.
 */
int
write_full(int fd, const void *buf, size_t num)
{
    int i, offset = 0;
    int total = num;

    while (num > 0) {
	i = write(fd, (const char *)buf + offset, num);
	if (i < 0)
	    return i;
	num -= i;
	offset += i;
    }
    return total;
}

