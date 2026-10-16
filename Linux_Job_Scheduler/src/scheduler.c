#include "scheduler.h"

#include "job_manager.h"
#include "job_storage.h"
#include "process_manager.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    Job job;
    int exit_code;
    int command_started;
    int process_result;
} JobExecution;

static JobExecution worker_execution;
static int worker_join_failed;

static void *run_job_worker(void *argument)
{
    JobExecution *execution = argument;

    execution->process_result =
        process_manager_run(&execution->job, &execution->exit_code,
                            &execution->command_started);
    if (execution->process_result == 0 && execution->command_started) {
        printf("Job %lu executed by worker thread.\n", execution->job.id);
        fflush(stdout);
    }
    return NULL;
}

int scheduler_run_due(void)
{
    size_t index;
    time_t now = time(NULL);
    size_t count = job_manager_count();

    if (worker_join_failed) {
        fprintf(stderr, "Cannot continue after failing to join a job worker.\n");
        return -1;
    }

    for (index = 0; index < count; ++index) {
        const Job *stored_job = job_manager_get(index);
        Job job;
        char message[128];
        int exit_code = 1;
        JobStatus final_status;
        int process_result;
        int thread_error;
        pthread_t worker;

        if (stored_job == NULL || stored_job->status != JOB_PENDING ||
            stored_job->scheduled_at > now) {
            continue;
        }
        job = *stored_job;
        if (job_manager_update(job.id, JOB_RUNNING, 0) != 0) {
            return -1;
        }
        (void)snprintf(message, sizeof(message), "Job %lu is now running.",
                       job.id);
        if (job_storage_log(job.id, message) != 0) {
            if (job_manager_update(job.id, JOB_FAILED, 1) != 0) {
                return -1;
            }
            return -1;
        }
        worker_execution.job = job;
        worker_execution.exit_code = -1;
        worker_execution.command_started = 0;
        worker_execution.process_result = -1;
        thread_error = pthread_create(&worker, NULL, run_job_worker,
                                      &worker_execution);
        if (thread_error != 0) {
            fprintf(stderr, "Cannot create worker thread for job %lu: %s\n",
                    job.id, strerror(thread_error));
            (void)snprintf(message, sizeof(message),
                           "Failed to create worker thread: %s",
                           strerror(thread_error));
            if (job_storage_log(job.id, message) != 0) {
                return -1;
            }
            process_result = -1;
        } else {
            thread_error = pthread_join(worker, NULL);
            if (thread_error != 0) {
                worker_join_failed = 1;
                fprintf(stderr, "Cannot join worker thread for job %lu: %s\n",
                        job.id, strerror(thread_error));
                return -1;
            }
            exit_code = worker_execution.exit_code;
            process_result = worker_execution.process_result;
        }
        if (exit_code < 0) {
            exit_code = 1;
        }
        final_status = exit_code == 0 ? JOB_SUCCEEDED : JOB_FAILED;
        if (job_manager_update(job.id, final_status, exit_code) != 0) {
            return -1;
        }
        (void)snprintf(message, sizeof(message),
                       "Job %lu finished with status %s (exit code %d).",
                       job.id, job_status_name(final_status), exit_code);
        if (job_storage_log(job.id, message) != 0) {
            return -1;
        }
        printf("Job %lu: %s (exit code %d).\n", job.id,
               job_status_name(final_status), exit_code);
        printf("Recorded execution log for job %lu:\n", job.id);
        if (job_storage_show_log(job.id) != 0) {
            return -1;
        }
        fflush(stdout);
        if (process_result != 0) {
            return -1;
        }
    }
    return 0;
}

int scheduler_milliseconds_until_next(void)
{
    size_t index;
    time_t now = time(NULL);
    double shortest_wait = -1.0;

    for (index = 0; index < job_manager_count(); ++index) {
        const Job *job = job_manager_get(index);
        double wait_seconds;

        if (job == NULL || job->status != JOB_PENDING) {
            continue;
        }
        wait_seconds = difftime(job->scheduled_at, now);
        if (wait_seconds <= 0.0) {
            return 0;
        }
        if (shortest_wait < 0.0 || wait_seconds < shortest_wait) {
            shortest_wait = wait_seconds;
        }
    }
    if (shortest_wait < 0.0) {
        return -1;
    }
    if (shortest_wait >= (double)INT_MAX / 1000.0) {
        return INT_MAX;
    }
    return (int)(shortest_wait * 1000.0);
}
