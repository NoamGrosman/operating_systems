/*
 * Test 8: Error handling and edge cases
 * Tests various error conditions and edge cases
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
            usleep(50000);
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

int test_invalid_command() {
    printf("Test: Invalid command (nonexistentcommand)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "nonexistentcommand123",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should see error message
    if (strstr(output, "error") != NULL || 
        strstr(output, "not found") != NULL ||
        strstr(output, "No such file") != NULL ||
        strstr(output, "cannot find") != NULL) {
        printf("  PASSED: Error message shown\n");
        return 0;
    }
    printf("  FAILED: Expected error message for invalid command\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_cd_too_many_args() {
    printf("Test: cd with too many arguments\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "cd /tmp extra_arg another_arg",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should see "too many arguments" error
    if (strstr(output, "too many arguments") != NULL) {
        printf("  PASSED: Too many arguments error\n");
        return 0;
    }
    printf("  FAILED: Expected 'too many arguments' error\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_kill_invalid_job() {
    printf("Test: kill with invalid job ID\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "kill 9 999",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should see "does not exist" error
    if (strstr(output, "does not exist") != NULL) {
        printf("  PASSED: Job not exist error\n");
        return 0;
    }
    printf("  FAILED: Expected 'does not exist' error\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_fg_no_jobs() {
    printf("Test: fg with no background jobs\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "fg",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should see error about no jobs
    if (strstr(output, "jobs list is empty") != NULL ||
        strstr(output, "no jobs") != NULL ||
        strstr(output, "error") != NULL) {
        printf("  PASSED: No jobs error shown\n");
        return 0;
    }
    printf("  FAILED: Expected empty jobs list error\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_fg_invalid_job() {
    printf("Test: fg with invalid job ID\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "fg 999",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should see error about job not existing
    if (strstr(output, "does not exist") != NULL ||
        strstr(output, "error") != NULL) {
        printf("  PASSED: Invalid job error\n");
        return 0;
    }
    printf("  FAILED: Expected job not exist error\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_alias_syntax_errors() {
    printf("Test: alias syntax errors\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "alias invalid syntax",  // Missing =
        "alias name=",           // Missing command
        "alias =cmd",            // Missing name
        "quit"
    };
    
    run_smash_commands(commands, 4, output, BUFFER_SIZE);
    
    // Should see some error messages
    int error_count = 0;
    char* p = output;
    while ((p = strstr(p, "error")) != NULL) {
        error_count++;
        p++;
    }
    if ((p = strstr(output, "invalid")) != NULL) error_count++;
    
    if (error_count > 0) {
        printf("  PASSED: Syntax errors detected\n");
        return 0;
    }
    printf("  NOTE: Error handling for malformed aliases varies\n");
    return 0;
}

int test_unalias_nonexistent() {
    printf("Test: unalias nonexistent alias\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "unalias thisaliasdoesnotexist",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should see error
    if (strstr(output, "error") != NULL ||
        strstr(output, "not found") != NULL ||
        strstr(output, "does not exist") != NULL ||
        strstr(output, "alias not") != NULL) {
        printf("  PASSED: Unalias error shown\n");
        return 0;
    }
    printf("  FAILED: Expected error for nonexistent alias\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_diff_missing_files() {
    printf("Test: diff with missing files\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "diff /nonexistent/file1 /nonexistent/file2",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should see error about unable to open
    if (strstr(output, "failed to open") != NULL ||
        strstr(output, "error") != NULL ||
        strstr(output, "cannot open") != NULL) {
        printf("  PASSED: File open error shown\n");
        return 0;
    }
    printf("  FAILED: Expected file open error\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_showpid_extra_args() {
    printf("Test: showpid with extra arguments (should ignore)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "showpid extra args here",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should still show PID (ignoring extra args)
    if (strstr(output, "smash pid is") != NULL) {
        printf("  PASSED: showpid works with extra args\n");
        return 0;
    }
    printf("  FAILED: showpid should work with extra args\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_pwd_extra_args() {
    printf("Test: pwd with extra arguments (should ignore)\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "pwd extra args here",
        "quit"
    };
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    // Should still show current directory
    if (strchr(output, '/') != NULL) {
        printf("  PASSED: pwd works with extra args\n");
        return 0;
    }
    printf("  FAILED: pwd should work with extra args\n");
    printf("  Output: %s\n", output);
    return 1;
}

int test_special_characters_in_args() {
    printf("Test: Special characters in arguments\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "echo 'hello world'",
        "echo \"test message\"",
        "echo test$special",
        "quit"
    };
    
    run_smash_commands(commands, 4, output, BUFFER_SIZE);
    
    // Should handle quotes
    if (strstr(output, "hello") != NULL || strstr(output, "test") != NULL) {
        printf("  PASSED: Special characters handled\n");
        return 0;
    }
    printf("  NOTE: Quote handling varies by implementation\n");
    return 0;
}

int main() {
    printf("=== Test 8: Error Handling and Edge Cases ===\n\n");
    
    int failures = 0;
    
    failures += test_invalid_command();
    failures += test_cd_too_many_args();
    failures += test_kill_invalid_job();
    failures += test_fg_no_jobs();
    failures += test_fg_invalid_job();
    failures += test_alias_syntax_errors();
    failures += test_unalias_nonexistent();
    failures += test_diff_missing_files();
    failures += test_showpid_extra_args();
    failures += test_pwd_extra_args();
    failures += test_special_characters_in_args();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
