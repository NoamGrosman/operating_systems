
#ifndef UNTITLED_RWLOCK_H
#define UNTITLED_RWLOCK_H

#include <pthread.h>
typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  can_read;
    pthread_cond_t  can_write;

    int readers;          /* active readers count */
    int writer_active;    /* 0/1 */
    int writers_waiting;  /* waiting writers count (to prefer writers) */
} rwlock_t;

/* Initializes the lock fields. */
void rwlock_init(rwlock_t *l);

/* Destroys mutex/conds. Call only when nobody holds the lock. */
void rwlock_destroy(rwlock_t *l);

/* Reader lock:
 * Multiple readers may enter together if no writer is active.
 * If a writer is waiting, prefer writers (block new readers).
 */
void rwlock_rdlock(rwlock_t *l);

/* Releases reader lock. If last reader leaves, wake one writer. */
void rwlock_rdunlock(rwlock_t *l);

/* Writer lock:
 * Exclusive access. Wait until no readers and no active writer.
 */
void rwlock_wrlock(rwlock_t *l);

/* Releases writer lock. Prefer waking a writer if waiting,
 * otherwise wake all readers.
 */
void rwlock_wrunlock(rwlock_t *l);
#endif //UNTITLED_RWLOCK_H
