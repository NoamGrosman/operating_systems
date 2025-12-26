#include "util.h"
#include <stdio.h>
#include <stdlib.h>

void die_illegal_arguments(void) {
    fprintf(stderr, "Bank error: illegal arguments\n");
    exit(1);
}

void die_syscall(const char *syscall_name) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Bank error: %s failed"
             ,syscall_name);
    perror(msg);
    exit(1);
}

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die_syscall("malloc");
    return p;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p) die_syscall("calloc");
    return p;
}
