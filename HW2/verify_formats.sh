#!/bin/bash
# Log format verification script
# Verifies that log output matches exact specification format

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

cd /home/noam0/final_os_2

echo "=========================================="
echo "Log Format Verification Tests"
echo "=========================================="

PASS=0
FAIL=0

# Test 1: Open account format
echo -n "Open account format: "
timeout 5 ./bank 0 tests/spec_log_format_basic.txt > /dev/null 2>&1
if grep -q "^1: New account id is [0-9]* with password [0-9]* and initial balance [0-9]* ILS and [0-9]* USD$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 2: Balance inquiry format
echo -n "Balance inquiry format: "
if grep -q "^1: Account [0-9]* balance is [0-9]* ILS and [0-9]* USD$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 3: Deposit format
echo -n "Deposit format: "
if grep -q "^1: Account [0-9]* new balance is [0-9]* ILS and [0-9]* USD after [0-9]* ILS was deposited$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 4: Close account format
echo -n "Close account format: "
timeout 8 ./bank 0 tests/spec_close_account_format.txt > /dev/null 2>&1
if grep -E "^1: Account [0-9]+ is now closed\. Balance was [0-9]+ ILS and [0-9]+ USD$" log.txt > /dev/null; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 5: Sleep format
echo -n "Sleep format: "
timeout 5 ./bank 0 tests/spec_sleep_format.txt > /dev/null 2>&1
if grep -q "^1: Currently on a scheduled break. Service will resume within [0-9]* ms\.$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 6: Rollback format
echo -n "Rollback format: "
timeout 8 ./bank 0 tests/spec_rollback_format.txt > /dev/null 2>&1
if grep -q "^1: Rollback to [0-9]* bank iterations ago was completed successfully$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 7: Commission format
echo -n "Commission format: "
if grep -q "^Bank: commissions of [0-9]* % were charged, bank gained [0-9]* ILS and [0-9]* USD from account [0-9]*$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 8: ATM close format
echo -n "ATM close format: "
timeout 10 ./bank 0 tests/spec_close_already_closed1.txt tests/spec_close_already_closed2.txt > /dev/null 2>&1
if grep -q "^Bank: ATM [0-9]* closed [0-9]* successfully$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 9: Error - account exists format
echo -n "Error account exists format: "
timeout 8 ./bank 0 tests/spec_account_exists_error.txt > /dev/null 2>&1
if grep -q "^Error 1: Your transaction failed – account with the same id exists$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 10: Error - account not exist format
echo -n "Error account not exist format: "
timeout 8 ./bank 0 tests/spec_account_not_exist.txt > /dev/null 2>&1
if grep -q "^Error 1: Your transaction failed – account id [0-9]* does not exist$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 11: Error - wrong password format
echo -n "Error wrong password format: "
timeout 10 ./bank 0 tests/spec_wrong_password.txt > /dev/null 2>&1
if grep -q "^Error 1: Your transaction failed – password for account id [0-9]* is incorrect$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 12: Error - insufficient funds format
echo -n "Error insufficient funds format: "
timeout 8 ./bank 0 tests/spec_insufficient_funds.txt > /dev/null 2>&1
if grep -q "^Error 1: Your transaction failed – account id [0-9]* balance is [0-9]* ILS and [0-9]* USD is lower than [0-9]* \(ILS\|USD\)$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 13: Transfer format
echo -n "Transfer format: "
timeout 12 ./bank 0 tests/spec_transfer_both_currencies.txt > /dev/null 2>&1
if grep -q "^1: Transfer [0-9]* \(ILS\|USD\) from account [0-9]* to account [0-9]* new account balance is [0-9]* ILS and [0-9]* USD new target account balance is [0-9]* ILS and [0-9]* USD$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 14: Exchange format
echo -n "Exchange format: "
timeout 10 ./bank 0 tests/spec_exchange_format.txt > /dev/null 2>&1
if grep -q "^1: Account [0-9]* new balance is [0-9]* ILS and [0-9]* USD after [0-9]* \(ILS\|USD\) was exchanged$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 15: Error - ATM already closed format
echo -n "Error ATM already closed format: "
timeout 10 ./bank 0 tests/spec_close_already_closed1.txt tests/spec_close_already_closed2.txt > /dev/null 2>&1
if grep -q "^Error 1: Your close operation failed – ATM ID [0-9]* is already in a closed state$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 16: Error - ATM not exist format
echo -n "Error ATM not exist format: "
timeout 5 ./bank 0 tests/spec_close_atm_format.txt > /dev/null 2>&1
if grep -q "^Error 1: Your transaction failed – ATM ID [0-9]* does not exist$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

# Test 17: Error - transfer insufficient funds format (different from withdraw)
echo -n "Transfer insufficient funds format: "
timeout 10 ./bank 0 tests/spec_transfer_insufficient.txt > /dev/null 2>&1
if grep -q "^Error 1: Your transaction failed – balance of account id [0-9]* is lower than [0-9]* \(ILS\|USD\)$" log.txt; then
    echo -e "${GREEN}PASS${NC}"
    ((PASS++))
else
    echo -e "${RED}FAIL${NC}"
    ((FAIL++))
fi

echo ""
echo "=========================================="
echo "Format Verification Summary"
echo "=========================================="
echo -e "Passed: ${GREEN}$PASS${NC}"
echo -e "Failed: ${RED}$FAIL${NC}"

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}All format checks passed!${NC}"
    exit 0
else
    echo -e "${RED}Some format checks failed!${NC}"
    exit 1
fi
