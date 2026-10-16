#include "job_storage.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define JOB_DATABASE_PATH "data/jobs.db"
#define JOB_DATABASE_TEMP_PATH "data/jobs.db.tmp"

static int ensure_directory(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
    {
        return 0;
    }
    return -1;
}

static int ensure_directories(void)
{
    if (ensure_directory("data") != 0 || ensure_directory("logs") != 0)
    {
        return -1;
    }
    return 0;
}

static int parse_unsigned_long(const char *text, unsigned long *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        text[0] < '0' || text[0] > '9')
    {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int parse_long_long(const char *text, long long *value)
{
    char *end = NULL;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
    {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int parse_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX)
    {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

int job_storage_load(Job jobs[], size_t capacity, size_t *count,
                     unsigned long *next_id)
{
    FILE *file;
    char line[1024];
    unsigned long largest_id = 0;

    if (jobs == NULL || count == NULL || next_id == NULL) {
        fprintf(stderr, "Cannot load jobs into invalid storage arguments.\n");
        return -1;
    }
    *count = 0;
    *next_id = 1;
    if (ensure_directories() != 0)
    {
        perror("Cannot create data and logs directories");
        return -1;
    }

    file = fopen(JOB_DATABASE_PATH, "r");
    if (file == NULL)
    {
        if (errno == ENOENT)
        {
            return 0;
        }
        perror("Cannot open job database");
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *fields[6];
        char *cursor;
        unsigned long id;
        long long scheduled_at;
        int status;
        int exit_code;
        size_t field_index;
        Job job;
        size_t index;

        if (strchr(line, '\n') == NULL && !feof(file))
        {
            fprintf(stderr, "Job database contains an overlong record.\n");
            fclose(file);
            return -1;
        }
        line[strcspn(line, "\r\n")] = '\0';
        fields[0] = line;
        cursor = line;
        for (field_index = 0; field_index < 5; ++field_index)
        {
            cursor = strchr(cursor, '\t');
            if (cursor == NULL)
            {
                break;
            }
            *cursor = '\0';
            fields[field_index + 1] = cursor + 1;
            cursor++;
        }
        if (field_index != 5 ||
            parse_unsigned_long(fields[0], &id) != 0 || id == 0 ||
            parse_long_long(fields[1], &scheduled_at) != 0 ||
            parse_int(fields[2], &status) != 0 ||
            status < JOB_PENDING || status > JOB_CANCELLED ||
            parse_int(fields[3], &exit_code) != 0 ||
            fields[4][0] == '\0' ||
            strlen(fields[4]) >= sizeof(job.command) ||
            strlen(fields[5]) >= sizeof(job.arguments) ||
            *count >= capacity)
        {
            fprintf(stderr, "Job database contains an invalid record.\n");
            fclose(file);
            return -1;
        }

        for (index = 0; index < *count; ++index)
        {
            if (jobs[index].id == id)
            {
                fprintf(stderr, "Job database contains duplicate job ID %lu.\n", id);
                fclose(file);
                return -1;
            }
        }
        memset(&job, 0, sizeof(job));
        job.id = id;
        job.scheduled_at = (time_t)scheduled_at;
        job.status = (JobStatus)status;
        job.exit_code = exit_code;
        memcpy(job.command, fields[4], strlen(fields[4]) + 1);
        memcpy(job.arguments, fields[5], strlen(fields[5]) + 1);
        jobs[*count] = job;
        (*count)++;
        if (id > largest_id)
        {
            largest_id = id;
        }
    }

    if (ferror(file))
    {
        perror("Cannot read job database");
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0)
    {
        perror("Cannot close job database");
        return -1;
    }
    if (largest_id == ULONG_MAX)
    {
        fprintf(stderr, "No more unique job IDs are available.\n");
        return -1;
    }
    *next_id = largest_id + 1;
    return 0;
}

int job_storage_save(const Job jobs[], size_t count)
{
    FILE *file;
    size_t index;
    int failed = 0;

    if (count > JOB_MAX_COUNT || (count > 0 && jobs == NULL)) {
        fprintf(stderr, "Cannot save an invalid job record array.\n");
        return -1;
    }
    if (ensure_directories() != 0)
    {
        perror("Cannot create data and logs directories");
        return -1;
    }
    file = fopen(JOB_DATABASE_TEMP_PATH, "w");
    if (file == NULL)
    {
        perror("Cannot write temporary job database");
        return -1;
    }

    for (index = 0; index < count; ++index)
    {
        if (fprintf(file, "%lu\t%lld\t%d\t%d\t%s\t%s\n",
                    jobs[index].id, (long long)jobs[index].scheduled_at,
                    (int)jobs[index].status, jobs[index].exit_code,
                    jobs[index].command, jobs[index].arguments) < 0)
        {
            failed = 1;
            break;
        }
    }
    if (fclose(file) != 0)
    {
        failed = 1;
    }
    if (failed)
    {
        perror("Cannot save job database");
        unlink(JOB_DATABASE_TEMP_PATH);
        return -1;
    }
    if (rename(JOB_DATABASE_TEMP_PATH, JOB_DATABASE_PATH) != 0)
    {
        perror("Cannot replace job database");
        unlink(JOB_DATABASE_TEMP_PATH);
        return -1;
    }
    return 0;
}

int job_storage_log_path(unsigned long job_id, char *path, size_t path_size)
{
    int length = snprintf(path, path_size, "logs/job_%lu.log", job_id);
    if (length < 0 || (size_t)length >= path_size)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int job_storage_log(unsigned long job_id, const char *message)
{
    char path[64];
    char timestamp[32];
    time_t now = time(NULL);
    struct tm local_time;
    FILE *file;

    if (ensure_directories() != 0)
    {
        perror("Cannot create data and logs directories");
        return -1;
    }
    if (job_storage_log_path(job_id, path, sizeof(path)) != 0)
    {
        perror("Cannot build job log path");
        return -1;
    }
    if (localtime_r(&now, &local_time) == NULL ||
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
                 &local_time) == 0)
    {
        fprintf(stderr, "Cannot format job log timestamp.\n");
        return -1;
    }
    file = fopen(path, "a");
    if (file == NULL)
    {
        perror("Cannot open job log");
        return -1;
    }
    if (fprintf(file, "[%s] %s\n", timestamp, message) < 0)
    {
        perror("Cannot write job log");
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0)
    {
        perror("Cannot close job log");
        return -1;
    }
    return 0;
}

int job_storage_show_log(unsigned long job_id)
{
    char path[64];
    char line[512];
    FILE *file;

    if (job_storage_log_path(job_id, path, sizeof(path)) != 0)
    {
        perror("Cannot build job log path");
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL)
    {
        if (errno == ENOENT)
        {
            puts("No log entries for this job yet.");
            return 0;
        }
        perror("Cannot open job log");
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        fputs(line, stdout);
    }
    if (ferror(file))
    {
        perror("Cannot read job log");
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0)
    {
        perror("Cannot close job log");
        return -1;
    }
    return 0;
}
