CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Iinclude
LDFLAGS ?=
THREAD_FLAGS := -pthread

TARGET := build/linux-job-scheduler
SOURCES := src/main.c \
	src/menu.c \
	src/job_manager.c \
	src/scheduler.c \
	src/process_manager.c \
	src/job_storage.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all run test clean

all: $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	sh tests/test_scheduler.sh
	$(MAKE) build/test-process-failures
	sh tests/test_verification.sh

build/test-process-failures: tests/test_process_failures.c $(filter-out build/main.o build/menu.o,$(OBJECTS)) $(wildcard include/*.h)
	$(CC) $(THREAD_FLAGS) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_process_failures.c \
		$(filter-out build/main.o build/menu.o,$(OBJECTS)) \
		-Wl,--wrap=fork -Wl,--wrap=execv -Wl,--wrap=pthread_create \
		-Wl,--wrap=job_storage_log

$(TARGET): $(OBJECTS)
	$(CC) $(THREAD_FLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(THREAD_FLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPENDENCIES)

clean:
	rm -rf build
