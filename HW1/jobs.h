#ifndef SMASH_JOBS_H
#define SMASH_JOBS_H
#include <string>
#include <vector>
#include <sys/types.h>


enum class JobStatus { Running, Stopped };

/**
 * @brief A single process (job) that is being managed by the SMASH.
 * It stores the metadata of the process.
 */
class Job {
    public:
    /**
     * @brief Constructor of the Job object.
     * @param JobId Shell-assigned ID.
     * @param pid OS-assigned PID.
     * @param cmdLine Command string.
     * @param startTime Starting time of the job object.
     * @param status Initial status of the job.
     */
    Job(int JobId, pid_t pid, const std::string& cmdLine, time_t startTime, JobStatus status);
    int getId() const; /** @return Shell-assigned ID. */
    pid_t getPid() const; /** @return OS-assigned PID. */
    const std::string& getCommand() const; /** @return The command string. */
    time_t getStartTime() const; /** @return Starting time of the job. */
    JobStatus getStatus() const; /** @return Status of the job. */

    /**
     * @brief Updates the status of the job (Running or Stopeed)
     * @param newStatus The new status of the job.
    */
    void setStatus(JobStatus newStatus);

    private:
    int m_id;
    pid_t m_pid;
    std::string m_cmdLine;
    time_t m_startTime;
    JobStatus m_status;
};

/**
 * @brief A list that manages the background and stopped jobs.
 * It handles adding, removing, printing, and retrieving jobs.
 */
class JobList {
    public:
    JobList(); /** @brief Constructor - builds an empty list. */

    /**
     * @brief Adds a new job to the list.
     * @param pid The process ID of the new job.
     * @param cmdLine The command string.
     * @param startTime Starting time of the process creation.
     * @param isStopped True if the initially stopped - False otherwise.
     * @return The allocated job ID.
     */
    int addJob(pid_t pid, const std::string& cmdLine, time_t startTime, bool isStopped);
    void printJobList() const; /** Prints the list of jobs */
    Job* getJobById(int jobId); /** Find a job by ID */
    Job* getJobByPid(pid_t pid); /** Find a job by PID */
    bool removeJobByPid(pid_t pid); /** Remove a job by PID */
    bool removeJobById(int jobId); /** Remove a job by ID */
    Job* getLastJob(); /** Gets the last job added */
    Job* getLastStoppedJob(); /** Gets the last stopped job */
    void removeFinishedJobs(); /** Removes any jobs that have been terminated */
    void killAllJobs(); /** Sends SIGTERM to all jobs to terminate them. */

    private:
    std::vector<Job> m_jobs;
    int m_nextID;
    int allocateJobId();
};

extern JobList g_job_list;
#endif //SMASH_JOBS_H