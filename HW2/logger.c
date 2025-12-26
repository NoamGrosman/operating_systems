
#include "logger.h"
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include "logger.h"

static FILE *g_log = NULL;
static  pthread_mutex_t g_log_mtx= PTHREAD_MUTEX_INITIALIZER;
int  logger_init(const char *filename){
    g_log = fopen(filename,"w");
    if (!g_log){
        return -1;
    }
    setvbuf(g_log, NULL, _IONBF, 0); // unbuffered
    return 0;
}

void log_line(const char *fmt, ...){
    if (g_log == NULL){
        return;
    }
    pthread_mutex_lock(&g_log_mtx);
    va_list ap;
    va_start(ap,fmt);
    vfprintf(g_log,fmt, ap);
    va_end(ap);
    fputc('\n',g_log);
    pthread_mutex_unlock(&g_log_mtx);
}
void logger_close(void){
    pthread_mutex_lock(&g_log_mtx);
    if(g_log) {
        fclose(g_log);
        g_log=NULL;
    }
    pthread_mutex_unlock(&g_log_mtx);

}
