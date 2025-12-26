#include "jobs.h"
#include "my_system_call.h"
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <algorithm>
#include <sys/wait.h>
#include <unistd.h>
using namespace std;

JobList g_job_list;

// ======================================
// Job class Implementation
// ======================================

/**
 * @brief Constructs a new Job object.
 * @param jobId Each job's unique ID
 * @param pid The process's ID (PID) which is assigned by the OS.
 * @param cmdLine The original command string entered by the user.
 * @param startTime The timestamp of which the job first started at.
 * @param status The current state of the job (Running / Stopped)
 */
Job::Job(int jobId,
         pid_t pid,
         const string& cmdLine,
         time_t startTime,
         JobStatus status)
    : m_id(jobId),
      m_pid(pid),
      m_cmdLine(cmdLine),
      m_startTime(startTime),
      m_status(status)
{}

// Getters and Setters
/** @return The small int ID of the job */
int Job::getId() const {return m_id;}

/** @return The OS process ID (PID) */
pid_t Job::getPid() const {return m_pid;}

/** @return The command string of a job */
const string& Job::getCommand() const {return m_cmdLine;}

/** @return The timestamp of when the job first started */
time_t Job::getStartTime() const {return m_startTime;}

/** @return The current status of the job */
JobStatus Job::getStatus() const {return m_status;}

/**
 * @brief Updates the status of the job.
 * @param newStatus The new status to assign (For example - changing the job from Running to Stopped).
 */
void Job::setStatus(JobStatus newStatus) {m_status = newStatus;}

// ======================================
// JobList Class Implementation
// ======================================

/**
 * @brief Constructs and empty JobList.
 */
JobList::JobList() : m_jobs(), m_nextID(1) {}

/**
 * @brief Finds the smallest unsued positive int ID.
 * This function iterates starting from 0 and checks if that ID is currently assigned to any job in the list.
 * It returns the first ID that is free.
 * @return The allocated Job ID.
 */
int JobList::allocateJobId() {
    int id = 0;
    while (true) {
        bool exists = false;
        for (const auto& job : m_jobs) {
            if (job.getId() == id) {
                exists = true;
                break;
            }
        }
        if (!exists) return id;
        id++;
    }
}

/**
 * @brief Adds a new job to the Job List.
 * Allocated a new PID to each job, creates the job object and adds it to the list.
 * Keeps the list sorted by Job ID.
 * @param pid Each job's pricess ID.
 * @param cmdLine The command string.
 * @param startTime Each job's starting time.
 * @param isStopped True if the job is created in a 'stopped' state.
 * @return The ID assigned to the new job.
 */
int JobList::addJob(pid_t pid, const string& cmdLine, time_t startTime, bool isStopped) {
    int id = allocateJobId();
    JobStatus stat = isStopped ? JobStatus::Stopped : JobStatus::Running;
    m_jobs.emplace_back(id, pid, cmdLine, startTime, stat);
    std::sort(m_jobs.begin(), m_jobs.end(), [](const Job& a, const Job& b) {
        return a.getId() < b.getId();
    });
    return id;
}

/**
 * @brief Prints the Job List by order.
 * Also calculates the time run.
 */
void JobList::printJobList() const {
    time_t now = time(nullptr);
    for (const Job& job : m_jobs) {
        double seconds = difftime(now, job.getStartTime());
        long secs = static_cast<long>(seconds);
        cout << "[" << job.getId() << "] " << job.getCommand() << " : " << job.getPid() << " " << secs << " secs";
        if (job.getStatus() == JobStatus::Stopped) {
            cout << " (stopped) ";
        }
        cout << endl;
    }
}

/**
 * @brief Searches for a job by its ID.
 * @param jobId The ID to search for.
 * @return Pointer to the job object (NULL if doesn't exist).
 */
Job* JobList::getJobById(int jobId) {
    for (Job& job : m_jobs) {
        if (job.getId() == jobId) {
            return &job;
        }
    }
    return nullptr;
}

/**
 * @brief Searches for a job by its PID (that the OS gives it)
 * @param pid The PID to search for.
 * @return Pointer to the job object (NULL if doesn't exist).
 */
Job* JobList::getJobByPid(pid_t pid) {
    for (Job& job : m_jobs) {
        if (job.getPid() == pid) {
            return &job;
        }
    }
    return nullptr;
}

/**
 * @brief Finds the last job in the list.
 * (FOR 'fg' COMMAND)
 * @return Pointer to the job object (NULL if doesn't exist).
 */
Job* JobList::getLastJob() {
    if (m_jobs.empty()) {
        return nullptr;
    }
    else {
        return &m_jobs.back();
    }
}

/**
 * @brief Finds the last job that has been stopped.
 * (FOR 'bg' COMMAND)
 * @return Pointer to that specific job object (NULL if doesn't exist)
 */
Job* JobList::getLastStoppedJob() {
    if (m_jobs.empty()) {
        return nullptr;
    }
    for (int i = static_cast<int>(m_jobs.size()) - 1; i >= 0; --i) {
        if (m_jobs[i].getStatus() == JobStatus::Stopped) {
            return &m_jobs[i];
        }
    }
    return nullptr;
}

/**
 * @brief Removes a job object by it's ID.
 * @param jobId The ID of the job to remove.
 * @return True if the job has been removed - False otherwise.
 */
bool JobList::removeJobById(int jobId) {
    for (size_t i = 0; i < m_jobs.size(); i++) {
        if (m_jobs[i].getId() == jobId) {
            m_jobs.erase(m_jobs.begin() + i);
            return true;
        }
    }
    return false;
}

/**
 * @brief Removes a job object by it's PID.
 * @param pid The PID of the job to remove.
 * @return True if the job has been removed - False otherwise.
 */
bool JobList::removeJobByPid(pid_t pid) {
    for (size_t i = 0; i < m_jobs.size(); i++) {
        if (m_jobs[i].getPid() == pid) {
            m_jobs.erase(m_jobs.begin() + i);
            return true;
        }
    }
    return false;
}

/**
 * @brief Searches the job list and removes any process that has been terminated.
 * If a process has finished running (or terminated), it is removed from the list.
 */
void JobList::removeFinishedJobs() {
    if (m_jobs.empty()) {
        return;
    }
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ) {
        pid_t jobPid = it->getPid();
        int status;
        // Check if finished OR if waitpid failed (child gone)
        pid_t res = (pid_t)my_system_call(SYS_WAITPID, jobPid, &status, WNOHANG);

        if (res > 0 || (res == -1 && errno == ECHILD)) {
            // Job finished or process no longer exists -> Remove it
            it = m_jobs.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * @brief Terminates all jobs in the list.
 * Sends SIGTERM to all jobs.
 * After a few seconds that all the jobs has been terminated, it sends out SIGKILL to any jobs that are still remaining.
 * Clears the job list.
 */
void JobList::killAllJobs() {
    printf("smash: sending SIGKILL...\n");
    for (const auto& job : m_jobs) {
        pid_t pid = job.getPid();
        int jobId = job.getId();
        string cmd = job.getCommand();
        printf("[%d] %s - sending SIGTERM... ", jobId, cmd.c_str());
        fflush(stdout);
        my_system_call(SYS_KILL, pid, SIGTERM);
        bool killed = false;
        for (int i = 0; i < 5; i++) {
            int status;
            pid_t result = (pid_t)my_system_call(SYS_WAITPID, pid, &status, WNOHANG);
            if (result == pid) {
                killed = true;
                break;
            }
            sleep(1);
        }
        if (killed) {
            printf("done.\n");
        } else {
            printf("(5 sec passed) sending SIGKILL... ");
            fflush(stdout);
            my_system_call(SYS_KILL, pid, SIGKILL);
            printf("done.\n");
        }
    }
    m_jobs.clear();
}