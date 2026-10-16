#ifndef JOB_STORAGE_H
#define JOB_STORAGE_H

#include "job_manager.h"

int job_storage_load(Job jobs[], size_t capacity, size_t *count,
                    unsigned long *next_id);
int job_storage_save(const Job jobs[], size_t count);
int job_storage_log_path(unsigned long job_id, char *path, size_t path_size);
int job_storage_log(unsigned long job_id, const char *message);
int job_storage_show_log(unsigned long job_id);

#endif
