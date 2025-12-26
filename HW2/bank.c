#include "bank.h"
#include <stdlib.h>
#include "rwlock.h"
#include "logger.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include <string.h>
static bank_rc_t bank_lock_account(bank_t *b, int acc_id, int write_lock, account_t **out);
static void bank_unlock_account(account_t *acc, int write_lock);
static __thread bank_log_mode_t g_tls_log_mode = BANK_LOG_ALL;
static int bank_insert_account(bank_t *b, account_t *acc);
void bank_set_thread_log_mode(bank_log_mode_t mode) {
    g_tls_log_mode = mode;
}
static unsigned int xorshift32(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}
struct rollback_req {
    int atm_id;
    int iterations_back;
    struct rollback_req *next;
};

static void snapshot_free(bank_snapshot_t *s)
{
    if (!s) return;
    free(s->accs);
    s->accs = NULL;
    s->acc_count = 0;
}

/* Capture a snapshot of the bank state. Caller provides the already-sorted accounts array. */
static void snapshot_capture(bank_t *b, const acc_status_t *arr, int n)
{
    bank_snapshot_t snap;
    snap.acc_count = n;
    snap.accs = NULL;
    snap.atm_count = b->atm_count;
    snap.atm_closed = NULL; /* we don't rollback ATM-closed state */

    pthread_mutex_lock(&b->bank_money_mtx);
    snap.bank_ils = b->bank_ils;
    snap.bank_usd = b->bank_usd;
    pthread_mutex_unlock(&b->bank_money_mtx);

    if (n > 0) {
        snap.accs = calloc((size_t)n, sizeof(bank_acc_snapshot_t));
        if (!snap.accs) return; /* best-effort: skip snapshot on OOM */
        for (int i = 0; i < n; i++) {
            snap.accs[i].id = arr[i].id;
            snap.accs[i].password = arr[i].password;
            snap.accs[i].bal_ils = arr[i].ils;
            snap.accs[i].bal_usd = arr[i].usd;
        }
    }

    pthread_mutex_lock(&b->snap_mtx);
    const int idx = b->snap_head;
    snapshot_free(&b->snapshots[idx]);
    b->snapshots[idx] = snap;
    b->snap_head = (b->snap_head + 1) % b->snap_capacity;
    if (b->snap_count < b->snap_capacity) b->snap_count++;
    pthread_mutex_unlock(&b->snap_mtx);
}
static void snapshot_apply(bank_t *b, const bank_snapshot_t *snap){
    rwlock_wrlock(&b->accounts_lock);
    for (int i = 0; i < b->capacity; i++) {
        if (b->entries[i].acc) {
            account_destroy(b->entries[i].acc);
            b->entries[i].acc = NULL;
        }
    }
    b->count = 0;
    if (snap->acc_count > b->capacity) {
        int new_cap = b->capacity;
        while (new_cap < snap->acc_count) new_cap *= 2;
        bank_entry_t *new_entries = realloc(b->entries, (size_t)new_cap * sizeof(bank_entry_t));
        if (new_entries) {
            b->entries = new_entries;
            for (int i = b->capacity; i < new_cap; i++) b->entries[i].acc = NULL;
            b->capacity = new_cap;
        }
    }
    int inserted = 0;
    for (int i = 0; i < snap->acc_count; i++) {
        if (inserted >= b->capacity) break;
        account_t *acc = account_create(snap->accs[i].id, snap->accs[i].password,
                                        snap->accs[i].bal_ils, snap->accs[i].bal_usd);
        if (!acc) continue;
        for (int j = 0; j < b->capacity; j++) {
            if (!b->entries[j].acc) {
                b->entries[j].acc = acc;
                b->count++;
                inserted++;
                break;
            }
        }
    }
    rwlock_wrunlock(&b->accounts_lock);
    pthread_mutex_lock(&b->bank_money_mtx);
    b->bank_ils = snap->bank_ils;
    b->bank_usd = snap->bank_usd;
    pthread_mutex_unlock(&b->bank_money_mtx);
}
static void bank_process_rollbacks(bank_t *b){
    while (1) {
        pthread_mutex_lock(&b->rb_mtx);
        rollback_req_t *req = b->rb_head;
        if (!req) {
            pthread_mutex_unlock(&b->rb_mtx);
            break;
        }
        b->rb_head = req->next;
        if (!b->rb_head) b->rb_tail = NULL;
        pthread_mutex_unlock(&b->rb_mtx);
        bank_snapshot_t copy;
        memset(&copy, 0, sizeof(copy));
        pthread_mutex_lock(&b->snap_mtx);
        if (req->iterations_back <= 0 || req->iterations_back >= b->snap_count) {
            pthread_mutex_unlock(&b->snap_mtx);
            free(req);
            continue;
        }
        int idx = (b->snap_head - 1 - req->iterations_back) % b->snap_capacity;
        if (idx < 0) idx += b->snap_capacity;
        bank_snapshot_t *src = &b->snapshots[idx];
        copy.bank_ils = src->bank_ils;
        copy.bank_usd = src->bank_usd;
        copy.acc_count = src->acc_count;
        if (src->acc_count > 0 && src->accs) {
            copy.accs = calloc((size_t)src->acc_count, sizeof(bank_acc_snapshot_t));
            if (copy.accs) {
                memcpy(copy.accs, src->accs, (size_t)src->acc_count * sizeof(bank_acc_snapshot_t));
            } else {
                copy.acc_count = 0;
            }
        }
        pthread_mutex_unlock(&b->snap_mtx);

        snapshot_apply(b, &copy);
        snapshot_free(&copy);

        log_line("%d: Rollback to %d bank iterations ago was completed successfully",
                 req->atm_id, req->iterations_back);
        free(req);
    }
}

static const char* cur_str(currency_t cur) {
    return (cur == CUR_ILS) ? "ILS" : "USD";
}
static void sleep_msec(long ms){
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}
void bank_request_stop(bank_t *b)
{
    pthread_mutex_lock(&b->stop_mtx);
    b->stop = 1;
    pthread_mutex_unlock(&b->stop_mtx);
}
int bank_should_stop(bank_t *b)
{
    pthread_mutex_lock(&b->stop_mtx);
    int s = b->stop;
    pthread_mutex_unlock(&b->stop_mtx);
    return s;
}
bank_rc_t bank_init(bank_t *b, int atm_count){
    if (!b || atm_count <= 0) return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    b->count = 0;
    b->stop = 0;
    b->bank_usd = 0;
    b->bank_ils = 0;
    b->capacity = 128;
    b->atm_count = atm_count;
    if(pthread_mutex_init(&b->atm_mtx,NULL))return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    if(pthread_mutex_init(&b->bank_money_mtx,NULL))return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    if(pthread_mutex_init(&b->stop_mtx,NULL))return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    b-> entries =calloc(b->capacity, sizeof(bank_entry_t));
    if (!b->entries) return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    b->atm_closed = calloc(atm_count+1, sizeof(int));
    b->atm_close_req = calloc((size_t)atm_count + 1, sizeof(int));
    if (!b->atm_closed ||  !b->atm_close_req) {
        free(b->entries);
        b->entries = NULL;
        return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    }
    b->snap_capacity = 120;
    b->snap_head = 0;
    b->snap_count = 0;
    b->snapshots = calloc((size_t)b->snap_capacity, sizeof(bank_snapshot_t));
    if (!b->snapshots) {
        free(b->atm_close_req);
        free(b->atm_closed);
        free(b->entries);
        b->entries = NULL;
        b->atm_closed = NULL;
        b->atm_close_req = NULL;
        return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    }
    if (pthread_mutex_init(&b->snap_mtx, NULL) != 0) return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    b->rb_head = NULL;
    b->rb_tail = NULL;
    if (pthread_mutex_init(&b->rb_mtx, NULL) != 0) return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    rwlock_init(&b->accounts_lock);
    return BANK_OK;
}

void bank_destroy(bank_t *b){
    if(!b)return;
    rwlock_wrlock(&b->accounts_lock);
    for (int i = 0; i < b->capacity; i++) {
        if (b->entries && b->entries[i].acc) {
            account_destroy(b->entries[i].acc);
            b->entries[i].acc = NULL;
        }
    }
    b->count = 0;
    rwlock_wrunlock(&b->accounts_lock);
    if (b->snapshots) {
        for (int i = 0; i < b->snap_capacity; i++) {
            snapshot_free(&b->snapshots[i]);
        }
    }
    pthread_mutex_lock(&b->rb_mtx);
    rollback_req_t *cur = b->rb_head;
    b->rb_head = b->rb_tail = NULL;
    pthread_mutex_unlock(&b->rb_mtx);
    while (cur) {
        rollback_req_t *nxt = cur->next;
        free(cur);
        cur = nxt;
    }
    rwlock_destroy(&b->accounts_lock);
    pthread_mutex_destroy(&b->rb_mtx);
    pthread_mutex_destroy(&b->snap_mtx);
    pthread_mutex_destroy(&b->atm_mtx);
    pthread_mutex_destroy(&b->bank_money_mtx);
    pthread_mutex_destroy(&b->stop_mtx);
    free(b->snapshots);
    free(b->atm_close_req);
    free(b->entries);
    free(b->atm_closed);
    b->entries = NULL;
    b->atm_closed = NULL;
    b->atm_close_req = NULL;
    b->snapshots = NULL;
}

static bank_rc_t bank_lock_account(bank_t *b, int acc_id, int write_lock, account_t **out) {
    rwlock_rdlock(&b->accounts_lock);
    account_t *acc = NULL;
    for (int i = 0; i < b->capacity; ++i) {
        if (b->entries[i].acc && b->entries[i].acc->id == acc_id) {
            acc = b->entries[i].acc;
            break;
        }
    }
    if (!acc) {
        rwlock_rdunlock(&b->accounts_lock);
        return BANK_ERR_ACCOUNT_NOT_FOUND;
    }
    if (write_lock) rwlock_wrlock(&acc->lock);
    else            rwlock_rdlock(&acc->lock);
    rwlock_rdunlock(&b->accounts_lock);
    *out = acc;
    return BANK_OK;
}
static void bank_unlock_account(account_t *acc, int write_lock) {
    if (!acc) return;
    if (write_lock) rwlock_wrunlock(&acc->lock);
    else            rwlock_rdunlock(&acc->lock);
}
static int bank_insert_account(bank_t *b, account_t *acc){
    rwlock_wrlock(&b->accounts_lock);
    for (int i = 0; i < b->capacity; ++i) {
        if(b->entries[i].acc && (b->entries[i].acc->id == acc->id)){
            rwlock_wrunlock(&b->accounts_lock);
            return -1;
        }
    }
    for (int i = 0; i < b->capacity; ++i) {
        if(!b->entries[i].acc ){
            b->entries[i].acc=acc;
            ++b->count;
            rwlock_wrunlock(&b->accounts_lock);
            return 0;
        }
    }
    int old_cap = b->capacity;
    int new_cap = old_cap * 2;
    bank_entry_t *new_entries =
            realloc(b->entries, (size_t)new_cap * sizeof(bank_entry_t));
    if (!new_entries) {
        rwlock_wrunlock(&b->accounts_lock);
        return -1;
    }
    b->entries = new_entries;
    for (int i = old_cap; i < new_cap; ++i) {
        b->entries[i].acc = NULL;
    }
    b->capacity = new_cap;
    b->entries[old_cap].acc = acc;
    b->count++;
    rwlock_wrunlock(&b->accounts_lock);
    return 0;
}
bank_rc_t bank_open(bank_t *b, int atm_id, int acc_id, int password,
                    int init_ils, int init_usd){
    if ((init_usd <0) || (init_ils<0))return BANK_ERR_ILLEGAL_AMOUNT;
    account_t *acc = account_create(acc_id,password,init_ils,init_usd);
    if(!acc)return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    if(bank_insert_account(b,acc)==-1){
        account_destroy(acc);
        log_line("Error %d: Your transaction failed – account with the same id exists",
                 atm_id);
        return BANK_ERR_ACCOUNT_EXISTS;
    }
    log_line("%d: New account id is %d with password %d and initial balance %d ILS and %d USD",
             atm_id, acc_id, password, init_ils, init_usd);
    return BANK_OK;
}
bank_rc_t bank_deposit(bank_t *b, int atm_id, int acc_id, int password,
                       currency_t cur, int amount){
    if (amount<=0)return BANK_ERR_ILLEGAL_AMOUNT;
    account_t *acc = NULL;
    bank_rc_t rc = bank_lock_account(b, acc_id, 1, &acc);
    if (rc != BANK_OK) {
        log_line("Error %d: Your transaction failed – account id %d does not exist", atm_id, acc_id);
        return rc;
    }
    if (acc->password != password){
        bank_unlock_account(acc, 1);
        log_line("Error %d: Your transaction failed – password for account id %d is incorrect",
                 atm_id, acc_id);
        return  BANK_ERR_BAD_PASSWORD;
    }
    account_add(acc,cur,amount);
    int bal_ils = account_get_balance(acc, CUR_ILS);
    int bal_usd = account_get_balance(acc, CUR_USD);
    bank_unlock_account(acc, 1);
    log_line("%d: Account %d new balance is %d ILS and %d USD after %d %s was deposited",
             atm_id, acc_id, bal_ils, bal_usd, amount, cur_str(cur));
    return BANK_OK;
}
bank_rc_t bank_withdraw(bank_t *b, int atm_id, int acc_id, int password,
                        currency_t cur, int amount){
    if (amount<=0)return BANK_ERR_ILLEGAL_AMOUNT;
    account_t *acc = NULL;
    bank_rc_t rc = bank_lock_account(b, acc_id, 1, &acc);
    if (rc != BANK_OK) {
        log_line("Error %d: Your transaction failed – account id %d does not exist", atm_id, acc_id);
        return rc;
    }
    if (acc->password != password){
        bank_unlock_account(acc, 1);
        log_line("Error %d: Your transaction failed – password for account id %d is incorrect",
                 atm_id, acc_id);
        return  BANK_ERR_BAD_PASSWORD;
    }
    int check = account_sub(acc,cur,amount);
    int bal_ils = account_get_balance(acc, CUR_ILS);
    int bal_usd = account_get_balance(acc, CUR_USD);
    if(check == -1){
        bank_unlock_account(acc, 1);
        log_line("Error %d: Your transaction failed – account id %d balance is %d ILS and %d USD is lower than %d %s",
                 atm_id, acc_id, bal_ils, bal_usd, amount, cur_str(cur));
        return BANK_ERR_INSUFFICIENT_FUNDS;
    }
    bank_unlock_account(acc, 1);
    log_line("%d: Account %d new balance is %d ILS and %d USD after %d %s was withdrawn",
             atm_id, acc_id, bal_ils, bal_usd, amount, cur_str(cur));
    return BANK_OK;
}
bank_rc_t bank_balance(bank_t *b, int atm_id, int acc_id, int password,
                       int *out_ils, int *out_usd){
    account_t *acc = NULL;
    bank_rc_t rc = bank_lock_account(b, acc_id, 0, &acc);
    if (rc != BANK_OK) {
        log_line("Error %d: Your transaction failed – account id %d does not exist", atm_id, acc_id);
        return rc;
    }
    if (acc->password != password) {
        bank_unlock_account(acc, 0);
        log_line("Error %d: Your transaction failed – password for account id %d is incorrect",
                 atm_id, acc_id);
        return BANK_ERR_BAD_PASSWORD;
    }
    int bal_ils = account_get_balance(acc, CUR_ILS);
    int bal_usd = account_get_balance(acc, CUR_USD);
    if (out_ils) *out_ils = bal_ils;
    if (out_usd) *out_usd = bal_usd;
    bank_unlock_account(acc, 0);
    log_line("%d: Account %d balance is %d ILS and %d USD",
             atm_id, acc_id, bal_ils, bal_usd);
    return BANK_OK;
}
bank_rc_t bank_close(bank_t *b, int atm_id, int acc_id, int password) {
    rwlock_wrlock(&b->accounts_lock);
    int idx = -1;
    for (int i = 0; i < b->capacity; i++) {
        if (b->entries[i].acc && b->entries[i].acc->id == acc_id) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        rwlock_wrunlock(&b->accounts_lock);
        log_line("Error %d: Your transaction failed – account id %d does not exist",
                 atm_id, acc_id);
        return BANK_ERR_ACCOUNT_NOT_FOUND;
    }
    account_t *acc = b->entries[idx].acc;
    rwlock_wrlock(&acc->lock);
    if (acc->password != password) {
        rwlock_wrunlock(&acc->lock);
        rwlock_wrunlock(&b->accounts_lock);
        log_line("Error %d: Your transaction failed – password for account id %d is incorrect",
                 atm_id, acc_id);
        return BANK_ERR_BAD_PASSWORD;
    }
    int bal_ils = account_get_balance(acc, CUR_ILS);
    int bal_usd = account_get_balance(acc, CUR_USD);
    b->entries[idx].acc = NULL;
    b->count--;
    rwlock_wrunlock(&b->accounts_lock);
    rwlock_wrunlock(&acc->lock);
    account_destroy(acc);
    log_line("%d: Account %d is now closed. Balance was %d ILS and %d USD",
             atm_id, acc_id, bal_ils, bal_usd);
    return BANK_OK;
}
bank_rc_t bank_transfer(bank_t *b, int atm_id, int src_id, int password,
                        int dst_id, currency_t cur, int amount){
    if (amount <= 0) return BANK_ERR_ILLEGAL_AMOUNT;
    if (src_id == dst_id) return BANK_ERR_SAME_ACCOUNT;
    rwlock_rdlock(&b->accounts_lock);
    account_t *src = NULL, *dst = NULL;
    for (int i = 0; i < b->capacity; i++) {
        if (b->entries[i].acc && b->entries[i].acc->id == src_id) src = b->entries[i].acc;
        if (b->entries[i].acc && b->entries[i].acc->id == dst_id) dst = b->entries[i].acc;
    }
    if (!src) {
        rwlock_rdunlock(&b->accounts_lock);
        log_line("Error %d: Your transaction failed – account id %d does not exist", atm_id, src_id);
        return BANK_ERR_ACCOUNT_NOT_FOUND;
    }
    if (!dst) {
        rwlock_rdunlock(&b->accounts_lock);
        log_line("Error %d: Your transaction failed – account id %d does not exist", atm_id, dst_id);
        return BANK_ERR_ACCOUNT_NOT_FOUND;
    }
    account_t *first  = (src_id < dst_id) ? src : dst;
    account_t *second = (src_id < dst_id) ? dst : src;
    rwlock_wrlock(&first->lock);
    rwlock_wrlock(&second->lock);
    rwlock_rdunlock(&b->accounts_lock);
    if (src->password != password) {
        rwlock_wrunlock(&second->lock);
        rwlock_wrunlock(&first->lock);
        log_line("Error %d: Your transaction failed – password for account id %d is incorrect",
                 atm_id, src_id);
        return BANK_ERR_BAD_PASSWORD;
    }
    if (account_sub(src, cur, amount) == -1) {
        rwlock_wrunlock(&second->lock);
        rwlock_wrunlock(&first->lock);
        log_line("Error %d: Your transaction failed – balance of account id %d is lower than %d %s",
                 atm_id, src_id, amount, cur_str(cur));
        return BANK_ERR_INSUFFICIENT_FUNDS;
    }
    account_add(dst, cur, amount);
    int src_ils = account_get_balance(src, CUR_ILS);
    int src_usd = account_get_balance(src, CUR_USD);
    int dst_ils = account_get_balance(dst, CUR_ILS);
    int dst_usd = account_get_balance(dst, CUR_USD);
    rwlock_wrunlock(&second->lock);
    rwlock_wrunlock(&first->lock);
    log_line("%d: Transfer %d %s from account %d to account %d new account balance is %d ILS and %d USD new target account balance is %d ILS and %d USD",
             atm_id, amount, cur_str(cur), src_id, dst_id,
             src_ils, src_usd, dst_ils, dst_usd);

    return BANK_OK;
}

bank_rc_t bank_exchange(bank_t *b, int atm_id, int acc_id, int password,
                        currency_t from_cur, currency_t to_cur, int amount_from){
    if (amount_from <= 0) return BANK_ERR_ILLEGAL_AMOUNT;
    if (from_cur == to_cur) return BANK_OK;
    account_t *acc = NULL;
    bank_rc_t rc = bank_lock_account(b, acc_id, 1, &acc);
    if (rc != BANK_OK) {
        log_line("Error %d: Your transaction failed – account id %d does not exist", atm_id, acc_id);
        return rc;
    }
    if (acc->password != password) {
        bank_unlock_account(acc, 1);
        log_line("Error %d: Your transaction failed – password for account id %d is incorrect",
                 atm_id, acc_id);
        return BANK_ERR_BAD_PASSWORD;
    }
    int bal_ils = account_get_balance(acc, CUR_ILS);
    int bal_usd = account_get_balance(acc, CUR_USD);
    if (account_sub(acc, from_cur, amount_from) == -1) {
        bank_unlock_account(acc, 1);
        log_line("Error %d: Your transaction failed – account id %d balance is %d ILS and %d USD is lower than %d %s",
                 atm_id, acc_id, bal_ils, bal_usd, amount_from, cur_str(from_cur));
        return BANK_ERR_INSUFFICIENT_FUNDS;
    }
    int amount_to = amount_from;
    if (from_cur == CUR_USD && to_cur == CUR_ILS) amount_to = amount_from * 5;
    else if (from_cur == CUR_ILS && to_cur == CUR_USD) amount_to = amount_from / 5;
    account_add(acc, to_cur, amount_to);
    bal_ils = account_get_balance(acc, CUR_ILS);
    bal_usd = account_get_balance(acc, CUR_USD);
    bank_unlock_account(acc, 1);
    log_line("%d: Account %d new balance is %d ILS and %d USD after %d %s was exchanged",
             atm_id, acc_id, bal_ils, bal_usd, amount_from, cur_str(from_cur));

    return BANK_OK;
}
bank_rc_t bank_close_atm_request(bank_t *b, int atm_id_src, int atm_id_target){
    pthread_mutex_lock(&b->atm_mtx);
    if (atm_id_target < 1 || atm_id_target > b->atm_count) {
        pthread_mutex_unlock(&b->atm_mtx);
        log_line("Error %d: Your transaction failed – ATM ID %d does not exist",
                 atm_id_src, atm_id_target);
        return BANK_ERR_ATM_NOT_FOUND;
    }
    if (b->atm_closed[atm_id_target] || b->atm_close_req[atm_id_target] != 0) {
        pthread_mutex_unlock(&b->atm_mtx);
        log_line("Error %d: Your close operation failed – ATM ID %d is already in a closed state",
                 atm_id_src, atm_id_target);
        return BANK_ERR_ATM_ALREADY_CLOSED;
    }
    b->atm_close_req[atm_id_target] = atm_id_src;
    pthread_mutex_unlock(&b->atm_mtx);
    return BANK_OK;
}
int bank_is_atm_closed(bank_t *b, int atm_id){
    if (atm_id < 1 || atm_id > b->atm_count) return 1;
    pthread_mutex_lock(&b->atm_mtx);
    int c = b->atm_closed[atm_id];
    pthread_mutex_unlock(&b->atm_mtx);
    return c;
}
bank_rc_t bank_rollback(bank_t *b, int atm_id, int iterations_back)
{
    if (!b || iterations_back <= 0 || iterations_back > 120) return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    rollback_req_t *req = calloc(1, sizeof(*req));
    if (!req) return BANK_ERR_ROLLBACK_NOT_POSSIBLE;
    req->atm_id = atm_id;
    req->iterations_back = iterations_back;

    pthread_mutex_lock(&b->rb_mtx);
    if (b->rb_tail) b->rb_tail->next = req;
    else b->rb_head = req;
    b->rb_tail = req;
    pthread_mutex_unlock(&b->rb_mtx);
    return BANK_OK;
}
bank_rc_t bank_invest(bank_t *b, int atm_id, int acc_id, int password,
                      int amount, currency_t cur, int time_msec)
{
    (void)atm_id;
    if (!b || amount <= 0 || time_msec <= 0) return BANK_ERR_ILLEGAL_AMOUNT;
    if (time_msec % 10 != 0) return BANK_ERR_ILLEGAL_AMOUNT;
    account_t *acc = NULL;
    bank_rc_t rc = bank_lock_account(b, acc_id, 1, &acc);
    if (rc != BANK_OK) return rc;
    if (acc->password != password) {
        bank_unlock_account(acc, 1);
        return BANK_ERR_BAD_PASSWORD;
    }
    if (account_sub(acc, cur, amount) == -1) {
        bank_unlock_account(acc, 1);
        return BANK_ERR_INSUFFICIENT_FUNDS;
    }
    bank_unlock_account(acc, 1);
    const int steps = time_msec / 10;
    double final_d = (double)amount * pow(1.03, (double)steps);
    int final_amount = (int)floor(final_d);
    if (final_amount < 0) final_amount = 0;
    sleep_msec(time_msec);
    account_t *acc2 = NULL;
    rc = bank_lock_account(b, acc_id, 1, &acc2);
    if (rc == BANK_OK) {
        account_add(acc2, cur, final_amount);
        bank_unlock_account(acc2, 1);
    }
    return BANK_OK;
}
static int cmp_acc_status_by_id(const void *a, const void *b) {
    const acc_status_t *x = (const acc_status_t *)a;
    const acc_status_t *y = (const acc_status_t *)b;
    return (x->id - y->id);
}
void *bank_status_thread(void *arg){
    bank_t *b = (bank_t *)arg;
    while (!bank_should_stop(b)) {
        /* spec: print status every 10 milliseconds */
        sleep_msec(10);
        if (bank_should_stop(b)) break;
        rwlock_rdlock(&b->accounts_lock);
        int expected = b->count;
        acc_status_t *arr = NULL;
        if (expected > 0) {
            arr = (acc_status_t *)malloc((size_t)expected * sizeof(acc_status_t));
        }
        int k = 0;
        for (int i = 0; i < b->capacity && k < expected; i++) {
            account_t *acc = b->entries[i].acc;
            if (!acc) continue;
            rwlock_rdlock(&acc->lock);
            arr[k].id = acc->id;
            arr[k].password = acc->password;
            arr[k].ils = account_get_balance(acc, CUR_ILS);
            arr[k].usd = account_get_balance(acc, CUR_USD);
            rwlock_rdunlock(&acc->lock);

            k++;
        }
        rwlock_rdunlock(&b->accounts_lock);
        if (k > 1) {
            qsort(arr, (size_t)k, sizeof(acc_status_t), cmp_acc_status_by_id);
        }
        snapshot_capture(b, arr, k);
        int bank_ils , bank_usd ;
        pthread_mutex_lock(&b->bank_money_mtx);
        bank_ils = b->bank_ils;
        bank_usd = b->bank_usd;
        pthread_mutex_unlock(&b->bank_money_mtx);
        printf("\033[2J");
        printf("\033[1;1H");
        printf("Current Bank Status\n");
        for (int i = 0; i < k; i++) {
            printf("Account %d: Balance - %d ILS %d USD, Account Password - %d\n",
                   arr[i].id, arr[i].ils, arr[i].usd, arr[i].password);
        }
        printf("The Bank has %d ILS and %d USD\n\n", bank_ils, bank_usd);
        fflush(stdout);
        free(arr);

        /* After printing status: handle pending ATM close requests (then rollback later). */
        pthread_mutex_lock(&b->atm_mtx);
        for (int target = 1; target <= b->atm_count; target++) {
            int src = b->atm_close_req[target];
            if (src != 0 && !b->atm_closed[target]) {
                b->atm_closed[target] = 1;
                b->atm_close_req[target] = 0;
                log_line("Bank: ATM %d closed %d successfully", src, target);
            }
        }
        pthread_mutex_unlock(&b->atm_mtx);

        /* After close requests, handle rollback requests. */
        bank_process_rollbacks(b);
    }
    return NULL;
}

void *bank_commission_thread(void *arg){
    bank_t *b = (bank_t *)arg;
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)pthread_self();
    while (!bank_should_stop(b)) {
        /* spec: charge commission every 30 milliseconds */
        sleep_msec(30);
        if (bank_should_stop(b)) break;
        rwlock_rdlock(&b->accounts_lock);
        for (int i = 0; i < b->capacity; i++) {
            account_t *acc = b->entries[i].acc;
            if (!acc) continue;
            int percent = (int)(xorshift32(&seed) % 5) + 1;
            rwlock_wrlock(&acc->lock);
            int bal_ils = account_get_balance(acc, CUR_ILS);
            int bal_usd = account_get_balance(acc, CUR_USD);
            int com_ils = (bal_ils * percent) / 100;
            int com_usd = (bal_usd * percent) / 100;
            if (com_ils > 0) (void)account_sub(acc, CUR_ILS, com_ils);
            if (com_usd > 0) (void)account_sub(acc, CUR_USD, com_usd);
            int acc_id = acc->id;
            rwlock_wrunlock(&acc->lock);
            pthread_mutex_lock(&b->bank_money_mtx);
            b->bank_ils += com_ils;
            b->bank_usd += com_usd;
            pthread_mutex_unlock(&b->bank_money_mtx);
            log_line("Bank: commissions of %d %% were charged, bank gained %d ILS and %d USD from account %d",
                     percent, com_ils, com_usd, acc_id);
        }
        rwlock_rdunlock(&b->accounts_lock);
    }
    return NULL;
}
