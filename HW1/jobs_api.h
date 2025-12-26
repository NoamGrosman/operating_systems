#ifndef SMASH_JOBS_API_H
#define SMASH_JOBS_API_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

    void jobs_add_job(pid_t pid, const char *cmd_line, int is_stopped);
    void jobs_print(void);
    void jobs_remove_finished(void);
    pid_t jobs_get_pid_by_id(int job_id);

#ifdef __cplusplus
}
#endif

#endif // SMASH_JOBS_API_HBS_API_H