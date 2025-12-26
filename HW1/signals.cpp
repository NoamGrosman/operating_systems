#include <iostream>
#include <unistd.h>
#include <signal.h>
#include "signals.h"
#include "commands.h"
#include "my_system_call.h"
#include "jobs.h"

using namespace std;

/*=============================================================================
* global variables
=============================================================================*/
/**
 * @brief Stores the PID of the process that is currently running in the foreground.
 * 0 indicates no foreground process running.
 */
int g_fg_pid = 0;

/**
 * @brief Stores the command string of the foreground process.
 */
string g_fg_cmd = "";

/*=============================================================================
* signal handlers
=============================================================================*/

/**
 * @brief Handles the SIGTSTP signal (Ctrl + Z)
 * First it logs that the signal was caught.
 * Then checks if there is a process that is running in the foreground.
 * If there is one:
 * - Sends SIGSTOP to stop that process.
 * - Adds the process to the job list (as 'stopped' job)
 * - Resets the forground variable to 0.
 * @param sig_num The signal number (from 'my_system_call' file).
 */
void ctrlZHandler(int sig_num) {
   cout << "smash: caught CTRL+Z" << endl;
   if (g_fg_pid > 0) {
      int res = my_system_call(SYS_KILL, g_fg_pid, SIGSTOP);
      if (res != -1) {
         cout << "smash: process " << g_fg_pid << " was stopped" << endl;
         // The next step saves the job so that we can use it later.
         g_job_list.addJob(g_fg_pid, g_fg_cmd.c_str(), time(nullptr), true);
         g_fg_pid = 0;
         g_fg_cmd = "";
      } else {
         perror("smash error: kill failed");
      }
   }
}

/**
* @brief Handles the SIGINT signal (Ctrl + C)
 * First it logs that the signal was caught.
 * Then checks if there is a process that is running in the foreground.
 * If there is one:
 * - Sends SIGKILL to kill that process.
 * - Resets the foreground variable to 0.
 * @param sig_num The signal number (from 'my_system_call' file).
 */
void ctrlCHandler(int sig_num) {
   cout << "smash: caught CTRL+C" << endl;
   if (g_fg_pid > 0) {
      int res = my_system_call(SYS_KILL, g_fg_pid, SIGKILL);
      if (res != -1) {
         cout << "smash: process " << g_fg_pid << " was killed" << endl;
         g_fg_pid = 0;
         g_fg_cmd = "";
      } else {
         perror("smash error: kill failed");
      }
   }
}