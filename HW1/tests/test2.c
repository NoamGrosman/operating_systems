/*
 * Test 2: cd command tests
 * Tests directory changing functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

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
        // Child process
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
        }
        write(cmd_pipe[1], "quit\n", 5);
        close(cmd_pipe[1]);
        
        int status;
        waitpid(smash_pid, &status, 0);
        exit(WEXITSTATUS(status));
    }
    
    close(pipefd[1]);
    
    ssize_t bytes_read = read(pipefd[0], output, output_size - 1);
    if (bytes_read > 0) {
        output[bytes_read] = '\0';
    } else {
        output[0] = '\0';
    }
    close(pipefd[0]);
    
    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

int test_cd_basic() {
    printf("Test: cd /tmp then pwd\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"cd /tmp", "pwd"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE);
    
    if (strstr(output, "/tmp") != NULL) {
        printf("  PASSED: cd changed to /tmp\n");
        return 0;
    } else {
        printf("  FAILED: Expected /tmp in output, got: %s\n", output);
        return 1;
    }
}

int test_cd_parent() {
    printf("Test: cd .. from /tmp\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"cd /tmp", "cd ..", "pwd"};
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE);
    
    // After cd /tmp and cd .., should be at /
    char* last_slash = strrchr(output, '/');
    if (last_slash && (strcmp(last_slash, "/\n") == 0 || strcmp(last_slash, "/") == 0 ||
        strstr(output, "smash > /\n") != NULL)) {
        printf("  PASSED: cd .. went to parent\n");
        return 0;
    }
    // Check if we're at root
    if (strstr(output, "\n/\n") != NULL || strstr(output, "> /\n") != NULL) {
        printf("  PASSED: cd .. went to root\n");
        return 0;
    }
    printf("  FAILED: Expected / in output, got: %s\n", output);
    return 1;
}

int test_cd_dash() {
    printf("Test: cd - returns to previous directory\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"cd /tmp", "cd /var", "cd -", "pwd"};
    
    run_smash_commands(commands, 4, output, BUFFER_SIZE);
    
    if (strstr(output, "/tmp") != NULL) {
        printf("  PASSED: cd - returned to /tmp\n");
        return 0;
    } else {
        printf("  FAILED: Expected /tmp in output, got: %s\n", output);
        return 1;
    }
}

int test_cd_dash_no_oldpwd() {
    printf("Test: cd - when no previous directory\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"cd -"};
    
    run_smash_commands(commands, 1, output, BUFFER_SIZE);
    
    // Check for either format of the error message
    if (strstr(output, "old pwd not set") != NULL || 
        strstr(output, "OLDPWD not set") != NULL ||
        strstr(output, "oldpwd") != NULL) {
        printf("  PASSED: cd - reports no old pwd\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'old pwd not set' error, got: %s\n", output);
        return 1;
    }
}

int test_cd_nonexistent() {
    printf("Test: cd to nonexistent directory\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"cd /this_path_does_not_exist_12345"};
    
    run_smash_commands(commands, 1, output, BUFFER_SIZE);
    
    if (strstr(output, "does not exist") != NULL || 
        strstr(output, "no such") != NULL ||
        strstr(output, "No such") != NULL) {
        printf("  PASSED: cd reports nonexistent directory\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'does not exist' error, got: %s\n", output);
        return 1;
    }
}

int test_cd_to_file() {
    printf("Test: cd to a file (not directory)\n");
    char output[BUFFER_SIZE];
    
    // Create a temp file
    const char* temp_file = "/tmp/smash_test_file_12345";
    int fd = open(temp_file, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        printf("  SKIPPED: Could not create test file\n");
        return 0;
    }
    close(fd);
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "cd %s", temp_file);
    const char* commands[] = {cmd};
    
    run_smash_commands(commands, 1, output, BUFFER_SIZE);
    
    unlink(temp_file);  // Clean up
    
    if (strstr(output, "not a directory") != NULL || 
        strstr(output, "Not a directory") != NULL) {
        printf("  PASSED: cd reports not a directory\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'not a directory' error, got: %s\n", output);
        return 1;
    }
}

int test_cd_wrong_args() {
    printf("Test: cd with wrong number of arguments\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"cd"};  // No arguments
    
    run_smash_commands(commands, 1, output, BUFFER_SIZE);
    
    if (strstr(output, "expected 1 argument") != NULL || 
        strstr(output, "invalid arguments") != NULL) {
        printf("  PASSED: cd reports wrong args\n");
        return 0;
    } else {
        printf("  FAILED: Expected argument error, got: %s\n", output);
        return 1;
    }
}

int main() {
    printf("=== Test 2: cd Command Tests ===\n\n");
    
    int failures = 0;
    
    failures += test_cd_basic();
    failures += test_cd_parent();
    failures += test_cd_dash();
    failures += test_cd_dash_no_oldpwd();
    failures += test_cd_nonexistent();
    failures += test_cd_to_file();
    failures += test_cd_wrong_args();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
