#include "rwlock.h"
#include <pthread.h>
#include <stddef.h>
#include "util.h"
#include <errno.h>

static void die_pthread(const char *name, int rc);
void rwlock_init(rwlock_t *l){
    int c1 = pthread_mutex_init(&l->mtx,NULL);
    if (c1)die_pthread("pthread_mutex_init",c1);
    c1 = pthread_cond_init(&l->can_read,NULL);
    if (c1)die_pthread("pthread_cond_init",c1);
    c1 = pthread_cond_init(&l->can_write,NULL);
    if (c1)die_pthread("pthread_cond_init",c1);
    l->readers=0;
    l->writer_active=0;
    l->writers_waiting=0;

}
void rwlock_destroy(rwlock_t *l){
   int rc =pthread_cond_destroy(&l->can_read);
   if (rc)die_pthread("pthread_cond_destroy",rc);
   rc = pthread_cond_destroy(&l->can_write);
    if (rc)die_pthread("pthread_cond_destroy",rc);
    rc = pthread_mutex_destroy(&l->mtx);
    if (rc)die_pthread("pthread_mutex_destroy",rc);
}

void rwlock_rdlock(rwlock_t *l){
    int rc = pthread_mutex_lock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_lock",rc);
    while (l->writer_active || (l->writers_waiting > 0)){
       rc =  pthread_cond_wait(&l->can_read,&l->mtx);
        if (rc)die_pthread("pthread_cond_wait",rc);

    }
    ++l->readers;
   rc =  pthread_mutex_unlock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_unlock",rc);
}

void rwlock_rdunlock(rwlock_t *l){
    int rc = pthread_mutex_lock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_lock",rc);
    --l->readers;
    if(!(l->readers)){
        rc = pthread_cond_signal(&l->can_write);
        if (rc)die_pthread("pthread_cond_signal",rc);

    }
    rc =  pthread_mutex_unlock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_unlock",rc);
}

void rwlock_wrlock(rwlock_t *l){
    int rc = pthread_mutex_lock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_lock",rc);
    ++l->writers_waiting;
    while(l->writer_active || (l->readers>0)){
       rc =  pthread_cond_wait(&l->can_write,&l->mtx);
        if (rc)die_pthread("pthread_cond_wait",rc);
    }
    --l->writers_waiting;
    l->writer_active = 1;
    rc =  pthread_mutex_unlock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_unlock",rc);
}
void rwlock_wrunlock(rwlock_t *l){
    int rc = pthread_mutex_lock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_lock",rc);
    l->writer_active = 0;
    if(l->writers_waiting > 0 ){
        rc = pthread_cond_signal(&l->can_write);
        if (rc)die_pthread("pthread_cond_signal",rc);

    }else{
       rc = pthread_cond_broadcast(&l->can_read);
        if (rc)die_pthread("pthread_cond_broadcast",rc);

    }
    rc =  pthread_mutex_unlock(&l->mtx);
    if (rc)die_pthread("pthread_mutex_unlock",rc);
}

static void die_pthread(const char *name, int rc) {
    errno = rc;
    die_syscall(name);
}