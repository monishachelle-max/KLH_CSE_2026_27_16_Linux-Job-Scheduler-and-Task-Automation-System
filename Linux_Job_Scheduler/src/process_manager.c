#include "process_manager.h"

#include "job_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define EXECUTION_MESSAGE_SIZE 128

typedef struct {
    int exit_code;
    int terminating_signal;
    int command_started;
    char message[EXECUTION_MESSAGE_SIZE];
} ExecutionResult;

typedef struct {
    const char *name;
    const char *path;
} ApprovedProgram;

static const ApprovedProgram approved_programs[] = {
    {"date", "/usr/bin/date"},
    {"echo", "/usr/bin/echo"},
    {"true", "/usr/bin/true"},
    {"false", "/usr/bin/false"}
};

static const ApprovedProgram *find_program(const char *name)
{
    size_t index;

    for (index = 0;
         index < sizeof(approved_programs) / sizeof(approved_programs[0]);
         ++index) {
        if (strcmp(name, approved_programs[index].name) == 0) {
            return &approved_programs[index];
        }
    }
    return NULL;
}

static int write_all(int descriptor, const void *buffer, size_t size)
{
    const char *bytes = buffer;
    size_t written = 0;

    while (written < size) {
        ssize_t result = write(descriptor, bytes + written, size - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (result == 0) {
            errno = EIO;
            return -1;
        }
        written += (size_t)result;
    }
    return 0;
}

static int read_all(int descriptor, void *buffer, size_t size)
{
    char *bytes = buffer;
    size_t received = 0;

    while (received < size) {
        ssize_t result = read(descriptor, bytes + received, size - received);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (result == 0) {
            if (received == 0) {
                return 1;
            }
            errno = EPIPE;
            return -1;
        }
        received += (size_t)result;
    }
    return 0;
}

static size_t append_text(char *buffer, size_t capacity, size_t offset,
                          const char *text)
{
    while (*text != '\0' && offset + 1 < capacity) {
        buffer[offset++] = *text++;
    }
    if (capacity > 0) {
        buffer[offset] = '\0';
    }
    return offset;
}

static size_t append_number(char *buffer, size_t capacity, size_t offset,
                            unsigned int number)
{
    char digits[sizeof(number) * 3];
    size_t digit_count = 0;

    do {
        digits[digit_count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);
    while (digit_count > 0 && offset + 1 < capacity) {
        buffer[offset++] = digits[--digit_count];
    }
    if (capacity > 0) {
        buffer[offset] = '\0';
    }
    return offset;
}

static int wait_for_child(pid_t child, int *status)
{
    pid_t result;

    do {
        result = waitpid(child, status, 0);
    } while (result < 0 && errno == EINTR);
    return result < 0 ? -1 : 0;
}

static void set_execution_result(ExecutionResult *result, int wait_status)
{
    size_t offset = 0;

    if (WIFEXITED(wait_status)) {
        result->exit_code = WEXITSTATUS(wait_status);
        result->terminating_signal = 0;
        if (result->exit_code == 0) {
            (void)append_text(result->message, sizeof(result->message), offset,
                              "Job completed successfully.");
        } else {
            offset = append_text(result->message, sizeof(result->message),
                                 offset, "Job failed with exit code ");
            offset = append_number(result->message, sizeof(result->message),
                                   offset, (unsigned int)result->exit_code);
            (void)append_text(result->message, sizeof(result->message), offset,
                              ".");
        }
    } else if (WIFSIGNALED(wait_status)) {
        result->terminating_signal = WTERMSIG(wait_status);
        result->exit_code = 128 + result->terminating_signal;
        offset = append_text(result->message, sizeof(result->message), offset,
                             "Job failed because its process was terminated by signal ");
        offset = append_number(result->message, sizeof(result->message), offset,
                              (unsigned int)result->terminating_signal);
        (void)append_text(result->message, sizeof(result->message), offset, ".");
    } else {
        result->exit_code = 1;
        result->terminating_signal = 0;
        (void)append_text(result->message, sizeof(result->message), offset,
                          "Job failed without a normal exit status.");
    }
}

static void report_child_result(int descriptor,
                                const ExecutionResult *result)
{
    if (write_all(descriptor, result, sizeof(*result)) != 0) {
        _exit(125);
    }
    if (close(descriptor) != 0) {
        _exit(125);
    }
    _exit(0);
}

static void run_job_child(const ApprovedProgram *program,
                          char *const argv[], char *const echo_environment[],
                          size_t program_name_length, int use_execve,
                          const char *log_path,
                          ExecutionResult *result,
                          const int pipe_descriptors[2])
{
    int log_fd;
    pid_t command_child;
    int wait_status;
    int exec_pipe[2];
    int descriptor_flags;
    int launch_error = 0;
    int launch_result;

    if (close(pipe_descriptors[0]) != 0) {
        _exit(125);
    }
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        result->message[0] = '\0';
        (void)append_text(result->message, sizeof(result->message), 0,
                          "Job failed to open its log.");
        result->exit_code = 127;
        result->terminating_signal = 0;
        (void)write(STDERR_FILENO, "Cannot open job log.\n",
                    sizeof("Cannot open job log.\n") - 1);
        report_child_result(pipe_descriptors[1], result);
    }

    if (pipe(exec_pipe) != 0) {
        if (log_fd != STDOUT_FILENO && log_fd != STDERR_FILENO) {
            (void)close(log_fd);
        }
        result->message[0] = '\0';
        (void)append_text(result->message, sizeof(result->message), 0,
                          "Job failed to create its exec-status pipe.");
        result->exit_code = 1;
        result->terminating_signal = 0;
        report_child_result(pipe_descriptors[1], result);
    }
    descriptor_flags = fcntl(exec_pipe[1], F_GETFD);
    if (descriptor_flags < 0 ||
        fcntl(exec_pipe[1], F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        (void)close(exec_pipe[0]);
        (void)close(exec_pipe[1]);
        if (log_fd != STDOUT_FILENO && log_fd != STDERR_FILENO) {
            (void)close(log_fd);
        }
        result->message[0] = '\0';
        (void)append_text(result->message, sizeof(result->message), 0,
                          "Job failed to configure its exec-status pipe.");
        result->exit_code = 1;
        result->terminating_signal = 0;
        report_child_result(pipe_descriptors[1], result);
    }

    command_child = fork();
    if (command_child < 0) {
        if (log_fd != STDOUT_FILENO && log_fd != STDERR_FILENO) {
            (void)close(log_fd);
        }
        (void)close(exec_pipe[0]);
        (void)close(exec_pipe[1]);
        result->message[0] = '\0';
        (void)append_text(result->message, sizeof(result->message), 0,
                          "Job failed to create its command process.");
        result->exit_code = 1;
        result->terminating_signal = 0;
        (void)write(STDERR_FILENO, "Cannot create command process.\n",
                    sizeof("Cannot create command process.\n") - 1);
        report_child_result(pipe_descriptors[1], result);
    }
    if (command_child == 0) {
        if (close(exec_pipe[0]) != 0) {
            launch_error = errno;
            (void)write_all(exec_pipe[1], &launch_error,
                            sizeof(launch_error));
            _exit(127);
        }
        if (close(pipe_descriptors[1]) != 0) {
            launch_error = errno;
            (void)write_all(exec_pipe[1], &launch_error,
                            sizeof(launch_error));
            (void)write(STDERR_FILENO, "Cannot close job-result pipe.\n",
                        sizeof("Cannot close job-result pipe.\n") - 1);
            _exit(127);
        }
        if (dup2(log_fd, STDOUT_FILENO) < 0 ||
            dup2(log_fd, STDERR_FILENO) < 0) {
            launch_error = errno;
            (void)write_all(exec_pipe[1], &launch_error,
                            sizeof(launch_error));
            (void)write(STDERR_FILENO, "Cannot redirect job output.\n",
                        sizeof("Cannot redirect job output.\n") - 1);
            _exit(127);
        }
        if (log_fd != STDOUT_FILENO && log_fd != STDERR_FILENO) {
            (void)close(log_fd);
        }
        if (use_execve) {
            /* Keep POSIXLY_CORRECT from enabling escape processing in GNU echo. */
            execve(program->path, argv, echo_environment);
        } else {
            execv(program->path, argv);
        }
        launch_error = errno;
        (void)write_all(exec_pipe[1], &launch_error, sizeof(launch_error));
        (void)write(STDERR_FILENO, "Cannot execute ",
                    sizeof("Cannot execute ") - 1);
        (void)write(STDERR_FILENO, program->name, program_name_length);
        (void)write(STDERR_FILENO, ": execution failed.\n",
                    sizeof(": execution failed.\n") - 1);
        _exit(127);
    }

    if (log_fd != STDOUT_FILENO && log_fd != STDERR_FILENO) {
        (void)close(log_fd);
    }
    if (close(exec_pipe[1]) != 0) {
        (void)close(exec_pipe[0]);
        (void)wait_for_child(command_child, &wait_status);
        result->message[0] = '\0';
        (void)append_text(result->message, sizeof(result->message), 0,
                          "Job failed to close its exec-status pipe.");
        result->exit_code = 1;
        result->terminating_signal = 0;
        report_child_result(pipe_descriptors[1], result);
    }
    launch_result = read_all(exec_pipe[0], &launch_error,
                             sizeof(launch_error));
    if (close(exec_pipe[0]) != 0 && launch_result >= 0) {
        launch_result = -1;
    }
    if (launch_result == 1) {
        result->command_started = 1;
    }
    if (wait_for_child(command_child, &wait_status) != 0) {
        result->message[0] = '\0';
        (void)append_text(result->message, sizeof(result->message), 0,
                          "Job failed while waiting for its command process.");
        result->exit_code = 1;
        result->terminating_signal = 0;
        report_child_result(pipe_descriptors[1], result);
    }
    set_execution_result(result, wait_status);
    report_child_result(pipe_descriptors[1], result);
}

const char *process_manager_program(size_t selection)
{
    if (selection == 0 ||
        selection > sizeof(approved_programs) / sizeof(approved_programs[0])) {
        return NULL;
    }
    return approved_programs[selection - 1].name;
}

int process_manager_validate(const char *command, const char *message,
                             char *error, size_t error_size)
{
    size_t index;

    if (command == NULL || find_program(command) == NULL) {
        (void)snprintf(error, error_size,
                       "That program is not approved. Choose one of the listed programs.");
        return -1;
    }
    if (message == NULL || strlen(message) >= JOB_ARGUMENTS_SIZE) {
        (void)snprintf(error, error_size, "Message must be at most %d characters.",
                       JOB_ARGUMENTS_SIZE - 1);
        return -1;
    }
    if (strcmp(command, "echo") != 0 && message[0] != '\0') {
        (void)snprintf(error, error_size, "Only echo accepts a message.");
        return -1;
    }
    if (message[0] == '-' && message[1] != '\0' &&
        strspn(message + 1, "neE") == strlen(message + 1)) {
        (void)snprintf(error, error_size, "Message cannot be an echo option such as -n or -e.");
        return -1;
    }
    for (index = 0; message[index] != '\0'; ++index) {
        unsigned char character = (unsigned char)message[index];
        if (character < 32 || character == 127) {
            (void)snprintf(error, error_size, "Message cannot contain control characters.");
            return -1;
        }
    }
    return 0;
}

void process_manager_print_approved_commands(void)
{
    size_t index;

    puts("\nAvailable programs:");
    for (index = 0;
         index < sizeof(approved_programs) / sizeof(approved_programs[0]);
         ++index) {
        printf("%zu. %s\n", index + 1, approved_programs[index].name);
    }
    putchar('\n');
}

int process_manager_run(const Job *job, int *exit_code, int *command_started)
{
    const ApprovedProgram *program;
    char *argv[3];
    char *echo_environment[] = {"PATH=/usr/bin:/bin", NULL};
    char arguments_copy[JOB_ARGUMENTS_SIZE];
    size_t program_name_length;
    char error[160];
    char log_path[64];
    char message[JOB_ARGUMENTS_SIZE + 128];
    pid_t child;
    int wait_status;
    int pipe_descriptors[2];
    int read_result;
    int use_execve;
    ExecutionResult result;

    *exit_code = -1;
    *command_started = 0;
    if (process_manager_validate(job->command, job->arguments,
                                 error, sizeof(error)) != 0) {
        fprintf(stderr, "Cannot execute job %lu: %s\n", job->id, error);
        return -1;
    }
    program = find_program(job->command);
    if (program == NULL) {
        fprintf(stderr, "Cannot execute job %lu: program is not approved.\n", job->id);
        return -1;
    }
    if (job_storage_log_path(job->id, log_path, sizeof(log_path)) != 0) {
        perror("Cannot build job log path");
        return -1;
    }
    argv[0] = (char *)program->name;
    program_name_length = strlen(program->name);
    argv[1] = NULL;
    argv[2] = NULL;
    use_execve = strcmp(job->command, "echo") == 0;
    if (use_execve) {
        (void)snprintf(arguments_copy, sizeof(arguments_copy), "%s", job->arguments);
        argv[1] = arguments_copy;
    }
    (void)snprintf(message, sizeof(message), "Starting %s %s",
                   job->command, job->arguments);
    if (job_storage_log(job->id, message) != 0) {
        return -1;
    }

    if (pipe(pipe_descriptors) != 0) {
        perror("Cannot create job-result pipe");
        (void)job_storage_log(job->id, "Failed to create the job-result pipe.");
        return -1;
    }
    memset(&result, 0, sizeof(result));
    child = fork();
    if (child < 0) {
        int saved_errno = errno;
        (void)close(pipe_descriptors[0]);
        (void)close(pipe_descriptors[1]);
        errno = saved_errno;
        perror("Cannot create job process");
        (void)job_storage_log(job->id, "Failed to fork the job process.");
        return -1;
    }
    if (child == 0) {
        run_job_child(program, argv, echo_environment, program_name_length,
                      use_execve, log_path, &result, pipe_descriptors);
        _exit(125);
    }

    if (close(pipe_descriptors[1]) != 0) {
        int saved_errno = errno;
        (void)close(pipe_descriptors[0]);
        (void)wait_for_child(child, &wait_status);
        errno = saved_errno;
        perror("Cannot close job-result pipe");
        return -1;
    }
    read_result = read_all(pipe_descriptors[0], &result, sizeof(result));
    if (close(pipe_descriptors[0]) != 0 && read_result == 0) {
        perror("Cannot close job-result pipe");
        read_result = -1;
    }
    if (wait_for_child(child, &wait_status) != 0) {
        perror("Cannot wait for job process");
        (void)job_storage_log(job->id, "Failed while waiting for the job process.");
        return -1;
    }
    if (read_result != 0) {
        if (read_result > 0) {
            fprintf(stderr, "Job process closed the result pipe without a result.\n");
        } else {
            perror("Cannot read job result from pipe");
        }
        (void)job_storage_log(job->id,
                              "Failed to receive the job result from its process.");
        return -1;
    }
    if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
        fprintf(stderr, "Job result supervisor did not exit successfully.\n");
        (void)job_storage_log(job->id,
                              "Job result supervisor did not exit successfully.");
        return -1;
    }
    if (memchr(result.message, '\0', sizeof(result.message)) == NULL ||
        result.exit_code < 0 ||
        result.terminating_signal < 0 ||
        (result.terminating_signal > 0 &&
         result.exit_code != 128 + result.terminating_signal)) {
        fprintf(stderr, "Job result received from pipe is invalid.\n");
        (void)job_storage_log(job->id, "Received an invalid job result.");
        return -1;
    }
    *exit_code = result.exit_code;
    *command_started = result.command_started;
    printf("Received execution result from child: %s\n", result.message);
    if (result.terminating_signal == 0) {
        (void)snprintf(message, sizeof(message),
                       "Process exited with status %d.", *exit_code);
    } else {
        (void)snprintf(message, sizeof(message),
                       "Process was terminated by signal %d.",
                       result.terminating_signal);
    }
    if (job_storage_log(job->id, message) != 0) {
        return -1;
    }
    return 0;
}
