#ifndef BANK_H
#define BANK_H

#include "account.h"
#include "rwlock.h"
#include <pthread.h>

/* ---------- return codes for bank operations ---------- */
typedef enum {
    BANK_OK = 0,

    BANK_ERR_ACCOUNT_EXISTS,
    BANK_ERR_ACCOUNT_NOT_FOUND,
    BANK_ERR_BAD_PASSWORD,
    BANK_ERR_INSUFFICIENT_FUNDS,
    BANK_ERR_ILLEGAL_AMOUNT,
    BANK_ERR_SAME_ACCOUNT,

    BANK_ERR_ATM_NOT_FOUND,

    BANK_ERR_ATM_ALREADY_CLOSED,
    BANK_ERR_ROLLBACK_NOT_POSSIBLE
} bank_rc_t;
typedef enum {
    BANK_LOG_ALL = 0,          /* log success + errors */
    BANK_LOG_SUCCESS_ONLY = 1, /* log only success, suppress errors */
    BANK_LOG_NONE = 2          /* suppress everything (not used usually) */
} bank_log_mode_t;
void bank_set_thread_log_mode(bank_log_mode_t mode);
typedef struct {
    int id;
    int password;
    int bal_ils;
    int bal_usd;
} bank_acc_snapshot_t;

/* Forward declaration for rollback request queue (opaque to users). */
typedef struct rollback_req rollback_req_t;

typedef struct {
    int bank_ils;
    int bank_usd;

    int acc_count;
    bank_acc_snapshot_t *accs;

    int atm_count;
    int *atm_closed;
} bank_snapshot_t;
/* A single account entry in the bank's array/map */
typedef struct {
    account_t *acc;   /* NULL means empty slot */
} bank_entry_t;

/* Opaque-ish bank object (we expose the struct for now to simplify C work) */
typedef struct bank {
    /* accounts container */
    bank_entry_t *entries;
    int capacity;     /* allocated slots */
    int count;        /* number of live accounts */

    /* lock protecting the accounts container itself (not per-account data) */
    rwlock_t accounts_lock;

    /* bank’s own balance from commissions (optional at this stage) */
    int bank_ils;
    int bank_usd;
    pthread_mutex_t bank_money_mtx;

    /* ATM close bookkeeping */
    int atm_count;
    int *atm_closed;  /* array[atm_count], 0/1 */
    int *atm_close_req; /* array[atm_count], 0=no request, else = source ATM id */
    pthread_mutex_t atm_mtx;

    /* status/commission threads stop flag */
    int stop;
    pthread_mutex_t stop_mtx;

    /* ---------- rollback snapshots (captured by status thread) ---------- */
    bank_snapshot_t *snapshots; /* ring buffer of snapshots */
    int snap_capacity;          /* typically 120 */
    int snap_head;              /* next insert index */
    int snap_count;             /* number of valid snapshots in ring */
    pthread_mutex_t snap_mtx;

    /* ---------- rollback requests (enqueued by ATMs, processed by status thread) ---------- */
    rollback_req_t *rb_head;
    rollback_req_t *rb_tail;
    pthread_mutex_t rb_mtx;

} bank_t;

typedef struct {
    int id;
    int password;
    int ils;
    int usd;
} acc_status_t;

/* ---------- lifecycle ---------- */

/* Initialize bank internals (alloc arrays, init locks, set atm_count). */
bank_rc_t bank_init(bank_t *b, int atm_count);

/* Free everything: destroy accounts, destroy locks, free arrays. */
void bank_destroy(bank_t *b);

/* Set stop flag to ask background threads to exit (status/commission). */
void bank_request_stop(bank_t *b);

/* Read stop flag (thread-safe). */
int bank_should_stop(bank_t *b);


/* ---------- core operations (called by ATM/VIP workers) ---------- */

/* O: Open account */
bank_rc_t bank_open(bank_t *b, int atm_id, int acc_id, int password,
                    int init_ils, int init_usd);

/* Q: Close account (and remove it from bank) */
bank_rc_t bank_close(bank_t *b, int atm_id, int acc_id, int password);

/* D: Deposit */
bank_rc_t bank_deposit(bank_t *b, int atm_id, int acc_id, int password,
                       currency_t cur, int amount);

/* W: Withdraw */
bank_rc_t bank_withdraw(bank_t *b, int atm_id, int acc_id, int password,
                        currency_t cur, int amount);

/* B: Balance inquiry (returns balances via out params) */
bank_rc_t bank_balance(bank_t *b, int atm_id, int acc_id, int password,
                       int *out_ils, int *out_usd);

/* T: Transfer from src->dst in given currency */
bank_rc_t bank_transfer(bank_t *b, int atm_id, int src_id, int password,
                        int dst_id, currency_t cur, int amount);

/* X: Exchange between currencies (fixed rate handled in bank.c) */
bank_rc_t bank_exchange(bank_t *b, int atm_id, int acc_id, int password,
                        currency_t from_cur, currency_t to_cur, int amount_from);

/* Rollback to snapshot taken by status thread */
bank_rc_t bank_rollback(bank_t *b, int atm_id, int iterations_back);

/* Investment */
bank_rc_t bank_invest(bank_t *b, int atm_id, int acc_id, int password,
                      int amount, currency_t cur, int time_msec);
/* ---------- ATM close requests (handled later by the status thread) ---------- */

/* Request to close an ATM (mark target ATM as closed). */
bank_rc_t bank_close_atm_request(bank_t *b, int atm_id_src, int atm_id_target);

/* Check if an ATM is closed (used by ATM thread before executing commands). */
int bank_is_atm_closed(bank_t *b, int atm_id);


/* ---------- background thread routines (you’ll implement later) ---------- */

/* Prints bank status snapshot periodically (0.5s). */
void *bank_status_thread(void *arg);

/* Charges commissions periodically (3s). */
void *bank_commission_thread(void *arg);


#endif //BANK_H
