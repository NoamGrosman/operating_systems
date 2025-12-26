
#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>

/* Print to stderr exactly: "Bank error: illegal arguments\n" and exit(1). */
void die_illegal_arguments(void);

/* perror with prefix "Bank error: <syscall> failed" and exit(1). */
void die_syscall(const char *syscall_name);

void *xmalloc(size_t n);
void *xcalloc(size_t nmemb, size_t size);

#endif //UTIL_H
