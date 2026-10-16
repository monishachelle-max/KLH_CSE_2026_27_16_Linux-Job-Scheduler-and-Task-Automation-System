#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <stddef.h>
#include <time.h>

#define JOB_MAX_COUNT 100
#define JOB_COMMAND_SIZE 32
#define JOB_ARGUMENTS_SIZE 256

typedef enum {
    JOB_PENDING = 0,
    JOB_RUNNING,
    JOB_SUCCEEDED,
    JOB_FAILED,
    JOB_CANCELLED
} JobStatus;

typedef struct {
    unsigned long id;
    time_t scheduled_at;
    JobStatus status;
    int exit_code;
    char command[JOB_COMMAND_SIZE];
    char arguments[JOB_ARGUMENTS_SIZE];
} Job;

int job_manager_load(void);
int job_manager_schedule(time_t scheduled_at, const char *command,
                         const char *message, unsigned long *job_id);
size_t job_manager_count(void);
const Job *job_manager_get(size_t index);
const Job *job_manager_find(unsigned long job_id);
int job_manager_update(unsigned long job_id, JobStatus status, int exit_code);
int job_manager_cancel(unsigned long job_id);
const char *job_status_name(JobStatus status);

#endif
