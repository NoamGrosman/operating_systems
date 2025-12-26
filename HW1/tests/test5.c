/*
 * Test 5: External commands and alias
 * Tests external command execution and alias functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define BUFFER_SIZE 4096

int run_smash_commands(const char* commands[], int num_commands, char* output, size_t output_size, int timeout_sec) {
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
            usleep(50000);  // Small delay
        }
        close(cmd_pipe[1]);
        
        int status;
        waitpid(smash_pid, &status, 0);
        exit(WEXITSTATUS(status));
    }
    
    close(pipefd[1]);
    
    alarm(timeout_sec);
    
    ssize_t total_read = 0;
    ssize_t bytes_read;
    while ((bytes_read = read(pipefd[0], output + total_read, output_size - total_read - 1)) > 0) {
        total_read += bytes_read;
    }
    output[total_read] = '\0';
    
    alarm(0);
    close(pipefd[0]);
    
    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

int test_external_echo() {
    printf("Test: external command (echo)\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"echo hello world", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "hello world") != NULL) {
        printf("  PASSED: echo works\n");
        return 0;
    }
    printf("  FAILED: Expected 'hello world', got: %s\n", output);
    return 1;
}

int test_external_ls() {
    printf("Test: external command (ls)\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"ls", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    // ls should produce some output (file names)
    int line_count = 0;
    char* p = output;
    while ((p = strchr(p, '\n')) != NULL) {
        line_count++;
        p++;
    }
    
    if (line_count > 1) {  // At least prompt + some files
        printf("  PASSED: ls produces output\n");
        return 0;
    }
    printf("  FAILED: Expected file listing, got: %s\n", output);
    return 1;
}

int test_external_background() {
    printf("Test: external command in background\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"sleep 5 &", "jobs", "quit kill"};
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE, 10);
    
    if (strstr(output, "sleep") != NULL && strstr(output, "[") != NULL) {
        printf("  PASSED: background job listed\n");
        return 0;
    }
    printf("  FAILED: Expected sleep in jobs, got: %s\n", output);
    return 1;
}

int test_alias_basic() {
    printf("Test: basic alias\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"alias hello='echo hello world'", "hello", "quit"};
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "hello world") != NULL) {
        printf("  PASSED: alias works\n");
        return 0;
    }
    printf("  FAILED: Expected 'hello world', got: %s\n", output);
    return 1;
}

int test_alias_list() {
    printf("Test: list aliases\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"alias test='echo test'", "alias", "quit"};
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "test") != NULL) {
        printf("  PASSED: alias list works\n");
        return 0;
    }
    printf("  FAILED: Expected alias in list, got: %s\n", output);
    return 1;
}

int test_unalias() {
    printf("Test: unalias\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {
        "alias test='echo test'", 
        "unalias test", 
        "alias",
        "quit"
    };
    
    run_smash_commands(commands, 4, output, BUFFER_SIZE, 5);
    
    // After unalias, the alias list should not contain 'test='
    // Find the second 'alias' output
    char* first_alias = strstr(output, "alias");
    if (first_alias) {
        char* second_alias = strstr(first_alias + 5, "alias");
        if (second_alias) {
            // Check if test= appears after the second alias command
            if (strstr(second_alias, "test=") == NULL) {
                printf("  PASSED: unalias removes alias\n");
                return 0;
            }
        }
    }
    // If no test= anywhere, also pass
    if (strstr(output, "test=") == NULL || strstr(output, "test='") == NULL) {
        printf("  PASSED: unalias removes alias\n");
        return 0;
    }
    printf("  FAILED: Expected alias removed, got: %s\n", output);
    return 1;
}

int test_complex_command_and() {
    printf("Test: && command (both succeed)\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"echo first && echo second", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "first") != NULL && strstr(output, "second") != NULL) {
        printf("  PASSED: && executes both commands\n");
        return 0;
    }
    printf("  FAILED: Expected 'first' and 'second', got: %s\n", output);
    return 1;
}

int test_complex_command_fail() {
    printf("Test: && command (first fails)\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"cd /nonexistent && echo should_not_appear", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "should_not_appear") == NULL) {
        printf("  PASSED: && stops on first failure\n");
        return 0;
    }
    printf("  FAILED: Expected second command not to run, got: %s\n", output);
    return 1;
}

int main() {
    printf("=== Test 5: External Commands and Alias ===\n\n");
    
    int failures = 0;
    
    failures += test_external_echo();
    failures += test_external_ls();
    failures += test_external_background();
    failures += test_alias_basic();
    failures += test_alias_list();
    failures += test_unalias();
    failures += test_complex_command_and();
    failures += test_complex_command_fail();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
