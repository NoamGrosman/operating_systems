#include "account.h"
#include <stdlib.h>
#include "util.h"
account_t *account_create(int id, int password, int init_ils, int init_usd){
    account_t * obj = xmalloc(sizeof (account_t)) ;
    if (!obj)return NULL;
    obj -> id = id;
    obj ->password =password;
    obj ->balance_ils =init_ils;
    obj ->balance_usd = init_usd;
    rwlock_init(&obj->lock);
    return obj;
}

void account_destroy(account_t *acc){
    if (!acc) return;
    rwlock_destroy(&acc->lock);
    free(acc);
}

int account_check_password(const account_t *acc, int password){
    return (acc->password == password);
}
int account_get_balance(const account_t *acc, currency_t cur){
if (cur == CUR_ILS)return acc->balance_ils;
if (cur == CUR_USD)return acc->balance_usd;
return 0;
}
void account_add(account_t *acc, currency_t cur, int amount){
    if (amount <= 0)return;
    if (cur == CUR_ILS){
        acc->balance_ils +=amount;
    }else if (cur == CUR_USD){
        acc->balance_usd +=amount;
    }
}
int  account_sub(account_t *acc, currency_t cur, int amount){
    if (amount <= 0) return 0;
    if (cur == CUR_ILS){
        if (acc->balance_ils >= amount) {
            acc->balance_ils -= amount;
            return 0;
        }else{
            return -1;
        }
    }else if (cur == CUR_USD){
        if (acc->balance_usd >= amount) {
            acc->balance_usd -= amount;
            return 0;
        }else{
            return -1;
        }
    }
    return -1;
}