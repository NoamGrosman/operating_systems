# Makefile for smash test suite

CC = gcc
CFLAGS = -Wall -g

# All test files
TESTS = test1 test2 test3 test4 test5 test6 test7 test8

# Default target
all: $(TESTS)

# Pattern rule for compiling tests
test%: test%.c
	$(CC) $(CFLAGS) -o $@ $<

# Run all tests
run: all
	@echo "=== Running All C Tests ==="
	@for test in $(TESTS); do \
		echo "\n--- Running $$test ---"; \
		./$$test || true; \
	done

# Run Python tests
python-tests:
	python3 run_tests.py

# Clean up
clean:
	rm -f $(TESTS)

.PHONY: all run python-tests clean
