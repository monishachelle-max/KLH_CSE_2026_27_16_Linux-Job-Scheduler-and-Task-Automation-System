#include "job_manager.h"
#include "job_storage.h"
#include "process_manager.h"
#include "scheduler.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int fail_fork;
static int fail_exec = 1;
static int fail_exit_log;
static int fail_thread_create;

pid_t __real_fork(void);
int __real_execv(const char *path, char *const argv[]);
int __real_job_storage_log(unsigned long id, const char *message);
int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                          void *(*start_routine)(void *), void *argument);

int __wrap_job_storage_log(unsigned long id, const char *message)
{
    if (fail_exit_log && strstr(message, "Process exited with status") != NULL)
    {
        fprintf(stderr, "Controlled failure: cannot append process exit log.\n");
        return -1;
    }
    return __real_job_storage_log(id, message);
}

pid_t __wrap_fork(void)
{
    if (fail_fork)
    {
        errno = EAGAIN;
        return -1;
    }
    return __real_fork();
}

int __wrap_execv(const char *path, char *const argv[])
{
    if (fail_exec)
    {
        errno = ENOENT;
        return -1;
    }
    return __real_execv(path, argv);
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                          void *(*start_routine)(void *), void *argument)
{
    if (fail_thread_create)
    {
        return EAGAIN;
    }
    return __real_pthread_create(thread, attributes, start_routine, argument);
}

static int log_contains(unsigned long id, const char *text)
{
    char path[64];
    char line[512];
    FILE *file;
    int found = 0;

    (void)snprintf(path, sizeof(path), "logs/job_%lu.log", id);
    file = fopen(path, "r");
    if (file == NULL)
    {
        perror("Cannot open failure-test log");
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (strstr(line, text) != NULL)
        {
            found = 1;
        }
    }
    fclose(file);
    return found;
}

int main(void)
{
    const Job *job;
    int status;

    /* Linker wrappers simulate failures only in this test executable. */
    if (job_manager_load() != 0 || scheduler_run_due() != 0)
    {
        return 1;
    }
    job = job_manager_find(1);
    if (job == NULL || job->status != JOB_FAILED || job->exit_code != 127 ||
        !log_contains(1, "Cannot execute date:") ||
        !log_contains(1, "Process exited with status 127."))
    {
        fprintf(stderr, "execv failure was not recorded correctly.\n");
        return 1;
    }
    if (job_manager_update(1, JOB_RUNNING, 0) == 0 ||
        job_manager_cancel(1) == 0)
    {
        fprintf(stderr, "A finished job accepted an invalid transition.\n");
        return 1;
    }
    if (waitpid(-1, &status, WNOHANG) != -1 || errno != ECHILD)
    {
        fprintf(stderr, "Child was not reaped by waitpid.\n");
        return 1;
    }
    fail_fork = 1;
    sleep(3);
    if (scheduler_run_due() != -1)
    {
        fprintf(stderr, "fork failure did not propagate a scheduler error.\n");
        return 1;
    }
    job = job_manager_find(2);
    if (job == NULL || job->status != JOB_FAILED || job->exit_code != 1 ||
        !log_contains(2, "Failed to fork the job process."))
    {
        fprintf(stderr, "fork failure was not reported correctly.\n");
        return 1;
    }
    fail_fork = 0;
    fail_exec = 0;
    fail_exit_log = 1;
    sleep(3);
    if (scheduler_run_due() != -1)
    {
        fprintf(stderr, "Log failure did not propagate a scheduler error.\n");
        return 1;
    }
    job = job_manager_find(3);
    if (job == NULL || job->status != JOB_SUCCEEDED || job->exit_code != 0 ||
        !log_contains(3, "Succeeded (exit code 0)"))
    {
        fprintf(stderr, "Log failure overwrote the real process exit code.\n");
        return 1;
    }
    fail_thread_create = 1;
    sleep(3);
    if (scheduler_run_due() != -1)
    {
        fprintf(stderr, "Worker-thread creation failure did not propagate.\n");
        return 1;
    }
    job = job_manager_find(4);
    if (job == NULL || job->status != JOB_FAILED || job->exit_code != 1 ||
        !log_contains(4, "Failed to create worker thread:"))
    {
        fprintf(stderr, "Worker-thread creation failure was not recorded.\n");
        return 1;
    }
    puts("Controlled execv/fork/thread-creation failures, exit-code preservation and child-reaping checks passed.");
    return 0;
}
