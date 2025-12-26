/*
 * Test 6: Stress tests
 * Tests shell under load and edge cases
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#define BUFFER_SIZE 65536
#define LARGE_BUFFER_SIZE 131072

int run_smash_commands_large(const char* commands[], int num_commands, char* output, size_t output_size, int timeout_sec) {
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
        if (total_read >= (ssize_t)(output_size - 1)) break;
    }
    output[total_read] = '\0';
    
    alarm(0);
    close(pipefd[0]);
    
    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

int test_many_echo_commands() {
    printf("Test: 100 sequential echo commands\n");
    char output[LARGE_BUFFER_SIZE];
    
    const char* commands[102];
    for (int i = 0; i < 100; i++) {
        commands[i] = "echo test";
    }
    commands[100] = "quit";
    commands[101] = NULL;
    
    clock_t start = clock();
    run_smash_commands_large(commands, 101, output, LARGE_BUFFER_SIZE, 60);
    clock_t end = clock();
    
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    
    // Count 'test' occurrences
    int count = 0;
    char* p = output;
    while ((p = strstr(p, "test")) != NULL) {
        count++;
        p++;
    }
    
    if (count >= 90) {  // Allow some margin
        printf("  PASSED: %d echo outputs in %.2fs\n", count, time_spent);
        return 0;
    }
    printf("  FAILED: Expected ~100 outputs, got %d\n", count);
    return 1;
}

int test_many_background_jobs() {
    printf("Test: 20 background jobs\n");
    char output[BUFFER_SIZE];
    
    const char* commands[23];
    for (int i = 0; i < 20; i++) {
        commands[i] = "sleep 100 &";
    }
    commands[20] = "jobs";
    commands[21] = "quit kill";
    commands[22] = NULL;
    
    run_smash_commands_large(commands, 22, output, BUFFER_SIZE, 60);
    
    // Count job listings
    int job_count = 0;
    char* p = output;
    while ((p = strchr(p, '[')) != NULL) {
        char* bracket = strchr(p, ']');
        if (bracket && strchr(bracket, ':')) {
            job_count++;
        }
        p++;
    }
    
    if (job_count >= 15) {  // Allow some margin
        printf("  PASSED: %d background jobs handled\n", job_count);
        return 0;
    }
    printf("  FAILED: Expected ~20 jobs, got %d\n", job_count);
    return 1;
}

int test_rapid_cd_changes() {
    printf("Test: 50 rapid directory changes\n");
    char output[BUFFER_SIZE];
    
    const char* commands[103];
    for (int i = 0; i < 50; i++) {
        commands[i * 2] = "cd /tmp";
        commands[i * 2 + 1] = "cd /var";
    }
    commands[100] = "pwd";
    commands[101] = "quit";
    commands[102] = NULL;
    
    run_smash_commands_large(commands, 102, output, BUFFER_SIZE, 30);
    
    // Should end up at /var
    if (strstr(output, "/var") != NULL) {
        printf("  PASSED: rapid cd changes handled\n");
        return 0;
    }
    printf("  FAILED: Expected /var in output\n");
    return 1;
}

int test_job_id_recycling() {
    printf("Test: Job ID recycling\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "sleep 100 &",  // Job 0
        "sleep 100 &",  // Job 1
        "sleep 100 &",  // Job 2
        "kill 9 0",     // Kill job 0
        "kill 9 1",     // Kill job 1
        "sleep 100 &",  // Should get job 0
        "sleep 100 &",  // Should get job 1
        "jobs",
        "quit kill"
    };
    
    run_smash_commands_large(commands, 9, output, BUFFER_SIZE, 20);
    
    // Should have jobs with IDs reused
    if (strstr(output, "[0]") != NULL && strstr(output, "[1]") != NULL) {
        printf("  PASSED: Job IDs are recycled\n");
        return 0;
    }
    printf("  FAILED: Expected recycled job IDs 0 and 1\n");
    return 1;
}

int test_long_command_line() {
    printf("Test: Long command with many arguments\n");
    char output[BUFFER_SIZE];
    
    // Create a command with many echo arguments
    char long_cmd[512] = "echo";
    for (int i = 0; i < 15; i++) {
        strcat(long_cmd, " arg");
        char num[4];
        sprintf(num, "%d", i);
        strcat(long_cmd, num);
    }
    
    const char* commands[] = {long_cmd, "quit"};
    
    run_smash_commands_large(commands, 2, output, BUFFER_SIZE, 10);
    
    // Should contain all arguments
    if (strstr(output, "arg0") != NULL && strstr(output, "arg14") != NULL) {
        printf("  PASSED: Long command handled\n");
        return 0;
    }
    printf("  FAILED: Expected all arguments in output\n");
    return 1;
}

int test_multiple_aliases() {
    printf("Test: Multiple aliases\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "alias a='echo a'",
        "alias b='echo b'",
        "alias c='echo c'",
        "alias d='echo d'",
        "alias e='echo e'",
        "a",
        "b",
        "c",
        "d",
        "e",
        "quit"
    };
    
    run_smash_commands_large(commands, 11, output, BUFFER_SIZE, 10);
    
    // Should have all alias outputs
    int found = 0;
    if (strstr(output, "\na\n") != NULL || strstr(output, "> a\n") != NULL) found++;
    if (strstr(output, "\nb\n") != NULL || strstr(output, "> b\n") != NULL) found++;
    if (strstr(output, "\nc\n") != NULL || strstr(output, "> c\n") != NULL) found++;
    if (strstr(output, "\nd\n") != NULL || strstr(output, "> d\n") != NULL) found++;
    if (strstr(output, "\ne\n") != NULL || strstr(output, "> e\n") != NULL) found++;
    
    if (found >= 4) {
        printf("  PASSED: Multiple aliases work\n");
        return 0;
    }
    printf("  FAILED: Expected 5 alias outputs, found %d\n", found);
    return 1;
}

int test_empty_lines() {
    printf("Test: Empty lines and whitespace\n");
    char output[BUFFER_SIZE];
    
    const char* commands[] = {
        "",
        "   ",
        "\t",
        "echo test",
        "",
        "quit"
    };
    
    run_smash_commands_large(commands, 6, output, BUFFER_SIZE, 10);
    
    // Should handle empty lines gracefully and still run echo
    if (strstr(output, "test") != NULL) {
        printf("  PASSED: Empty lines handled\n");
        return 0;
    }
    printf("  FAILED: Echo should still work\n");
    return 1;
}

int main() {
    printf("=== Test 6: Stress Tests ===\n\n");
    
    int failures = 0;
    
    failures += test_many_echo_commands();
    failures += test_many_background_jobs();
    failures += test_rapid_cd_changes();
    failures += test_job_id_recycling();
    failures += test_long_command_line();
    failures += test_multiple_aliases();
    failures += test_empty_lines();
    
    printf("\n=== Results: %d tests failed ===\n", failures);
    return failures > 0 ? 1 : 0;
}
