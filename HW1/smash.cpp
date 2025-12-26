//smash.c

/*=============================================================================
* includes, defines, usings
=============================================================================*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "commands.h"
#include "signals.h"
#include "jobs.h"

#define CMD_LENGTH_MAX 80
#define MAX_ARGS 20

/*=============================================================================
* classes/structs declarations
=============================================================================*/

/*=============================================================================
* global variables & data structures
=============================================================================*/
char _line[CMD_LENGTH_MAX];

/*=============================================================================
* main function
=============================================================================*/
int main(int argc, char* argv[])
{
	struct sigaction sa;
	sa.sa_handler = ctrlCHandler;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);

	sa.sa_handler = ctrlZHandler;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGTSTP, &sa, NULL);

	char _cmd[CMD_LENGTH_MAX];
	while(1) {
		printf("smash > ");
		fflush(stdout);
		if (fgets(_line, CMD_LENGTH_MAX, stdin) == NULL) {
			if (feof(stdin)) {
				printf("\n");
				break;
			} else {
				perror("fgets failed");
				continue;
			}
		}


		strcpy(_cmd, _line);
		_cmd[strcspn(_cmd, "\n")] = '\0';

		if (_cmd[0] == '\0') {
			continue;
		}
		g_job_list.removeFinishedJobs();
		//execute command
		CommandResult res = executeCommand(_cmd);
		if (res == SMASH_QUIT) {
			break;
		}

		//initialize buffers for next command
		_line[0] = '\0';
		_cmd[0] = '\0';
	}

	return 0;
}
