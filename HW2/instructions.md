The tests for Wet 2 can be divided into 3 groups:

1. Logical correctness tests
2. Concurrency tests - commutativity (I'll expand)
3. A mix of 1 and 2

A significant portion of the tests in group 1 can be done on a single ATM - easy to follow and verify correct behavior and outputs.

Group 2 is the most dangerous / newest for you.
It's very easy to check if there's a deadlock in your code.

Under the assumption that all commands each account needs to execute will succeed (the account has enough money in the bank) and that the order doesn't matter, some of the runs are commutative in the order of operations.

Without giving a concrete example, try to think whether the order of operations matters, or whether you can think of interesting scenarios that test concurrency where only the output matters.

I recommend not testing the assignment on type 3 problems before you've verified that types 1 and 2 are done well!

Therefore, create several tests that check exactly that:

1. Logical correctness tests.
2. Concurrency tests - commutativity: check for deadlocks, concurrency etc.
3. Mix of 1 and 2.
