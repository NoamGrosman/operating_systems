#include "jobs.h"
#include "commands.h"
#include "my_system_call.h"
#include "signals.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>
#include <iostream>

using namespace std;

#define MAX_ARGS 22

std::map<std::string, std::string> g_aliases; // A map used for aliases. (key = "alias_name", value = "command")
std::string checkAndReplaceAlias(const char* cmd_line);
static int isBuiltIn(const char *cmd);
static CommandResult runBuiltIn(char *argv[], const char* cmd_line);
static CommandResult runExternalForeground(char *argv[], const char* cmd_line);

// Stores the previous directory for 'cd -'
static char prev_dir[PATH_MAX];
static int prev_dir_valid = 0;

//example function for printing errors from internal commands
void perrorSmash(const char* cmd, const char* msg)
{
	fprintf(stderr, "smash error:%s%s%s\n",
		cmd ? cmd : "",
		cmd ? ": " : "",
		msg);
}

// Helper function to check if a string is a valid int.
bool isNumber(const char* str) {
	if (!str || *str == '\0') return false;
	if (*str == '-') str++;
	while (*str) {
		if (!isdigit(*str)) return false;
		str++;
	}
	return true;
}

/** @brief Checks if the command is internal */
static int isBuiltIn(const char *cmd) {
	return strcmp(cmd, "quit") == 0 || strcmp(cmd, "showpid") == 0 || strcmp(cmd, "pwd") == 0 ||
		   strcmp(cmd, "cd") == 0 || strcmp(cmd, "jobs") == 0 || strcmp(cmd, "kill") == 0 || strcmp(cmd, "fg") == 0 ||
		   strcmp(cmd, "bg") == 0 || strcmp(cmd, "diff") == 0 || strcmp(cmd, "alias") == 0 || strcmp(cmd, "unalias") == 0;
}

/** @brief Holds every built-in command. Returns Success/Fail/Quit */
static CommandResult runBuiltIn(char *argv[], const char* cmd_line) {
	const char *cmd = argv[0];
	// -- QUIT --
	if (strcmp(cmd, "quit") == 0) {
		if (argv[1] != NULL && argv[2] != NULL) {
			perrorSmash("quit", "expected 0 or 1 arguments");
			return SMASH_FAIL;
		}
		// "quit kill": Kill all jobs before exiting.
		if (argv[1] != NULL) {
			if (strcmp(argv[1], "kill") == 0) {
				g_job_list.killAllJobs();
			} else {
				perrorSmash("quit", "unexpected arguments");
				return SMASH_FAIL;
			}
		}
		return SMASH_QUIT;
	}

	// -- SHOWPID --
	if (strcmp(cmd, "showpid") == 0) {
		if (argv[1] != NULL) {
			perrorSmash("showpid", "expected 0 arguments");
			return SMASH_FAIL;
		}
		printf("smash pid is %d\n", (int)getpid());
		return SMASH_SUCCESS;
	}

	// -- PWD --
	if (strcmp(cmd, "pwd") == 0) {
		if (argv[1] != NULL) {
			perrorSmash("pwd", "expected 0 arguments");
			return SMASH_FAIL;
		}
		char cwd[1024];
		if (!getcwd(cwd, sizeof(cwd))) {
			perrorSmash(" pwd", "getcwd failed");
			return SMASH_FAIL;
		}
		printf("%s\n", cwd);
		return SMASH_SUCCESS;
	}

	// -- JOBS --
	if (strcmp(cmd, "jobs") == 0) {
		if (argv[1] != NULL) {
			perrorSmash(" jobs", "expected 0 arguments");
			return SMASH_FAIL;
		}
		// Cleaning up zombie processes before printing.
		g_job_list.removeFinishedJobs();
		g_job_list.printJobList();
		return SMASH_SUCCESS;
	}

	// -- CD --
	if (strcmp(cmd, "cd") == 0) {
		char cwd[PATH_MAX];
		if (!getcwd(cwd, sizeof(cwd))) {
			perrorSmash(" cd", "getcwd failed");
			return SMASH_FAIL;
		}
		const char *target = argv[1];
		bool is_dash = false;
		// 1 - "cd" with no args -> Go to HOME
		if (target == NULL) {
			target = getenv("HOME");
			if (!target) {
				perrorSmash(" cd", "HOME not set");
				return SMASH_FAIL;
			}
		// 2 - "cd -" -> Go to previous directory.
		} else if (strcmp(target, "-") == 0) {
			if (!prev_dir_valid) {
				perrorSmash(" cd", "OLDPWD not set");
				return SMASH_FAIL;
			}
			target = prev_dir;
			is_dash = true;
		}
		// Attempt to change directory
		if (chdir(target) < 0) {
			// Check why it failed
			struct stat sb;
			if (stat(target, &sb) == -1) {
				perrorSmash("cd", "target directory does not exist");
			} else if (!S_ISDIR(sb.st_mode)) {
				char msg[PATH_MAX + 32];
				snprintf(msg, sizeof(msg), "%s: not a directory", target);
				perrorSmash("cd", msg);
			} else {
				perrorSmash("cd", "chdir failed");
			}
			return SMASH_FAIL;
		}
		// Succeeds - updating the previous directory.
		strncpy(prev_dir, cwd, sizeof(prev_dir));
		prev_dir[sizeof(prev_dir) - 1] = '\0';
		prev_dir_valid = 1;

		// "cd -" - printing new path
		if (is_dash) {
			char new_cwd[PATH_MAX];
			getcwd(new_cwd, sizeof(new_cwd));
			printf("%s\n", new_cwd);
		}
		return SMASH_SUCCESS;
	}

	// -- KILL --
	if (strcmp(cmd, "kill") == 0) {
		if (argv[1] == NULL || argv[2] == NULL || argv[3] != NULL) {
			perrorSmash("kill", "invalid arguments");
			return SMASH_FAIL;
		}
		if (!isNumber(argv[1]) || !isNumber(argv[2])) {
			perrorSmash("kill", "invalid arguments");
			return SMASH_FAIL;
		}
		int signum = atoi(argv[1]);
		int jobId = atoi(argv[2]);
		if (signum < 0) {
			signum = -signum;
		}
		Job* job = g_job_list.getJobById(jobId);
		if (!job) {
			fprintf(stderr, "smash error: kill: job id %d does not exist\n", jobId);
			return SMASH_FAIL;
		}
		pid_t pid = job->getPid();
		if (my_system_call(SYS_KILL, pid, signum) < 0) {
			perrorSmash("kill", "kill failed");
			return SMASH_FAIL;
		}
		printf("signal number %d was sent to pid %d\n", signum, pid);
		g_job_list.removeFinishedJobs();
		return SMASH_SUCCESS;
	}

	// -- FG --
	if (strcmp(cmd, "fg") == 0) {
		int jobId = -1;
		// If there are no args - get the last job.
		// If there are args - get specific job.
		if (argv[1] == NULL) {
			Job* last = g_job_list.getLastJob();
			if (!last) {
				perrorSmash("fg", "job list is empty");
				return SMASH_FAIL;
			}
			jobId = last->getId();
		} else {
			jobId = atoi(argv[1]);
		}
		Job* job = g_job_list.getJobById(jobId);
		if (!job) {
			perrorSmash("fg", "job id does not exist");
			return SMASH_FAIL;
		}
		printf("%s : %d\n", job->getCommand().c_str(), job->getPid());
		my_system_call(SYS_KILL, job->getPid(), SIGCONT); // Sends SIGCONT in case it was stopped earlier.
		int pid = job->getPid(); // Updates globals for Ctrl+C/Z.
		string cmdStr = job->getCommand();
		g_fg_pid = pid;
		g_fg_cmd = cmdStr;
		g_job_list.removeJobById(jobId); // Remove from background list (now it is foreground)
		int status; // Block the smash until this process finishes or stopped.
		my_system_call(SYS_WAITPID, pid, &status, WUNTRACED);
		// Returning global variables to original case.
		g_fg_pid = 0;
		g_fg_cmd = "";
		return SMASH_SUCCESS;
	}

	// -- BG --
	if (strcmp(cmd, "bg") == 0) {
		if (argv[1] != NULL && argv[2] != NULL) {
			perrorSmash("bg", "invalid arguments");
			return SMASH_FAIL;
		}
		Job* job = nullptr;
		int jobId = 0;
		// If there are no args - get last stopped job.
		if (argv[1] == NULL) {
			job = g_job_list.getLastStoppedJob();
			if (!job) {
				perrorSmash("bg", "there are no stopped jobs to resume");
				return SMASH_FAIL;
			}
			jobId = job->getId();
		} else {
			jobId = atoi(argv[1]);
			job = g_job_list.getJobById(jobId);
			if (!job) {
				fprintf(stderr, "smash error: bg: job id %d does not exist\n", jobId);
				return SMASH_FAIL;
			}
			if (job->getStatus() != JobStatus::Stopped) {
				fprintf(stderr, "smash error: bg: job id %d is already in background\n", jobId);
				return SMASH_FAIL;
			}
		}
		printf("%s : %d\n", job->getCommand().c_str(), job->getPid());
		if (my_system_call(SYS_KILL, job->getPid(), SIGCONT) < 0) {
			perrorSmash("bg", "kill failed");
			return SMASH_FAIL;
		}
		job->setStatus(JobStatus::Running);
		return SMASH_SUCCESS;
	}

	// -- DIFF --
	if (strcmp(cmd, "diff") == 0) {
       if (argv[1] == NULL || argv[2] == NULL || argv[3] != NULL) {
          perrorSmash("diff", "expected 2 arguments");
          return SMASH_FAIL;
       }
       const char* file1 = argv[1];
       const char* file2 = argv[2];
       struct stat sb1, sb2;
       if (stat(file1, &sb1) == -1 || stat(file2, &sb2) == -1) {
           perrorSmash("diff", "expected valid paths for files");
           return SMASH_FAIL;
       }
       if (S_ISDIR(sb1.st_mode) || S_ISDIR(sb2.st_mode)) {
           perrorSmash("diff", "paths are not files");
           return SMASH_FAIL;
       }
       int fd1 = (int)my_system_call(SYS_OPEN, file1, O_RDONLY);
       int fd2 = (int)my_system_call(SYS_OPEN, file2, O_RDONLY);
       if (fd1 < 0 || fd2 < 0) {
           if (fd1 >= 0) my_system_call(SYS_CLOSE, fd1);
           if (fd2 >= 0) my_system_call(SYS_CLOSE, fd2);
           perrorSmash("diff", "open failed");
           return SMASH_FAIL;
       }
       unsigned char buf1[1], buf2[1];
       int r1, r2;
       int diff = 0;
       while (true) {
           r1 = (int)my_system_call(SYS_READ, fd1, buf1, 1);
           r2 = (int)my_system_call(SYS_READ, fd2, buf2, 1);
           if (r1 < 0 || r2 < 0) {
               perrorSmash("diff", "read failed");
               my_system_call(SYS_CLOSE, fd1);
               my_system_call(SYS_CLOSE, fd2);
               return SMASH_FAIL;
           }
           if (r1 != r2) {
               diff = 1;
               break;
           }
           if (r1 == 0) {
               break;
           }
           if (buf1[0] != buf2[0]) {
               diff = 1;
               break;
           }
       }
       my_system_call(SYS_CLOSE, fd1);
       my_system_call(SYS_CLOSE, fd2);
       printf("%d\n", diff);
       return SMASH_SUCCESS;
    }

	// -- ALIAS --
	if (strcmp(cmd, "alias") == 0) {
        if (argv[1] == NULL) {
            // No args - Print all aliases
            for (const auto& pair : g_aliases) {
                std::cout << pair.first << "='" << pair.second << "'" << std::endl;
            }
            return SMASH_SUCCESS;
        }

        // Logic to extract key and value.
        std::string fullLine(cmd_line);
        size_t aliasPos = fullLine.find("alias");
        size_t eqPos = fullLine.find('=');
        if (aliasPos == std::string::npos || eqPos == std::string::npos || eqPos < aliasPos) {
             perrorSmash("alias", "invalid format");
             return SMASH_FAIL;
        }

        // Extract key and TRIM spaces manually.
        std::string key = fullLine.substr(aliasPos + 5, eqPos - (aliasPos + 5)); // +5 to skip "alias"

        const auto strBegin = key.find_first_not_of(" \t");
        if (strBegin == std::string::npos) {
             perrorSmash("alias", "invalid format"); // Key is all spaces
             return SMASH_FAIL;
        }
        const auto strEnd = key.find_last_not_of(" \t");
        key = key.substr(strBegin, strEnd - strBegin + 1);

        // Detect quotes (either ' or ") to extract the value.
        size_t qSingle = fullLine.find('\'');
        size_t qDouble = fullLine.find('\"');
        char quoteType = 0;
        size_t quoteStart = std::string::npos;

        if (qSingle != std::string::npos && (qDouble == std::string::npos || qSingle < qDouble)) {
            quoteType = '\'';
            quoteStart = qSingle;
        } else if (qDouble != std::string::npos) {
            quoteType = '\"';
            quoteStart = qDouble;
        }
        if (quoteStart == std::string::npos || quoteStart < eqPos) {
            perrorSmash("alias", "invalid format");
            return SMASH_FAIL;
        }
        size_t quoteEnd = fullLine.rfind(quoteType);
        if (quoteEnd == std::string::npos || quoteEnd <= quoteStart) {
            perrorSmash("alias", "invalid format");
            return SMASH_FAIL;
        }
        std::string value = fullLine.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        if (key.empty() || value.empty()) {
             perrorSmash("alias", "invalid arguments");
             return SMASH_FAIL;
        }
        g_aliases[key] = value;
        return SMASH_SUCCESS;
    }

	// -- UNALIAS --
    if (strcmp(cmd, "unalias") == 0) {
        if (argv[1] == NULL) {
             perrorSmash("unalias", "not enough arguments");
             return SMASH_FAIL;
        }
        std::string key(argv[1]);
        auto it = g_aliases.find(key);
        if (it == g_aliases.end()) {
             perrorSmash("unalias", "not found");
             return SMASH_FAIL;
        }
        g_aliases.erase(it);
        return SMASH_SUCCESS;
    }
	return SMASH_SUCCESS;
}

/** Executes the external command in foreground */
static CommandResult runExternalForeground(char *argv[], const char* cmd_line) {
	pid_t pid = (pid_t)my_system_call(SYS_FORK);
	if (pid < 0) {
		perrorSmash(argv[0], "fork failed");
		return SMASH_FAIL;
	}
	if (pid == 0) { // Child process
		setpgrp();
		my_system_call(SYS_EXECVP, argv[0], argv);
		perrorSmash(argv[0], "exec failed");
		_exit(1);
	}

	// Set global variables for Ctrl+C/Z.
	g_fg_pid = pid;
	g_fg_cmd = cmd_line;

	int status;
	if (my_system_call(SYS_WAITPID, pid, &status, WUNTRACED) < 0) {
		perrorSmash (argv[0], "waitpid failed");
		g_fg_pid = 0;
		return SMASH_FAIL;
	}
	g_fg_pid = 0;
	g_fg_cmd = "";
	if (WIFEXITED(status)) {
		int exit_code = WEXITSTATUS(status);
		if (exit_code != 0) {
			return SMASH_FAIL;
		}
	}
	return SMASH_SUCCESS;
}

/** Executes the external command in background */
static CommandResult runExternalBackground(char *argv[], const char *cmd_line) {
	pid_t pid = (pid_t)my_system_call(SYS_FORK);
	if (pid < 0) {
		perrorSmash(argv[0], "fork failed");
		return SMASH_FAIL;
	}
	if (pid == 0) {
		setpgrp();
		my_system_call(SYS_EXECVP, argv[0], argv);
		perrorSmash(argv[0], "exec failed");
		_exit(1);
	}
	g_job_list.addJob(pid, cmd_line, time(nullptr), false);
	return SMASH_SUCCESS;
}

/** Handles recursive aliasing */
std::string checkAndReplaceAlias(const char* cmd_line) {
	std::string current_cmd = cmd_line;
	int loop_guard = 0; // Prevent infinite loops

	while (loop_guard < 80) {
		size_t firstSpace = current_cmd.find_first_of(" \t\n");
		std::string firstWord;
		if (firstSpace == std::string::npos) {
			firstWord = current_cmd;
		} else {
			firstWord = current_cmd.substr(0, firstSpace);
		}

		auto it = g_aliases.find(firstWord);
		if (it != g_aliases.end()) {
			// Found alias, replace and loop again to check if result is also alias
			std::string replacement = it->second;
			if (firstSpace != std::string::npos) {
				replacement += current_cmd.substr(firstSpace);
			}
			current_cmd = replacement;
			loop_guard++;
		} else {
			// No alias found - finish
			break;
		}
	}
	return current_cmd;
}

/** Main execution function */
CommandResult executeCommand(char* line) {
	// 1 - Expand aliases
	std::string expandedCmd = checkAndReplaceAlias(line);
	char buf[CMD_LENGTH_MAX];
	strncpy(buf,expandedCmd.c_str(),sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';

	// 2 - Look for "&&"
	char* ampersand = strstr(buf, "&&");
	if (ampersand != NULL) {
		// Split the string
		*ampersand = '\0'; // Replace the first '&' with null terminator
		char* cmd1 = buf;
		char* cmd2 = ampersand + 2; // Skip the "&&"

		// Recursively execute the left side
		CommandResult res = executeCommand(cmd1);

		// Only execute the right side if the left succeeded
		if (res == SMASH_SUCCESS) {
			return executeCommand(cmd2);
		}

		return SMASH_FAIL; // Left side failed, so we stop
	}

	// 3 - Parsing
	char *argv[MAX_ARGS];
	int argc = 0;
	char *token = strtok(buf," \t\n");
	while (token && argc < MAX_ARGS) {
		argv[argc++] = token;
		token = strtok(NULL," \t\n");
	}
	argv[argc] = NULL;

	if (argc == 0) {
		return SMASH_SUCCESS;
	}

	// 4 - Check for background execution (&)
	int runInBackground = 0;
	if (argc > 0 && strcmp(argv[argc - 1], "&") == 0) {
		runInBackground = 1;
		argv[argc - 1] = NULL;
		argc--;
	}

	// 5 - Dispatch
	if (isBuiltIn(argv[0])) {
		if (runInBackground) {
			// Built-in running in background
			pid_t pid = fork();
			if (pid == 0) {
				setpgrp();
				exit(0); // Child exits.
			} else if (pid > 0) {
				// Parent adds to job list
				g_job_list.addJob(pid, line, time(nullptr), false);
				return SMASH_SUCCESS;
			} else {
				perrorSmash(argv[0], "fork failed");
				return SMASH_FAIL;
			}
		} else {
			// Normal foreground built-in
			return runBuiltIn(argv, line);
		}
	}

	if (runInBackground) {
		return runExternalBackground(argv, line);
	}

	return runExternalForeground(argv, line);
	// return SMASH_SUCCESS;
}
