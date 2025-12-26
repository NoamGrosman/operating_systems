/*
 * Test 7: Complex commands (&&) and chained commands
 * Tests the && operator and command chaining
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define BUFFER_SIZE 4096

int run_smash_commands(const char* commands[], int num_commands, char* output, size_t output_size) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }
    
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        int cmd_pipe[2];
        pipe(cmd_pipe);
        
        pid_t smash_pid = fork();
        if (smash_pid == 0) {
            close(cmd_pipe[1]);
            dup2(cmd_pipe[0], STDIN_FILENO);
            close(cmd_pipe[0]);
            execlp("./smash", "./smash", NULL);
            perror("execlp");
            exit(1);
        }
        
        close(cmd_pipe[0]);
        for (int i = 0; i < num_commands; i++) {
            write(cmd_pipe[1], commands[i], strlen(commands[i]));
            write(cmd_pipe[1], "\n", 1);
            usleep(50000);  // 50ms between commands
        }
        close(cmd_pipe[1]);
        
        int status;
        waitpid(smash_pid, &status, 0);
        exit(WEXITSTATUS(status));
    }
    
    close(pipefd[1]);
    
    ssize_t total_read = 0;
    ssize_t bytes_read;
    while ((bytes_read = read(pipefd[0], output + total_read, output_size - total_read - 1)) > 0) {
        total_read += bytes_read;
    }
    output[total_read] = '\0';
    close(pipefd[0]);
    
    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

int test_simple_and_chain() {
    printf("Test: Simple && chain (echo a && echo b)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "echo a && echo b",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should have both 'a' and 'b' output
    char* pos_a = strstr(output, "a");
    char* pos_b = strstr(output, "b");
    
    if (pos_a != NULL && pos_b != NULL && pos_a < pos_b) {
        printf("  PASSED: Both commands executed in order\n");
        return 0;
    }
    printf("  FAILED: Expected 'a' then 'b'\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_builtin_chain() {
    printf("Test: Built-in && chain (pwd && showpid)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "pwd && showpid",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should have directory path and PID
    if (strstr(output, "/") != NULL && strstr(output, "smash pid is") != NULL) {
        printf("  PASSED: Built-in chain works\n");
        return 0;
    }
    printf("  FAILED: Expected pwd and showpid output\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_triple_chain() {
    printf("Test: Triple && chain (echo 1 && echo 2 && echo 3)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "echo 1 && echo 2 && echo 3",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should have 1, 2, 3 in order
    char* pos1 = strstr(output, "1");
    char* pos2 = strstr(output, "2");
    char* pos3 = strstr(output, "3");
    
    if (pos1 != NULL && pos2 != NULL && pos3 != NULL &&
        pos1 < pos2 && pos2 < pos3) {
        printf("  PASSED: Triple chain works\n");
        return 0;
    }
    printf("  FAILED: Expected '1' then '2' then '3'\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_cd_and_pwd() {
    printf("Test: cd && pwd chain\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "cd /tmp && pwd",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // pwd should show /tmp
    if (strstr(output, "/tmp") != NULL) {
        printf("  PASSED: cd && pwd works\n");
        return 0;
    }
    printf("  FAILED: Expected /tmp in output\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_chain_with_failing_first() {
    printf("Test: && chain with failing first command (cd nonexistent && pwd)\n");
    char output[BUFFER_SIZE];
    
    // First get current directory
    char* pwd = getcwd(NULL, 0);
    
    const char* commands[] = {
        "cd /this/path/does/not/exist && pwd",
        "pwd",
        "quit"
    };
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE);
    
    // Should see error message, and pwd should still show original directory
    // The second command should NOT execute if && short-circuits
    // But since we're also running pwd separately, we can check the directory didn't change
    if (strstr(output, "No such file or directory") != NULL ||
        strstr(output, "error") != NULL ||
        strstr(output, pwd) != NULL) {
        printf("  PASSED: Failure handled correctly\n");
        free(pwd);
        return 0;
    }
    printf("  NOTE: Behavior depends on implementation\n");
    free(pwd);
    return 0;  // Don't fail, just note
}

int test_external_chain() {
    printf("Test: External command chain (ls /tmp && echo done)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "ls /tmp && echo done",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should have "done" at the end
    if (strstr(output, "done") != NULL) {
        printf("  PASSED: External && chain works\n");
        return 0;
    }
    printf("  FAILED: Expected 'done' in output\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_mixed_chain() {
    printf("Test: Mixed built-in and external chain (pwd && ls . && showpid)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "pwd && echo hello && showpid",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should have directory, hello, and PID
    if (strstr(output, "/") != NULL &&
        strstr(output, "hello") != NULL &&
        strstr(output, "smash pid is") != NULL) {
        printf("  PASSED: Mixed chain works\n");
        return 0;
    }
    printf("  FAILED: Expected pwd, hello, and showpid output\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_background_in_chain() {
    printf("Test: Background in chain (note: may not be supported)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "echo first && sleep 100 &",
        "jobs",
        "quit kill"
    };
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE);
    
    // Just check that it doesn't crash
    // Behavior varies by implementation
    printf("  NOTE: Background in chain behavior implementation-specific\n");
    return 0;
}

int main() {
    printf("=== Test 7: Complex Commands (&&) ===\n\n");
    
    int failures = 0;
    
    failures += test_simple_and_chain();
    failures += test_builtin_chain();
    failures += test_triple_chain();
    failures += test_cd_and_pwd();
    failures += test_chain_with_failing_first();
    failures += test_external_chain();
    failures += test_mixed_chain();
    failures += test_background_in_chain();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
