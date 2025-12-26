/*
 * Test 4: diff and quit commands
 * Tests file comparison and quit functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

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

void create_test_file(const char* path, const char* content) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, content, strlen(content));
        close(fd);
    }
}

int test_diff_same_files() {
    printf("Test: diff with identical files\n");
    char output[BUFFER_SIZE];
    
    const char* file1 = "/tmp/smash_test_diff1";
    const char* file2 = "/tmp/smash_test_diff2";
    
    create_test_file(file1, "identical content\n");
    create_test_file(file2, "identical content\n");
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "diff %s %s", file1, file2);
    const char* commands[] = {cmd, "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    unlink(file1);
    unlink(file2);
    
    // Should output 0 for identical files
    if (strstr(output, "0") != NULL && strstr(output, "1") == NULL) {
        printf("  PASSED: diff returns 0 for identical files\n");
        return 0;
    }
    // Check if there's a 0 after the prompt
    char* prompt = strstr(output, "smash >");
    if (prompt && strstr(prompt, "0")) {
        printf("  PASSED: diff returns 0 for identical files\n");
        return 0;
    }
    printf("  FAILED: Expected 0, got: %s\n", output);
    return 1;
}

int test_diff_different_files() {
    printf("Test: diff with different files\n");
    char output[BUFFER_SIZE];
    
    const char* file1 = "/tmp/smash_test_diff1";
    const char* file2 = "/tmp/smash_test_diff2";
    
    create_test_file(file1, "content one\n");
    create_test_file(file2, "content two\n");
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "diff %s %s", file1, file2);
    const char* commands[] = {cmd, "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    unlink(file1);
    unlink(file2);
    
    // Should output 1 for different files
    if (strstr(output, "1") != NULL) {
        printf("  PASSED: diff returns 1 for different files\n");
        return 0;
    }
    printf("  FAILED: Expected 1, got: %s\n", output);
    return 1;
}

int test_diff_nonexistent() {
    printf("Test: diff with nonexistent file\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"diff /nonexistent1 /nonexistent2", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "expected valid paths") != NULL) {
        printf("  PASSED: diff reports invalid paths\n");
        return 0;
    }
    printf("  FAILED: Expected 'expected valid paths for files', got: %s\n", output);
    return 1;
}

int test_diff_directory() {
    printf("Test: diff with directories\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"diff /tmp /var", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "paths are not files") != NULL) {
        printf("  PASSED: diff rejects directories\n");
        return 0;
    }
    printf("  FAILED: Expected 'paths are not files', got: %s\n", output);
    return 1;
}

int test_diff_wrong_args() {
    printf("Test: diff with wrong number of arguments\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"diff /tmp", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "expected 2 arguments") != NULL) {
        printf("  PASSED: diff reports wrong arguments\n");
        return 0;
    }
    printf("  FAILED: Expected 'expected 2 arguments', got: %s\n", output);
    return 1;
}

int test_quit_basic() {
    printf("Test: quit exits with code 0\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"quit"};
    
    int exit_code = run_smash_commands(commands, 1, output, BUFFER_SIZE, 5);
    
    if (exit_code == 0) {
        printf("  PASSED: quit exits with code 0\n");
        return 0;
    }
    printf("  FAILED: Expected exit code 0, got: %d\n", exit_code);
    return 1;
}

int test_quit_kill() {
    printf("Test: quit kill terminates jobs\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"sleep 100 &", "sleep 100 &", "quit kill"};
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE, 15);
    
    if (strstr(output, "SIGTERM") != NULL) {
        printf("  PASSED: quit kill sends SIGTERM\n");
        return 0;
    }
    printf("  FAILED: Expected SIGTERM message, got: %s\n", output);
    return 1;
}

int test_quit_invalid_arg() {
    printf("Test: quit with invalid argument\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"quit foo", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "unexpected arguments") != NULL) {
        printf("  PASSED: quit rejects invalid argument\n");
        return 0;
    }
    printf("  FAILED: Expected 'unexpected arguments', got: %s\n", output);
    return 1;
}

int main() {
    printf("=== Test 4: diff and quit Commands ===\n\n");
    
    int failures = 0;
    
    failures += test_diff_same_files();
    failures += test_diff_different_files();
    failures += test_diff_nonexistent();
    failures += test_diff_directory();
    failures += test_diff_wrong_args();
    failures += test_quit_basic();
    failures += test_quit_kill();
    failures += test_quit_invalid_arg();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
