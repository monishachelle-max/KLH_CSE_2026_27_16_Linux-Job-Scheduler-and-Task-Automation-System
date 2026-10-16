#define _XOPEN_SOURCE 700

#include "menu.h"

#include "job_manager.h"
#include "job_storage.h"
#include "process_manager.h"
#include "scheduler.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INPUT_SIZE 512
#define INPUT_ERROR -2

static int read_line(char *buffer, size_t buffer_size)
{
    size_t length = 0;
    int too_long = 0;
    int invalid_character = 0;
    int carriage_return = 0;
    ssize_t bytes_read = 0;

    for (;;) {
        struct pollfd input;
        int wait_ms;
        int ready;
        char character;

        if (scheduler_run_due() != 0) {
            return INPUT_ERROR;
        }
        wait_ms = scheduler_milliseconds_until_next();
        input.fd = STDIN_FILENO;
        input.events = POLLIN;
        input.revents = 0;
        ready = poll(&input, 1, wait_ms);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Cannot wait for input");
            return INPUT_ERROR;
        }
        if (ready == 0) {
            continue;
        }
        if ((input.revents & (POLLERR | POLLNVAL)) != 0) {
            fprintf(stderr, "Input stream reported an error.\n");
            return INPUT_ERROR;
        }
        if ((input.revents & (POLLIN | POLLHUP)) == 0) {
            continue;
        }

        bytes_read = read(STDIN_FILENO, &character, 1);
        if (bytes_read == 0) {
            if (length == 0 && !too_long) {
                return 0;
            }
            break;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Cannot read input");
            return INPUT_ERROR;
        }
        if (character == '\n') {
            break;
        }
        if (carriage_return) {
            invalid_character = 1;
        }
        if (character == '\r') {
            carriage_return = 1;
            continue;
        }
        if ((unsigned char)character < 32 || (unsigned char)character == 127) {
            invalid_character = 1;
            continue;
        }
        if (length + 1 < buffer_size) {
            buffer[length++] = character;
        } else {
            too_long = 1;
        }
    }
    buffer[length] = '\0';
    if (too_long) {
        fprintf(stderr, "Input is too long; please try again.\n");
        return -1;
    }
    if (invalid_character || (carriage_return && bytes_read == 0)) {
        fprintf(stderr, "Input cannot contain embedded control characters.\n");
        return -1;
    }
    return 1;
}

static int prompt_for_line(const char *prompt, char *buffer, size_t size)
{
    int result;

    fputs(prompt, stdout);
    fflush(stdout);
    result = read_line(buffer, size);
    if (result < 0) {
        putchar('\n');
    }
    return result;
}

static int parse_scheduled_time(const char *input, time_t *scheduled_at)
{
    struct tm requested;
    struct tm original;
    struct tm normalized;
    char *end;
    time_t parsed;
    size_t index;

    if (strlen(input) != 19) {
        return -1;
    }
    for (index = 0; index < 19; ++index) {
        char separator = '\0';
        if (index == 4 || index == 7) {
            separator = '-';
        } else if (index == 10) {
            separator = ' ';
        } else if (index == 13 || index == 16) {
            separator = ':';
        }
        if ((separator != '\0' && input[index] != separator) ||
            (separator == '\0' && (input[index] < '0' || input[index] > '9'))) {
            return -1;
        }
    }
    memset(&requested, 0, sizeof(requested));
    requested.tm_isdst = -1;
    end = strptime(input, "%Y-%m-%d %H:%M:%S", &requested);
    if (end == NULL) {
        return -1;
    }
    while (*end == ' ') {
        ++end;
    }
    if (*end != '\0') {
        return -1;
    }
    original = requested;
    parsed = mktime(&requested);
    if (parsed == (time_t)-1 || localtime_r(&parsed, &normalized) == NULL ||
        normalized.tm_year != original.tm_year ||
        normalized.tm_mon != original.tm_mon ||
        normalized.tm_mday != original.tm_mday ||
        normalized.tm_hour != original.tm_hour ||
        normalized.tm_min != original.tm_min ||
        normalized.tm_sec != original.tm_sec ||
        parsed <= time(NULL)) {
        return -1;
    }
    *scheduled_at = parsed;
    return 0;
}

static void print_scheduled_time(time_t scheduled_at)
{
    struct tm local_time;
    char formatted[32];

    if (localtime_r(&scheduled_at, &local_time) == NULL ||
        strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S",
                 &local_time) == 0) {
        printf("unknown time");
        return;
    }
    printf("%s", formatted);
}

static void show_jobs(void)
{
    size_t index;

    if (job_manager_count() == 0) {
        puts("No scheduled jobs.");
        return;
    }
    puts("\nID   Scheduled time       Status     Program");
    for (index = 0; index < job_manager_count(); ++index) {
        const Job *job = job_manager_get(index);
        if (job == NULL) {
            continue;
        }
        printf("%-4lu ", job->id);
        print_scheduled_time(job->scheduled_at);
        printf("  %-10s %s\n", job_status_name(job->status), job->command);
    }
}

static int read_job_id(unsigned long *job_id)
{
    char input[INPUT_SIZE];
    char *end;
    unsigned long parsed;
    int result = prompt_for_line("Job ID: ", input, sizeof(input));

    if (result <= 0) {
        return result;
    }
    if (input[0] < '0' || input[0] > '9') {
        fprintf(stderr, "Enter a positive numeric job ID.\n");
        return -1;
    }
    errno = 0;
    parsed = strtoul(input, &end, 10);
    if (errno != 0 || end == input || *end != '\0' || parsed == 0) {
        fprintf(stderr, "Enter a positive numeric job ID.\n");
        return -1;
    }
    *job_id = parsed;
    return 1;
}

static int schedule_task(void)
{
    char program_input[INPUT_SIZE];
    char *end;
    unsigned long selection;
    const char *command;
    char message[JOB_ARGUMENTS_SIZE] = "";
    char error[160];
    char time_input[INPUT_SIZE];
    time_t scheduled_at;
    unsigned long job_id;
    int result;

    process_manager_print_approved_commands();
    result = prompt_for_line("Select program: ", program_input,
                             sizeof(program_input));
    if (result <= 0) {
        return result;
    }
    errno = 0;
    selection = strtoul(program_input, &end, 10);
    if (errno != 0 || end == program_input || *end != '\0' ||
        program_input[0] < '0' || program_input[0] > '9' ||
        (command = process_manager_program(selection)) == NULL) {
        fprintf(stderr, "Invalid program selection. Choose a number from the list.\n");
        return -1;
    }
    if (strcmp(command, "echo") == 0) {
        result = prompt_for_line("Enter message: ", message, sizeof(message));
        if (result <= 0) {
            return result;
        }
        if (process_manager_validate(command, message, error, sizeof(error)) != 0) {
            fprintf(stderr, "%s\n", error);
            return -1;
        }
    }
    result = prompt_for_line("\nEnter scheduled date and time (YYYY-MM-DD HH:MM:SS):\n",
                             time_input, sizeof(time_input));
    if (result <= 0) {
        return result;
    }
    if (parse_scheduled_time(time_input, &scheduled_at) != 0) {
        fprintf(stderr,
                "Invalid scheduled time. Use a real local time in the future.\n");
        return -1;
    }
    if (job_manager_schedule(scheduled_at, command, message, &job_id) != 0) {
        return -1;
    }
    printf("\nJob scheduled successfully!\nJob ID: %lu\nProgram: %s\nStatus: PENDING\n",
           job_id, command);
    return 1;
}

static int show_job_status_and_logs(void)
{
    unsigned long job_id;
    const Job *job;
    int result = read_job_id(&job_id);

    if (result <= 0) {
        return result;
    }
    job = job_manager_find(job_id);
    if (job == NULL) {
        fprintf(stderr, "Job %lu was not found.\n", job_id);
        return -1;
    }
    printf("Job %lu: %s; program: %s; scheduled: ",
           job->id, job_status_name(job->status), job->command);
    print_scheduled_time(job->scheduled_at);
    if (job->status == JOB_SUCCEEDED || job->status == JOB_FAILED) {
        printf("; exit code: %d", job->exit_code);
    }
    puts("\nLog:");
    (void)job_storage_show_log(job_id);
    return 1;
}

static int cancel_pending_task(void)
{
    unsigned long job_id;
    int result = read_job_id(&job_id);

    if (result <= 0) {
        return result;
    }
    if (job_manager_cancel(job_id) == 0) {
        printf("Job %lu cancelled.\n", job_id);
    }
    return 1;
}

static int exit_scheduler(int result)
{
    puts("\nScheduler stopped. Pending jobs remain saved but will NOT run until you restart this application.");
    return result;
}

int menu_run(void)
{
    if (job_manager_load() != 0) {
        return 1;
    }

    for (;;) {
        char input[INPUT_SIZE];
        int result;

        puts("\nLinux Job Scheduler");
        puts("1. Schedule a Task");
        puts("2. View Scheduled Jobs");
        puts("3. View Job Status and Logs");
        puts("4. Cancel a Pending Task");
        puts("5. Exit");
        result = prompt_for_line("Select an option: ", input, sizeof(input));
        if (result == 0) {
            putchar('\n');
            return exit_scheduler(0);
        }
        if (result == INPUT_ERROR) {
            return exit_scheduler(1);
        }
        if (result < 0) {
            continue;
        }
        if (strcmp(input, "1") == 0) {
            result = schedule_task();
        } else if (strcmp(input, "2") == 0) {
            show_jobs();
        } else if (strcmp(input, "3") == 0) {
            result = show_job_status_and_logs();
        } else if (strcmp(input, "4") == 0) {
            result = cancel_pending_task();
        } else if (strcmp(input, "5") == 0) {
            return exit_scheduler(0);
        } else {
            fprintf(stderr, "Invalid selection. Choose a number from 1 to 5.\n");
        }
        if (result == INPUT_ERROR) {
            return exit_scheduler(1);
        }
    }
}
