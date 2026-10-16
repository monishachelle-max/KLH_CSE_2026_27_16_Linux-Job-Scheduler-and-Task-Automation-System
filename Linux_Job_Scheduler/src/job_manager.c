#include "job_manager.h"

#include "job_storage.h"
#include "process_manager.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static Job jobs[JOB_MAX_COUNT];
static size_t jobs_used;
static unsigned long next_job_id = 1;

static int validate_job_record(const Job *job, char *error,
                               size_t error_size)
{
    if (job == NULL) {
        (void)snprintf(error, error_size, "Job record is missing.");
        return -1;
    }
    if (job->id == 0 || job->scheduled_at <= (time_t)0) {
        (void)snprintf(error, error_size,
                       "Job record has an invalid ID or scheduled time.");
        return -1;
    }
    if (memchr(job->command, '\0', sizeof(job->command)) == NULL ||
        job->command[0] == '\0' ||
        memchr(job->arguments, '\0', sizeof(job->arguments)) == NULL) {
        (void)snprintf(error, error_size,
                       "Job record contains an unterminated or empty string.");
        return -1;
    }
    if (job->status < JOB_PENDING || job->status > JOB_CANCELLED ||
        job->exit_code < 0) {
        (void)snprintf(error, error_size,
                       "Job record has an invalid status or exit code.");
        return -1;
    }
    if ((job->status == JOB_SUCCEEDED && job->exit_code != 0) ||
        ((job->status == JOB_PENDING || job->status == JOB_RUNNING ||
          job->status == JOB_CANCELLED) &&
         job->exit_code != 0) ||
        (job->status == JOB_FAILED && job->exit_code == 0)) {
        (void)snprintf(error, error_size,
                       "Job record status and exit code are inconsistent.");
        return -1;
    }
    return 0;
}

int job_manager_load(void)
{
    int recovered[JOB_MAX_COUNT] = {0};
    size_t index;
    int recovered_running_job = 0;

    if (job_storage_load(jobs, JOB_MAX_COUNT, &jobs_used, &next_job_id) != 0) {
        return -1;
    }
    for (index = 0; index < jobs_used; ++index) {
        char error[128];
        if (validate_job_record(&jobs[index], error, sizeof(error)) != 0) {
            fprintf(stderr, "Job %lu in the database is invalid: %s\n",
                    jobs[index].id, error);
            return -1;
        }
        if (process_manager_validate(jobs[index].command,
                                    jobs[index].arguments,
                                    error, sizeof(error)) != 0) {
            fprintf(stderr, "Job %lu in the database is invalid: %s\n",
                    jobs[index].id, error);
            return -1;
        }
        if (jobs[index].status == JOB_RUNNING) {
            jobs[index].status = JOB_FAILED;
            jobs[index].exit_code = 1;
            recovered[index] = 1;
            recovered_running_job = 1;
        }
    }
    if (recovered_running_job) {
        if (job_storage_save(jobs, jobs_used) != 0) {
            return -1;
        }
        for (index = 0; index < jobs_used; ++index) {
            if (recovered[index]) {
                char message[128];
                (void)snprintf(message, sizeof(message),
                               "Job %lu was marked failed after an interrupted run.",
                               jobs[index].id);
                if (job_storage_log(jobs[index].id, message) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

int job_manager_schedule(time_t scheduled_at, const char *command,
                         const char *message, unsigned long *job_id)
{
    Job job;
    char error[128];

    if (job_id == NULL) {
        fprintf(stderr, "Cannot schedule a job without an ID output.\n");
        return -1;
    }
    if (jobs_used >= JOB_MAX_COUNT) {
        fprintf(stderr, "The job limit of %d has been reached.\n",
                JOB_MAX_COUNT);
        return -1;
    }
    if (scheduled_at <= time(NULL)) {
        fprintf(stderr, "Scheduled time must be in the future.\n");
        return -1;
    }
    if (process_manager_validate(command, message, error, sizeof(error)) !=
        0) {
        fprintf(stderr, "%s\n", error);
        return -1;
    }
    if (next_job_id == 0 || next_job_id == ULONG_MAX) {
        fprintf(stderr, "No more unique job IDs are available.\n");
        return -1;
    }

    memset(&job, 0, sizeof(job));
    job.id = next_job_id;
    job.scheduled_at = scheduled_at;
    job.status = JOB_PENDING;
    memcpy(job.command, command, strlen(command) + 1);
    (void)snprintf(job.arguments, sizeof(job.arguments), "%s",
                   message);
    if (validate_job_record(&job, error, sizeof(error)) != 0) {
        fprintf(stderr, "Cannot schedule invalid job: %s\n", error);
        return -1;
    }
    jobs[jobs_used] = job;

    if (job_storage_save(jobs, jobs_used + 1) != 0) {
        memset(&jobs[jobs_used], 0, sizeof(jobs[jobs_used]));
        return -1;
    }
    jobs_used++;
    *job_id = next_job_id;
    next_job_id++;
    puts("Job record validated successfully.");
    return 0;
}

size_t job_manager_count(void)
{
    return jobs_used;
}

const Job *job_manager_get(size_t index)
{
    if (index >= jobs_used) {
        return NULL;
    }
    return &jobs[index];
}

const Job *job_manager_find(unsigned long job_id)
{
    size_t index;

    for (index = 0; index < jobs_used; ++index) {
        if (jobs[index].id == job_id) {
            return &jobs[index];
        }
    }
    return NULL;
}

int job_manager_update(unsigned long job_id, JobStatus status, int exit_code)
{
    size_t index;
    Job previous;
    int valid_transition;

    for (index = 0; index < jobs_used; ++index) {
        if (jobs[index].id == job_id) {
            break;
        }
    }
    if (index == jobs_used) {
        fprintf(stderr, "Job %lu was not found.\n", job_id);
        return -1;
    }
    valid_transition =
        (jobs[index].status == JOB_PENDING &&
         (status == JOB_RUNNING || status == JOB_CANCELLED)) ||
        (jobs[index].status == JOB_RUNNING &&
         (status == JOB_SUCCEEDED || status == JOB_FAILED));
    if (!valid_transition) {
        fprintf(stderr, "Invalid status transition for job %lu.\n", job_id);
        return -1;
    }

    previous = jobs[index];
    jobs[index].status = status;
    jobs[index].exit_code = exit_code;
    {
        char error[128];
        if (validate_job_record(&jobs[index], error, sizeof(error)) != 0) {
            jobs[index] = previous;
            fprintf(stderr, "Cannot update invalid job: %s\n", error);
            return -1;
        }
    }
    if (job_storage_save(jobs, jobs_used) != 0) {
        jobs[index] = previous;
        return -1;
    }
    return 0;
}

int job_manager_cancel(unsigned long job_id)
{
    const Job *job = job_manager_find(job_id);
    char message[128];

    if (job == NULL) {
        fprintf(stderr, "Job %lu was not found.\n", job_id);
        return -1;
    }
    if (job->status != JOB_PENDING) {
        fprintf(stderr, "Only pending jobs can be cancelled.\n");
        return -1;
    }
    if (job_manager_update(job_id, JOB_CANCELLED, 0) != 0) {
        return -1;
    }
    (void)snprintf(message, sizeof(message), "Job %lu was cancelled.", job_id);
    if (job_storage_log(job_id, message) != 0) {
        return -1;
    }
    return 0;
}

const char *job_status_name(JobStatus status)
{
    switch (status) {
    case JOB_PENDING:
        return "Pending";
    case JOB_RUNNING:
        return "Running";
    case JOB_SUCCEEDED:
        return "Succeeded";
    case JOB_FAILED:
        return "Failed";
    case JOB_CANCELLED:
        return "Cancelled";
    }
    return "Unknown";
}
