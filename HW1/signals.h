#ifndef SIGNALS_H
#define SIGNALS_H

/*=============================================================================
* includes, defines, usings
=============================================================================*/

#include <string>

/*=============================================================================
* global functions
=============================================================================*/

/**
 * @brief This is a global variable that is holding the PID of the process currently running in the foreground.
 * If there is no process running in the foreground - then it will hold a value to indicate 'no active job'.
 */
extern int g_fg_pid;

/**
 * @brief This is a global variable that is holding the command string of the foreground process.
 * It is used for printed different messages.
 */
extern std::string g_fg_cmd;


/**
 * @brief Signal handler for SIGTSTP (Ctrl + Z).
 * When Ctrl + Z is pressed, the OS sends SIGTSTP to the smash - and this function catches it.
 * First it will check if there is a foreground process running.
 * If there is one running, it sends SIGSTOP to that process.
 * Then add the process to the JobList as a 'Stopped' job.
 * After that, reset the global foreground variable.
 * @param sig_num The signal number (From the my_system_call file)
 */
void ctrlZHandler(int sig_num);

/**
* @brief Signal handler for SIGINT (Ctrl + C).
 * When Ctrl + C is pressed, the OS sends SIGINT to the smash - and this function catches it.
 * First it will check if there is a foreground process running.
 * If there is one running, it sends SIGKILL to that process.
 * After that, reset the global foreground variable.
 * @param sig_num The signal number (From the my_system_call file)
 */
void ctrlCHandler(int sig_num);

#endif //__SIGNALS_H__