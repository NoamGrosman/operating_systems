#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#include "bank.h"
#include "logger.h"
#include "util.h"

/* -------------------- small time helpers -------------------- */

static long long now_msec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) die_syscall("clock_gettime");
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void sleep_msec(long long ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000LL);
    ts.tv_nsec = (long)((ms % 1000LL) * 1000000LL);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

/* -------------------- VIP priority queue -------------------- */

typedef struct {
    int priority;      /* 1..100, higher first */
    long long seq;     /* FIFO among equal priority */
    int atm_id;
    char *line;        /* original line (heap) */
} vip_task_t;

typedef struct {
    vip_task_t *heap;
    size_t size;
    size_t cap;
    long long next_seq;
    int closed; /* no more tasks will be pushed */
    pthread_mutex_t mtx;
    pthread_cond_t cv;
} vip_queue_t;

static void vipq_init(vip_queue_t *q) {
    memset(q, 0, sizeof(*q));
    q->cap = 64;
    q->heap = (vip_task_t *)xmalloc(q->cap * sizeof(vip_task_t));
    q->size = 0;
    q->next_seq = 1;
    q->closed = 0;
    if (pthread_mutex_init(&q->mtx, NULL) != 0) die_syscall("pthread_mutex_init");
    if (pthread_cond_init(&q->cv, NULL) != 0) die_syscall("pthread_cond_init");
}

static void vipq_destroy(vip_queue_t *q) {
    /* free any remaining tasks */
    for (size_t i = 0; i < q->size; i++) free(q->heap[i].line);
    free(q->heap);
    pthread_cond_destroy(&q->cv);
    pthread_mutex_destroy(&q->mtx);
}

static int vip_task_higher(const vip_task_t *a, const vip_task_t *b) {
    if (a->priority != b->priority) return a->priority > b->priority;
    return a->seq < b->seq; /* smaller seq first */
}

static void vipq_sift_up(vip_queue_t *q, size_t i) {
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (vip_task_higher(&q->heap[p], &q->heap[i])) break;
        vip_task_t tmp = q->heap[p];
        q->heap[p] = q->heap[i];
        q->heap[i] = tmp;
        i = p;
    }
}

static void vipq_sift_down(vip_queue_t *q, size_t i) {
    while (1) {
        size_t l = 2 * i + 1, r = 2 * i + 2, best = i;
        if (l < q->size && vip_task_higher(&q->heap[l], &q->heap[best])) best = l;
        if (r < q->size && vip_task_higher(&q->heap[r], &q->heap[best])) best = r;
        if (best == i) break;
        vip_task_t tmp = q->heap[best];
        q->heap[best] = q->heap[i];
        q->heap[i] = tmp;
        i = best;
    }
}

static void vipq_push(vip_queue_t *q, int atm_id, int priority, const char *line) {
    pthread_mutex_lock(&q->mtx);
    if (q->closed) {
        pthread_mutex_unlock(&q->mtx);
        return;
    }

    if (q->size == q->cap) {
        q->cap *= 2;
        q->heap = (vip_task_t *)realloc(q->heap, q->cap * sizeof(vip_task_t));
        if (!q->heap) die_syscall("realloc");
    }

    vip_task_t t;
    t.priority = priority;
    t.seq = q->next_seq++;
    t.atm_id = atm_id;
    t.line = strdup(line);
    if (!t.line) die_syscall("strdup");

    q->heap[q->size] = t;
    vipq_sift_up(q, q->size);
    q->size++;

    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}

static int vipq_pop(vip_queue_t *q, vip_task_t *out) {
    pthread_mutex_lock(&q->mtx);
    while (q->size == 0 && !q->closed) {
        pthread_cond_wait(&q->cv, &q->mtx);
    }
    if (q->size == 0 && q->closed) {
        pthread_mutex_unlock(&q->mtx);
        return 0; /* done */
    }

    *out = q->heap[0];
    q->size--;
    if (q->size > 0) {
        q->heap[0] = q->heap[q->size];
        vipq_sift_down(q, 0);
    }

    pthread_mutex_unlock(&q->mtx);
    return 1;
}

static void vipq_close(vip_queue_t *q) {
    pthread_mutex_lock(&q->mtx);
    q->closed = 1;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}

/* -------------------- parsing helpers -------------------- */

static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static currency_t parse_currency(const char *s) {
    if (strcmp(s, "ILS") == 0) return CUR_ILS;
    if (strcmp(s, "USD") == 0) return CUR_USD;
    /* per assignment, input is valid; if not, treat as illegal args */
    die_illegal_arguments();
    return CUR_ILS;
}

/* Detect and strip trailing flags:
 * - "PERSISTENT"
 * - "VIP=X" (X 1..100)
 * Returns new ntok (after stripping). */
static int strip_flags(char **tok, int ntok, int *persistent, int *vip_prio) {
    *persistent = 0;
    *vip_prio = 0;

    while (ntok > 0) {
        if (strcmp(tok[ntok - 1], "PERSISTENT") == 0) {
            *persistent = 1;
            ntok--;
            continue;
        }
        if (strncmp(tok[ntok - 1], "VIP=", 4) == 0) {
            int x = atoi(tok[ntok - 1] + 4);
            if (x < 1 || x > 100) die_illegal_arguments();
            *vip_prio = x;
            ntok--;
            continue;
        }
        break;
    }
    return ntok;
}

/* Execute one command line (already tokenized).
 * - base_delay_ms: 1000 for ATM threads, 0 for VIP workers.
 * - persistent: if 1 and first attempt fails, do not log the first error and retry once.
 * Return 0 if line was empty / ignored, 1 otherwise.
 */
static int execute_tokens(bank_t *b, int atm_id, char **tok, int ntok, int persistent, int base_delay_ms) {
    if (ntok == 0) return 0;
    char cmd = tok[0][0];

    long long attempt_start = now_msec();

    /* For PERSISTENT: first failure must not print error. */
    bank_set_thread_log_mode(persistent ? BANK_LOG_SUCCESS_ONLY : BANK_LOG_ALL);

    bank_rc_t rc = BANK_OK;

    if (cmd == 'S') {
        /* S <time_in_msec> */
        int tms = atoi(tok[1]);
        log_line("%d: Currently on a scheduled break. Service will resume within %d ms.", atm_id, tms);
        sleep_msec(tms);
        rc = BANK_OK;
    } else if (cmd == 'O') {
        /* O <account> <password> <initial ILS> <initial USD> */
        int acc = atoi(tok[1]);
        int pass = atoi(tok[2]);
        int ils = atoi(tok[3]);
        int usd = atoi(tok[4]);
        rc = bank_open(b, atm_id, acc, pass, ils, usd);
    } else if (cmd == 'D') {
        /* D <account> <password> <amount> <currency> */
        int acc = atoi(tok[1]);
        int pass = atoi(tok[2]);
        int amount = atoi(tok[3]);
        currency_t cur = parse_currency(tok[4]);
        rc = bank_deposit(b, atm_id, acc, pass, cur, amount);
    } else if (cmd == 'W') {
        /* W <account> <password> <amount> <currency> */
        int acc = atoi(tok[1]);
        int pass = atoi(tok[2]);
        int amount = atoi(tok[3]);
        currency_t cur = parse_currency(tok[4]);
        rc = bank_withdraw(b, atm_id, acc, pass, cur, amount);
    } else if (cmd == 'B') {
        /* B <account> <password> */
        int acc = atoi(tok[1]);
        int pass = atoi(tok[2]);
        int out_ils = 0, out_usd = 0;
        rc = bank_balance(b, atm_id, acc, pass, &out_ils, &out_usd);
    } else if (cmd == 'Q') {
        /* Q <account> <password> */
        int acc = atoi(tok[1]);
        int pass = atoi(tok[2]);
        rc = bank_close(b, atm_id, acc, pass);
    } else if (cmd == 'T') {
        /* T <src> <password> <dst> <amount> <currency> */
        int src = atoi(tok[1]);
        int pass = atoi(tok[2]);
        int dst = atoi(tok[3]);
        int amount = atoi(tok[4]);
        currency_t cur = parse_currency(tok[5]);
        rc = bank_transfer(b, atm_id, src, pass, dst, cur, amount);
    } else if (cmd == 'X') {
        /* X <account> <password> <src currency> to <dst currency> <amount> */
        int acc = atoi(tok[1]);
        int pass = atoi(tok[2]);
        currency_t from_cur = parse_currency(tok[3]);
        /* tok[4] is literal "to" */
        currency_t to_cur = parse_currency(tok[5]);
        int amount = atoi(tok[6]);
        rc = bank_exchange(b, atm_id, acc, pass, from_cur, to_cur, amount);
    } else if (cmd == 'C') {
        /* C <target ATM ID> */
        int target = atoi(tok[1]);
        rc = bank_close_atm_request(b, atm_id, target);
    } else if (cmd == 'R') {
        /* R <iterations> */
        int iters = atoi(tok[1]);
        rc = bank_rollback(b, atm_id, iters);
    } else if (cmd == 'I') {
        /* I <account> <password> <amount> <currency> [is] <time_in_msec>
           We accept both variants: with/without the literal "is". */
        int acc = atoi(tok[1]);
        int pass = atoi(tok[2]);
        int amount = atoi(tok[3]);
        currency_t cur = parse_currency(tok[4]);

        int time_msec = 0;
        if (ntok >= 7 && strcmp(tok[5], "is") == 0) time_msec = atoi(tok[6]);
        else time_msec = atoi(tok[5]);

        rc = bank_invest(b, atm_id, acc, pass, amount, cur, time_msec);
    } else {
        /* unknown command => input invalid */
        die_illegal_arguments();
    }

    /* restore default logging mode */
    bank_set_thread_log_mode(BANK_LOG_ALL);

    /* Persistent retry logic:
       - if first attempt failed: wait remaining time of a normal command (1s) and try once more.
       - do NOT log the first error (we already set SUCCESS_ONLY for first attempt). */
    if (persistent && rc != BANK_OK) {
        long long elapsed = now_msec() - attempt_start;
        long long rem = 1000LL - elapsed;
        if (rem > 0) sleep_msec(rem);

        /* second attempt: log normally (errors allowed) */
        bank_set_thread_log_mode(BANK_LOG_ALL);
        rc = BANK_OK;

        /* Re-execute based on cmd (excluding S which cannot fail anyway) */
        if (cmd == 'O') {
            rc = bank_open(b, atm_id, atoi(tok[1]), atoi(tok[2]), atoi(tok[3]), atoi(tok[4]));
        } else if (cmd == 'D') {
            rc = bank_deposit(b, atm_id, atoi(tok[1]), atoi(tok[2]), parse_currency(tok[4]), atoi(tok[3]));
        } else if (cmd == 'W') {
            rc = bank_withdraw(b, atm_id, atoi(tok[1]), atoi(tok[2]), parse_currency(tok[4]), atoi(tok[3]));
        } else if (cmd == 'B') {
            int out_ils = 0, out_usd = 0;
            rc = bank_balance(b, atm_id, atoi(tok[1]), atoi(tok[2]), &out_ils, &out_usd);
        } else if (cmd == 'Q') {
            rc = bank_close(b, atm_id, atoi(tok[1]), atoi(tok[2]));
        } else if (cmd == 'T') {
            rc = bank_transfer(b, atm_id, atoi(tok[1]), atoi(tok[2]), atoi(tok[3]),
                               parse_currency(tok[5]), atoi(tok[4]));
        } else if (cmd == 'X') {
            rc = bank_exchange(b, atm_id, atoi(tok[1]), atoi(tok[2]),
                               parse_currency(tok[3]), parse_currency(tok[5]), atoi(tok[6]));
        } else if (cmd == 'C') {
            rc = bank_close_atm_request(b, atm_id, atoi(tok[1]));
        } else if (cmd == 'R') {
            rc = bank_rollback(b, atm_id, atoi(tok[1]));
        } else if (cmd == 'I') {
            int time_msec = 0;
            if (ntok >= 7 && strcmp(tok[5], "is") == 0) time_msec = atoi(tok[6]);
            else time_msec = atoi(tok[5]);
            rc = bank_invest(b, atm_id, atoi(tok[1]), atoi(tok[2]), atoi(tok[3]), parse_currency(tok[4]), time_msec);
        }
        bank_set_thread_log_mode(BANK_LOG_ALL);

        /* Note: if second attempt fails, bank.c should log the error. */
    }

    /* ATM delay (VIP workers pass base_delay_ms=0) */
    if (base_delay_ms > 0 && cmd != 'S') {
        long long elapsed = now_msec() - attempt_start;
        long long rem = (long long)base_delay_ms - elapsed;
        if (rem > 0) sleep_msec(rem);
    }

    return 1;
}

/* Tokenize line into tokens[].
 * Modifies line in place, returns ntok. */
static int tokenize(char *line, char **tok, int max_tok) {
    int n = 0;
    char *save = NULL;
    for (char *p = strtok_r(line, " \t\r\n", &save);
         p && n < max_tok;
         p = strtok_r(NULL, " \t\r\n", &save)) {
        tok[n++] = p;
    }
    return n;
}

/* -------------------- threads -------------------- */

typedef struct {
    bank_t *bank;
    vip_queue_t *vipq;
    int atm_id;
    const char *filename;
} atm_arg_t;

typedef struct {
    bank_t *bank;
    vip_queue_t *vipq;
} vip_worker_arg_t;

static void *atm_thread(void *arg) {
    atm_arg_t *a = (atm_arg_t *)arg;

    FILE *fp = fopen(a->filename, "r");
    if (!fp) die_illegal_arguments();

    char *line = NULL;
    size_t cap = 0;

    while (1) {
        if (bank_is_atm_closed(a->bank, a->atm_id)) break;

        ssize_t r = getline(&line, &cap, fp);
        if (r < 0) break;

        rstrip(line);
        if (line[0] == '\0') continue;

        /* tokenize a copy for inspection (flags) */
        char tmp[1024];
        strncpy(tmp, line, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        char *tok[16];
        int ntok = tokenize(tmp, tok, 16);

        int persistent = 0, vip_prio = 0;
        ntok = strip_flags(tok, ntok, &persistent, &vip_prio);
        if (ntok == 0) continue;

        char cmd = tok[0][0];

        long long start = now_msec();

        if (vip_prio > 0) {
            /* enqueue original line (including flags, worker will strip again) */
            vipq_push(a->vipq, a->atm_id, vip_prio, line);

            /* ATM still has its usual pacing */
            if (cmd != 'S') {
                long long elapsed = now_msec() - start;
                long long rem = 1000LL - elapsed;
                if (rem > 0) sleep_msec(rem);
            }
        } else {
            /* execute locally */
            /* tokenize real line for execution (we will strip flags again) */
            char execbuf[1024];
            strncpy(execbuf, line, sizeof(execbuf) - 1);
            execbuf[sizeof(execbuf) - 1] = '\0';

            char *etok[16];
            int entok = tokenize(execbuf, etok, 16);

            int p2 = 0, v2 = 0;
            entok = strip_flags(etok, entok, &p2, &v2);

            /* execute (ATM delay inside) */
            execute_tokens(a->bank, a->atm_id, etok, entok, p2, 1000);
        }

        /* after finishing current command, if bank closed this ATM, stop */
        if (bank_is_atm_closed(a->bank, a->atm_id)) break;
    }

    free(line);
    fclose(fp);
    return NULL;
}

static void *vip_worker_thread(void *arg) {
    vip_worker_arg_t *a = (vip_worker_arg_t *)arg;

    vip_task_t task;
    while (vipq_pop(a->vipq, &task)) {
        /* parse & execute (no ATM delay) */
        char buf[1024];
        strncpy(buf, task.line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *tok[16];
        int ntok = tokenize(buf, tok, 16);

        int persistent = 0, vip_prio = 0;
        ntok = strip_flags(tok, ntok, &persistent, &vip_prio);

        if (ntok > 0) {
            execute_tokens(a->bank, task.atm_id, tok, ntok, persistent, 0);
        }

        free(task.line);
    }

    return NULL;
}

/* -------------------- main -------------------- */

int main(int argc, char **argv)
{
    if (argc < 3) die_illegal_arguments();

    int vip_threads = atoi(argv[1]);
    int atm_count = argc - 2;

    /* verify input files can be opened */
    for (int i = 0; i < atm_count; i++) {
        FILE *fp = fopen(argv[2 + i], "r");
        if (!fp) die_illegal_arguments();
        fclose(fp);
    }

    if (logger_init("log.txt") != 0) die_syscall("fopen");

    bank_t bank;
    if (bank_init(&bank, atm_count) != BANK_OK) die_syscall("bank_init");

    vip_queue_t vipq;
    vipq_init(&vipq);

    pthread_t status_tid, comm_tid;
    if (pthread_create(&status_tid, NULL, bank_status_thread, &bank) != 0) die_syscall("pthread_create");
    if (pthread_create(&comm_tid, NULL, bank_commission_thread, &bank) != 0) die_syscall("pthread_create");

    /* VIP threads */
    pthread_t *vip_tids = NULL;
    vip_worker_arg_t vip_arg;
    vip_arg.bank = &bank;
    vip_arg.vipq = &vipq;

    if (vip_threads > 0) {
        vip_tids = (pthread_t *)xmalloc((size_t)vip_threads * sizeof(pthread_t));
        for (int i = 0; i < vip_threads; i++) {
            if (pthread_create(&vip_tids[i], NULL, vip_worker_thread, &vip_arg) != 0) die_syscall("pthread_create");
        }
    }

    /* ATM threads */
    pthread_t *atm_tids = (pthread_t *)xmalloc((size_t)atm_count * sizeof(pthread_t));
    atm_arg_t *atm_args = (atm_arg_t *)xmalloc((size_t)atm_count * sizeof(atm_arg_t));

    for (int i = 0; i < atm_count; i++) {
        atm_args[i].bank = &bank;
        atm_args[i].vipq = &vipq;
        atm_args[i].atm_id = i + 1;
        atm_args[i].filename = argv[2 + i];
        if (pthread_create(&atm_tids[i], NULL, atm_thread, &atm_args[i]) != 0) die_syscall("pthread_create");
    }

    for (int i = 0; i < atm_count; i++) {
        if (pthread_join(atm_tids[i], NULL) != 0) die_syscall("pthread_join");
    }

    /* No more ATM lines => close VIP queue after it drains */
    vipq_close(&vipq);

    for (int i = 0; i < vip_threads; i++) {
        if (pthread_join(vip_tids[i], NULL) != 0) die_syscall("pthread_join");
    }

    /* Stop background threads and cleanup */
    bank_request_stop(&bank);

    if (pthread_join(status_tid, NULL) != 0) die_syscall("pthread_join");
    if (pthread_join(comm_tid, NULL) != 0) die_syscall("pthread_join");

    free(atm_args);
    free(atm_tids);
    free(vip_tids);

    vipq_destroy(&vipq);

    bank_destroy(&bank);
    logger_close();

    return 0;
}