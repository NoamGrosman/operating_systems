#ifndef ACCOUNT_H
#define ACCOUNT_H
#include "rwlock.h"
typedef enum {
    CUR_ILS = 0,
    CUR_USD = 1
} currency_t;

typedef struct {
    int id;
    int password;

    int balance_ils;
    int balance_usd;

    /* Per-account lock (RW): readers for status/balance, writer for updates */
    rwlock_t lock;

    /* If you later implement investments, add fields here */
} account_t;
/* Allocate + initialize a new account object */
account_t *account_create(int id, int password, int init_ils, int init_usd);

/* Destroy and free */
void account_destroy(account_t *acc);

/* Check password (no locking inside; caller decides lock strategy) */
int account_check_password(const account_t *acc, int password);

/* Helpers to get/set balances (caller holds acc lock appropriately) */
int account_get_balance(const account_t *acc, currency_t cur);
void account_add(account_t *acc, currency_t cur, int amount);
int  account_sub(account_t *acc, currency_t cur, int amount);
#endif //ACCOUNT_H
