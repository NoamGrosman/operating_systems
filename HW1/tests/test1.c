/*
 * Test 1: Basic built-in commands (showpid, pwd)
 * Tests the most fundamental built-in commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024

int run_smash_command(const char* command, char* output, size_t output_size) {
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
        
        // Create a pipe to send command to smash
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
        write(cmd_pipe[1], command, strlen(command));
        write(cmd_pipe[1], "\n", 1);
        write(cmd_pipe[1], "quit\n", 5);
        close(cmd_pipe[1]);
        
        int status;
        waitpid(smash_pid, &status, 0);
        exit(WEXITSTATUS(status));
    }
    
    // Parent process
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

int test_showpid() {
    printf("Test: showpid\n");
    char output[BUFFER_SIZE];
    
    run_smash_command("showpid", output, BUFFER_SIZE);
    
    if (strstr(output, "smash pid is") != NULL) {
        printf("  PASSED: showpid prints correct format\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'smash pid is <pid>', got: %s\n", output);
        return 1;
    }
}

int test_pwd() {
    printf("Test: pwd\n");
    char output[BUFFER_SIZE];
    char cwd[BUFFER_SIZE];
    
    getcwd(cwd, BUFFER_SIZE);
    run_smash_command("pwd", output, BUFFER_SIZE);
    
    if (strstr(output, cwd) != NULL || output[0] == '/') {
        printf("  PASSED: pwd prints a path\n");
        return 0;
    } else {
        printf("  FAILED: Expected a path starting with /, got: %s\n", output);
        return 1;
    }
}

int test_showpid_with_args() {
    printf("Test: showpid with arguments (should fail)\n");
    char output[BUFFER_SIZE];
    
    run_smash_command("showpid arg1", output, BUFFER_SIZE);
    
    if (strstr(output, "expected 0 arguments") != NULL) {
        printf("  PASSED: showpid rejects arguments\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'expected 0 arguments' error, got: %s\n", output);
        return 1;
    }
}

int test_pwd_with_args() {
    printf("Test: pwd with arguments (should fail)\n");
    char output[BUFFER_SIZE];
    
    run_smash_command("pwd arg1", output, BUFFER_SIZE);
    
    if (strstr(output, "expected 0 arguments") != NULL) {
        printf("  PASSED: pwd rejects arguments\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'expected 0 arguments' error, got: %s\n", output);
        return 1;
    }
}

int main() {
    printf("=== Test 1: Basic Built-in Commands ===\n\n");
    
    int failures = 0;
    
    failures += test_showpid();
    failures += test_pwd();
    failures += test_showpid_with_args();
    failures += test_pwd_with_args();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
