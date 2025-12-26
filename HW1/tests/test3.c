/*
 * Test 3: jobs, kill, fg commands
 * Tests job management functionality
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
            usleep(100000);  // Small delay between commands
        }
        close(cmd_pipe[1]);
        
        int status;
        waitpid(smash_pid, &status, 0);
        exit(WEXITSTATUS(status));
    }
    
    close(pipefd[1]);
    
    // Set up timeout
    alarm(timeout_sec);
    
    ssize_t total_read = 0;
    ssize_t bytes_read;
    while ((bytes_read = read(pipefd[0], output + total_read, output_size - total_read - 1)) > 0) {
        total_read += bytes_read;
    }
    output[total_read] = '\0';
    
    alarm(0);  // Cancel alarm
    close(pipefd[0]);
    
    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

int test_jobs_empty() {
    printf("Test: jobs with empty list\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"jobs", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    // Count job entries (lines starting with [)
    int job_count = 0;
    char* p = output;
    while ((p = strchr(p, '[')) != NULL) {
        // Check if it's a job listing (has ] and :)
        char* bracket = strchr(p, ']');
        if (bracket && strchr(bracket, ':')) {
            job_count++;
        }
        p++;
    }
    
    if (job_count == 0) {
        printf("  PASSED: jobs shows empty list\n");
        return 0;
    } else {
        printf("  FAILED: Expected empty list, found %d jobs: %s\n", job_count, output);
        return 1;
    }
}

int test_jobs_with_background() {
    printf("Test: jobs with background process\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"sleep 10 &", "jobs", "quit kill"};
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE, 15);
    
    if (strstr(output, "sleep") != NULL && strstr(output, "[") != NULL) {
        printf("  PASSED: jobs shows background process\n");
        return 0;
    } else {
        printf("  FAILED: Expected sleep job in output, got: %s\n", output);
        return 1;
    }
}

int test_kill_job() {
    printf("Test: kill sends signal to job\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"sleep 100 &", "kill 9 0", "quit"};
    
    run_smash_commands(commands, 3, output, BUFFER_SIZE, 10);
    
    if (strstr(output, "signal number 9 was sent to pid") != NULL ||
        strstr(output, "signal") != NULL) {
        printf("  PASSED: kill sent signal\n");
        return 0;
    } else {
        printf("  FAILED: Expected signal confirmation, got: %s\n", output);
        return 1;
    }
}

int test_kill_nonexistent() {
    printf("Test: kill nonexistent job\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"kill 9 99", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "job id 99 does not exist") != NULL) {
        printf("  PASSED: kill reports nonexistent job\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'job id 99 does not exist', got: %s\n", output);
        return 1;
    }
}

int test_kill_invalid_args() {
    printf("Test: kill with invalid arguments\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"kill abc 0", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "invalid arguments") != NULL) {
        printf("  PASSED: kill reports invalid arguments\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'invalid arguments', got: %s\n", output);
        return 1;
    }
}

int test_fg_empty_list() {
    printf("Test: fg with empty job list\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"fg", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "job") != NULL && strstr(output, "empty") != NULL) {
        printf("  PASSED: fg reports empty list\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'jobs list is empty', got: %s\n", output);
        return 1;
    }
}

int test_fg_nonexistent() {
    printf("Test: fg nonexistent job\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {"fg 99", "quit"};
    
    run_smash_commands(commands, 2, output, BUFFER_SIZE, 5);
    
    if (strstr(output, "does not exist") != NULL) {
        printf("  PASSED: fg reports nonexistent job\n");
        return 0;
    } else {
        printf("  FAILED: Expected 'does not exist', got: %s\n", output);
        return 1;
    }
}

int test_multiple_background() {
    printf("Test: multiple background jobs\n");
    char output[BUFFER_SIZE];
    const char* commands[] = {
        "sleep 100 &",
        "sleep 100 &",
        "sleep 100 &",
        "jobs",
        "quit kill"
    };
    
    run_smash_commands(commands, 5, output, BUFFER_SIZE, 20);
    
    // Count job entries
    int job_count = 0;
    char* p = output;
    while ((p = strstr(p, "sleep")) != NULL) {
        if (strchr(p, '[') || strstr(p - 5, "[")) {
            job_count++;
        }
        p++;
    }
    
    // Should have at least 2-3 jobs
    if (job_count >= 2) {
        printf("  PASSED: multiple background jobs listed (%d)\n", job_count);
        return 0;
    } else {
        printf("  FAILED: Expected 3 jobs, found %d: %s\n", job_count, output);
        return 1;
    }
}

int main() {
    printf("=== Test 3: Jobs Management Tests ===\n\n");
    
    int failures = 0;
    
    failures += test_jobs_empty();
    failures += test_jobs_with_background();
    failures += test_kill_job();
    failures += test_kill_nonexistent();
    failures += test_kill_invalid_args();
    failures += test_fg_empty_list();
    failures += test_fg_nonexistent();
    failures += test_multiple_background();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
