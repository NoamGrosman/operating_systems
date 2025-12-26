# Smash Shell Test Suite

## Prerequisites

- Build the smash shell first: `make` (from the project root)
- Python 3.x for the Python test runner

## Running Tests

### Python Test Suite

From the project root directory:

```bash
python3 tests/run_tests.py
```

This runs all test categories:
- **Module Tests** - Individual command tests (showpid, pwd, cd, jobs, kill, etc.)
- **System Tests** - Command combinations and complex scenarios
- **Stress Tests** - Performance and edge case tests

### C Test Programs

Compile the C tests:
```bash
cd tests
make
```

Run individual tests from the project root:
```bash
./tests/test1   # Basic built-in commands
./tests/test2   # cd command
./tests/test3   # Job management
./tests/test4   # diff and quit commands
./tests/test5   # External commands and aliases
./tests/test6   # Stress tests
./tests/test7   # Complex commands (&&)
./tests/test8   # Error handling
```

Or run all C tests:
```bash
cd tests && make run
```

## Test Coverage

| Test File | Description |
|-----------|-------------|
| test1.c | showpid, pwd, argument errors |
| test2.c | cd with paths, cd -, cd .., error cases |
| test3.c | Background jobs, jobs, kill, fg commands |
| test4.c | diff file comparison, quit, quit kill |
| test5.c | External commands, alias, unalias |
| test6.c | Stress: many commands, many jobs, rapid operations |
| test7.c | && command chaining |
| test8.c | Error handling and edge cases |

## Cleaning Up

```bash
cd tests && make clean
```
