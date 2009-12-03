#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>

#include "util.h"

static void errmsg(int prio, const char *prefix, const char *fmt, va_list args) {
    fprintf(stderr, "%s: ", prefix);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

static void errmsgpe(int prio, const char *prefix, const char *fmt, va_list args) {
    fprintf(stderr, "%s: ", prefix);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, ": %s\n", strerror(errno));
}

NORETURN static void die(int prio, const char *prefix, const char *msg, va_list args) {
    errmsg(prio, prefix, msg, args);
    exit(1);
}

NORETURN static void diepe(int prio, const char *prefix, const char *msg, va_list args) {
    errmsgpe(prio, prefix, msg, args);
    exit(1);
}

NORETURN void fatal(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    die(LOG_CRIT, "fatal", msg, args);
    va_end(args);
}

void error(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    errmsg(LOG_ERR, "error", msg, args);
    va_end(args);
}

void warn(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    errmsg(LOG_WARNING, "warning", msg, args);
    va_end(args);
}

void notice(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    errmsg(LOG_NOTICE, "notice", msg, args);
    va_end(args);
}

void debug(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    errmsg(LOG_DEBUG, "debug", msg, args);
    va_end(args);
}

NORETURN void deny(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    die(LOG_ERR, "denied", msg, args);
    va_end(args);
}

NORETURN void badconf(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    die(LOG_CRIT, "configuration error", msg, args);
    va_end(args);
}

NORETURN void fatalpe(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    diepe(LOG_CRIT, "fatal", msg, args);
    va_end(args);
}

void errorpe(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    errmsgpe(LOG_ERR, "error", msg, args);
    va_end(args);
}

void warnpe(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    errmsgpe(LOG_WARNING, "warning", msg, args);
    va_end(args);
}
