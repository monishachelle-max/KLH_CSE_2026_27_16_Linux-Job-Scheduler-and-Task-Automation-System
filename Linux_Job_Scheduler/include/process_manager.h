#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include "job_manager.h"

#include <stddef.h>

int process_manager_validate(const char *command, const char *message,
                             char *error, size_t error_size);
const char *process_manager_program(size_t selection);
void process_manager_print_approved_commands(void);
int process_manager_run(const Job *job, int *exit_code, int *command_started);

#endif
