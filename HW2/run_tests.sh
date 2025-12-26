#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Results tracking
declare -a PASSED_TESTS
declare -a FAILED_TESTS

# Build the project first
echo "=========================================="
echo "Building project..."
echo "=========================================="
make clean > /dev/null 2>&1
if ! make > /dev/null 2>&1; then
    echo -e "${RED}BUILD FAILED${NC}"
    exit 1
fi
echo -e "${GREEN}Build successful${NC}"
echo ""

# Function to run a test
run_test() {
    local test_name="$1"
    local timeout_sec="$2"
    shift 2
    local files=("$@")
    
    printf "%-50s " "$test_name"
    
    # Run the test
    timeout "$timeout_sec" ./bank 0 "${files[@]}" > /dev/null 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}PASS${NC}"
        PASSED_TESTS+=("$test_name")
        return 0
    elif [ $exit_code -eq 124 ]; then
        echo -e "${RED}FAIL (timeout)${NC}"
        FAILED_TESTS+=("$test_name (timeout)")
        return 1
    else
        echo -e "${RED}FAIL (exit: $exit_code)${NC}"
        FAILED_TESTS+=("$test_name (exit: $exit_code)")
        return 1
    fi
}

# Function to run VIP test
run_vip_test() {
    local test_name="$1"
    local timeout_sec="$2"
    local vip_threads="$3"
    shift 3
    local files=("$@")
    
    printf "%-50s " "$test_name"
    
    timeout "$timeout_sec" ./bank "$vip_threads" "${files[@]}" > /dev/null 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}PASS${NC}"
        PASSED_TESTS+=("$test_name")
        return 0
    elif [ $exit_code -eq 124 ]; then
        echo -e "${RED}FAIL (timeout)${NC}"
        FAILED_TESTS+=("$test_name (timeout)")
        return 1
    else
        echo -e "${RED}FAIL (exit: $exit_code)${NC}"
        FAILED_TESTS+=("$test_name (exit: $exit_code)")
        return 1
    fi
}

echo "=========================================="
echo "Running Single ATM Tests"
echo "=========================================="

# Basic tests (test1-test4)
run_test "test1_basic" 15 tests/test1_basic.txt
run_test "test2_errors" 12 tests/test2_errors.txt
run_test "test3_transfer" 15 tests/test3_transfer.txt
run_test "test4_atm1" 8 tests/test4_atm1.txt

# Single ATM edge cases
run_test "edge_zero_balance" 12 tests/edge_zero_balance.txt
run_test "edge_passwords" 10 tests/edge_passwords.txt
run_test "edge_exact_balance" 12 tests/edge_exact_balance.txt
run_test "edge_exchange_rounding" 15 tests/edge_exchange_rounding.txt
run_test "edge_close_atm_invalid" 8 tests/edge_close_atm_invalid.txt
run_test "edge_close_self" 8 tests/edge_close_self.txt
run_test "edge_transfer_errors" 15 tests/edge_transfer_errors.txt
run_test "edge_rollback_simple" 12 tests/edge_rollback_simple.txt
run_test "edge_persistent_fail" 15 tests/edge_persistent_fail.txt
run_test "edge_investment_small" 15 tests/edge_investment_small.txt
run_test "edge_investment_errors" 10 tests/edge_investment_errors.txt
run_test "edge_sleep_only" 5 tests/edge_sleep_only.txt
run_test "edge_nonexistent_account" 10 tests/edge_nonexistent_account.txt
run_test "edge_empty_lines" 10 tests/edge_empty_lines.txt
run_test "edge_large_amounts" 12 tests/edge_large_amounts.txt
run_test "edge_many_accounts" 20 tests/edge_many_accounts.txt

echo ""
echo "=========================================="
echo "Running Multi-ATM Tests"
echo "=========================================="

# Multi-ATM concurrency tests
run_test "test5_deadlock (2 ATMs)" 15 tests/test5_deadlock1.txt tests/test5_deadlock2.txt
run_test "edge_race_open (2 ATMs)" 10 tests/edge_race_open1.txt tests/edge_race_open2.txt
run_test "edge_race_close (2 ATMs)" 10 tests/edge_race_close1.txt tests/edge_race_close2.txt
run_test "edge_deadlock_heavy (2 ATMs)" 25 tests/edge_deadlock_heavy1.txt tests/edge_deadlock_heavy2.txt
run_test "edge_commission_race (2 ATMs)" 20 tests/edge_commission_race1.txt tests/edge_commission_race2.txt
run_test "test9_concurrent (2 ATMs)" 20 tests/test9_concurrent_atm1.txt tests/test9_concurrent_atm2.txt

# 3-ATM tests
run_test "edge_triangle_transfer (3 ATMs)" 35 tests/edge_triangle_transfer1.txt tests/edge_triangle_transfer2.txt tests/edge_triangle_transfer3.txt
run_test "edge_close_multiple (3 ATMs)" 25 tests/edge_close_multiple_atm1.txt tests/edge_close_multiple_atm2.txt tests/edge_close_multiple_atm3.txt
run_test "test13_massive (3 ATMs)" 25 tests/test13_massive_atm1.txt tests/test13_massive_atm2.txt tests/test13_massive_atm3.txt

# 4-ATM test
run_test "test4 (4 ATMs)" 15 tests/test4_atm1.txt tests/test4_atm2.txt tests/test4_atm3.txt tests/test4_atm4.txt

echo ""
echo "=========================================="
echo "Running VIP Tests"
echo "=========================================="

run_vip_test "test7_vip (2 VIP workers)" 12 2 tests/test7_vip_atm1.txt
run_vip_test "edge_vip_priority (3 VIP workers)" 12 3 tests/edge_vip_priority.txt
run_vip_test "edge_vip_persistent (2 VIP workers)" 15 2 tests/edge_vip_persistent.txt

echo ""
echo "=========================================="
echo "Running Special Tests"
echo "=========================================="

run_test "test6_exchange" 12 tests/test6_exchange.txt
run_test "test8_sleep" 10 tests/test8_sleep.txt
run_test "test10_rollback" 15 tests/test10_rollback_atm.txt
run_test "test11_close_atm (2 ATMs)" 15 tests/test11_close_atm1.txt tests/test11_close_atm2.txt
run_test "test12_investment" 15 tests/test12_investment.txt
run_test "test14_persistent" 12 tests/test14_persistent.txt

echo ""
echo "=========================================="
echo "Running Specification Compliance Tests"
echo "=========================================="

# Log format tests
run_test "spec_log_format_basic" 12 tests/spec_log_format_basic.txt
run_test "spec_sleep_format" 8 tests/spec_sleep_format.txt
run_test "spec_exchange_format" 12 tests/spec_exchange_format.txt
run_test "spec_rollback_format" 10 tests/spec_rollback_format.txt
run_test "spec_close_atm_format" 8 tests/spec_close_atm_format.txt

# Error handling tests (per spec)
run_test "spec_account_exists_error" 10 tests/spec_account_exists_error.txt
run_test "spec_account_not_exist" 10 tests/spec_account_not_exist.txt
run_test "spec_wrong_password" 12 tests/spec_wrong_password.txt
run_test "spec_insufficient_funds" 10 tests/spec_insufficient_funds.txt

# Transfer spec tests
run_test "spec_transfer_order" 15 tests/spec_transfer_order.txt
run_test "spec_transfer_account_check" 12 tests/spec_transfer_account_check.txt
run_test "spec_transfer_both_currencies" 15 tests/spec_transfer_both_currencies.txt
run_test "spec_transfer_insufficient" 12 tests/spec_transfer_insufficient.txt

# Exchange rate tests (1 USD = 5 ILS)
run_test "spec_exchange_ils_to_usd" 12 tests/spec_exchange_ils_to_usd.txt
run_test "spec_exchange_usd_to_ils" 12 tests/spec_exchange_usd_to_ils.txt

# Investment tests (compound interest 1.03^n)
run_test "spec_investment_compound" 15 tests/spec_investment_compound.txt
run_test "spec_investment_insufficient" 10 tests/spec_investment_insufficient.txt
run_test "spec_investment_blocked" 15 tests/spec_investment_blocked.txt

# ATM close tests
run_test "spec_atm_close (3 ATMs)" 20 tests/spec_atm_close_source.txt tests/spec_atm_close_target1.txt tests/spec_atm_close_target2.txt
run_test "spec_close_already_closed (2 ATMs)" 15 tests/spec_close_already_closed1.txt tests/spec_close_already_closed2.txt

# VIP tests
run_vip_test "spec_vip_basic (2 workers)" 12 2 tests/spec_vip_basic.txt
run_vip_test "spec_vip_priority_order (3 workers)" 12 3 tests/spec_vip_priority_order.txt

# PERSISTENT tests
run_test "spec_persistent_basic" 15 tests/spec_persistent_basic.txt

# Rollback tests
run_test "spec_rollback_restore" 15 tests/spec_rollback_restore.txt

# Concurrent transfer tests
run_test "spec_concurrent_transfer (2 ATMs)" 20 tests/spec_concurrent_transfer1.txt tests/spec_concurrent_transfer2.txt

# Mixed operations
run_test "spec_mixed_operations" 12 tests/spec_mixed_operations.txt

# Example from PDF
run_test "spec_example_from_pdf" 12 tests/spec_example_from_pdf.txt

echo ""
echo "=========================================="
echo -e "${YELLOW}TEST SUMMARY${NC}"
echo "=========================================="

total_tests=$((${#PASSED_TESTS[@]} + ${#FAILED_TESTS[@]}))
passed=${#PASSED_TESTS[@]}
failed=${#FAILED_TESTS[@]}

echo -e "Total Tests: $total_tests"
echo -e "${GREEN}Passed: $passed${NC}"
echo -e "${RED}Failed: $failed${NC}"
echo ""

if [ $failed -gt 0 ]; then
    echo -e "${RED}Failed Tests:${NC}"
    for test in "${FAILED_TESTS[@]}"; do
        echo "  - $test"
    done
    echo ""
fi

# Calculate percentage
if [ $total_tests -gt 0 ]; then
    percentage=$((passed * 100 / total_tests))
    echo "Pass Rate: ${percentage}%"
fi

echo "=========================================="

# Exit with appropriate code
if [ $failed -eq 0 ]; then
    echo -e "${GREEN}ALL TESTS PASSED!${NC}"
    exit 0
else
    echo -e "${RED}SOME TESTS FAILED${NC}"
    exit 1
fi
