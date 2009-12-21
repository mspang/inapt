#ifndef CEO_UTIL_H
#define CEO_UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#define PRINTF_LIKE(extra) __attribute__((format(printf, extra+1, extra+2)))
#else
#define NORETURN
#define PRINTF_LIKE(extra)
#endif

PRINTF_LIKE(0) NORETURN void fatal(const char *, ...);
PRINTF_LIKE(0) NORETURN void fatalpe(const char *, ...);
PRINTF_LIKE(0) NORETURN void badconf(const char *, ...);
PRINTF_LIKE(0) NORETURN void deny(const char *, ...);
PRINTF_LIKE(0) void error(const char *, ...);
PRINTF_LIKE(0) void warn(const char *, ...);
PRINTF_LIKE(0) void notice(const char *, ...);
PRINTF_LIKE(0) void debug(const char *, ...);
PRINTF_LIKE(0) void errorpe(const char *, ...);
PRINTF_LIKE(0) void warnpe(const char *, ...);

static inline void *xmalloc(size_t size) {
    void *alloc = malloc(size);

    if (alloc == NULL)
        fatal("out of memory");

    return alloc;
}

static inline void *xrealloc(void *ptr, size_t size) {
    void *alloc = realloc(ptr, size);

    if (alloc == NULL)
        fatal("out of memory");

    return alloc;
}

static inline void *xcalloc(size_t nmemb, size_t size) {
    void *alloc = calloc(nmemb, size);

    if (alloc == NULL)
        fatal("out of memory");

    return alloc;
}

static inline char *xstrdup(const char *s) {
    char *dup = strdup(s);

    if (dup == NULL)
        fatal("out of memory");

    return dup;
}

static inline char *xstrndup(const char *s, size_t n) {
    char *dup = strndup(s, n);

    if (dup == NULL)
        fatal("out of memory");

    return dup;
}

extern bool debug_enabled;

#endif
