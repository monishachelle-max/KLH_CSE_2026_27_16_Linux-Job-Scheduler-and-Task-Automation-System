# Linux Job Scheduling Alerts

## 1. Team Details

- **Project Title:** Linux Job Scheduling Alerts
- **Team Number:** 16
- **Team Members and Roll Numbers:**
	- Monisha Raghini Chelle: 2520030537
	- Shaik Sabiya Tabassum: 2520030502
	- kolipaka Siri Chandana: 2520030453
- **Faculty Name:** Dr. K. Anusha
- **Course:** Operating Systems and Systems Programming (OSSP)
- **Section:** 6
- **Department:** CSE

Existing names and institutional details are retained from the supplied README;
the team must confirm their spelling and accuracy before submission.


---

## 2. Project Description

Linux Job Scheduler is a terminal-based job scheduling application developed using C on Linux. It allows users to schedule approved commands for future execution, view scheduled jobs, monitor job status, inspect execution logs, and cancel pending jobs.

The project demonstrates how Linux operating-system services can be used to manage processes, execute commands, communicate between processes, handle files, validate job records, and use worker threads.

The application is designed for execution in a controlled Linux environment, including Ubuntu on Windows Subsystem for Linux (WSL).

## 3. Objectives

- Schedule commands for execution at a specified future time.
- Execute scheduled commands using Linux process-management system calls.
- Track job status and exit codes.
- Store job information persistently.
- Record command output and execution logs.
- Demonstrate inter-process communication using an anonymous pipe.
- Validate job records and improve memory safety.
- Demonstrate POSIX worker-thread execution where implemented.
- Apply operating-system concepts through a working C application.

## 4. Technologies Used

- **Programming language:** C
- **Operating system:** Linux / Ubuntu WSL
- **Compiler:** GCC
- **Build system:** GNU Make
- **Process management:** `fork()`, `execv()`/`execve()`, `waitpid()`
- **File I/O:** `open()`, `dup2()`, file descriptors, and standard file operations
- **Inter-process communication:** Anonymous pipes
- **Thread execution:** POSIX threads (`pthread_create()` and `pthread_join()`), one job at a time
- **Version control:** Git and GitHub

## 5. Main Features

1. Schedule approved commands for future execution.
2. View scheduled jobs and their status.
3. Execute commands in child processes.
4. Capture command output in job-specific log files.
5. Record job results and exit codes.
6. Cancel pending jobs.
7. Store job records in `data/jobs.db`.
8. Communicate execution results between processes through an anonymous pipe.
9. Validate job records before accepting or processing them.
10. Execute each due job in a real POSIX worker thread, joined before the next job.

The application supports only the commands allowed by its implementation. It is not intended to be a general-purpose shell or a replacement for system-wide scheduling services.

## 6. Project Folder Structure

```text
Linux_Job_Scheduler/
├── build/
├── data/
├── docs/
├── include/
├── logs/
├── results/
├── src/
├── tests/
├── .gitignore
├── Makefile
└── README.md
```

### Folder and File Descriptions

| File/Folder | Purpose |
|---|---|
| `build/` | Compiled executable and object files generated during builds |
| `data/` | Persistent job database |
| `docs/` | Audit evidence in `audit-verification.md`; final report and PPT are still missing |
| `include/` | Header files and shared declarations |
| `logs/` | Job-specific execution logs |
| `results/` | Screenshots and demonstration/test evidence |
| `src/` | C source code for the application |
| `tests/` | Automated tests and test scripts |
| `.gitignore` | Specifies generated or unnecessary files to exclude from Git |
| `Makefile` | Contains build and other supported project commands |
| `README.md` | Project overview, setup, execution instructions, and CO mapping |

### Source File Responsibilities

The source files in this repository are:

- `src/main.c` — Application entry point.
- `src/menu.c` — Terminal menu and user interaction.
- `src/job_manager.c` — Job records, validation, and job-related operations.
- `src/job_storage.c` — Persistent storage of job information.
- `src/process_manager.c` — Child-process creation, command execution, exit-status handling, output redirection, and pipe communication.
- `src/scheduler.c` — Checks scheduled times and dispatches due jobs through a worker thread.
- `include/` — Header files defining shared data structures and function declarations.

The three executable tests are `tests/test_scheduler.sh`,
`tests/test_verification.sh`, and the wrapped C harness
`tests/test_process_failures.c`. Coverage is described in `tests/test_cases.md`.

## 7. System Architecture

The application follows this general execution flow:

1. The user interacts with the terminal menu.
2. The application validates the requested job and its scheduled time.
3. The job manager maintains the job record.
4. The storage module saves and retrieves job information.
5. `read_line()` uses `poll()` and `scheduler_milliseconds_until_next()` to dispatch due jobs even while input is incomplete.
6. `scheduler_run_due()` persists the Running state and creates a worker using `pthread_create()`.
7. The worker calls `process_manager_run()`, which creates a result pipe and forks a supervisor process.
8. The supervisor forks the command process; that process redirects stdout/stderr with `dup2()` and executes the approved program. A second close-on-exec pipe distinguishes successful launch from execution failure.
9. The supervisor calls `waitpid()` and writes an `ExecutionResult` through the anonymous result pipe. The worker reads it and reaps the supervisor.
10. The main thread joins the worker with `pthread_join()`, persists the final status, and displays the real execution log.

Jobs run sequentially. The menu waits while each worker is joined. Pending jobs
remain saved when the application exits, but execute only while it is running;
overdue pending jobs execute on restart. This is not a background service.

## 8. Course Outcome (CO) Mapping

The project demonstrates selected concepts from CO1–CO6. It does not implement every topic listed in the complete course syllabus.

### CO1 — OS as a Service Layer

**Syllabus topics:** System calls, user/kernel space, kernel services, shell execution, Linux architecture, and systems programming.

**Project implementation:**
- Uses Linux system calls and library interfaces.
- Uses terminal input and output.
- Creates and executes processes.
- Uses file descriptors and file operations.

**Relevant files:** `src/menu.c`, `src/process_manager.c`, `src/main.c`.

**Demonstration:** Schedule a job, execute an approved command such as `date`, and observe the command output and exit status.

**Limitation:** The project does not implement a shell or the Linux kernel architecture.

### CO2 — Processes and Process Control

**Syllabus topics:** Process creation, execution, lifecycle, synchronization, termination, and scheduling concepts.

**Project implementation:**
- Creates child processes with `fork()`.
- Executes commands with `execv()` or `execve()`.
- Waits for child termination with `waitpid()`.
- Checks exit status and signal-caused termination.
- Schedules and tracks jobs.

**Relevant files:** `src/process_manager.c`, `src/scheduler.c`, and `src/job_manager.c`.

**Demonstration:** Run a successful command and a failing command, then inspect their exit codes and job statuses.

**Limitation:** Cancellation applies only to Pending jobs. The application does
not send termination signals to running processes or implement CPU-scheduling algorithms.

### CO3 — Inter-Process Communication

**Syllabus topics:** Anonymous pipes, named FIFOs, signals, signal handlers, process groups, sessions, and job control.

**Project implementation:**
- Uses an anonymous pipe to transfer an execution-result message from the child process to the parent process.
- Uses process termination status to identify normal completion or signal-caused termination.

**Relevant file:** `src/process_manager.c`.

**Demonstration:** Observe the parent receiving the actual child-process result, for example:

```text
Received execution result from child: Job completed successfully.
```

The message must correspond to a real pipe write and read.

**Limitation:** The anonymous pipe does not mean that named FIFOs, custom signal handlers, `SIGALRM` scheduling, process groups, or session management are implemented.

### CO4 — Memory Management

**Syllabus topics:** Virtual memory, address translation, page tables, page faults, demand paging, process address spaces, dynamic allocation, copy-on-write, memory errors, and debugging tools.

**Project implementation:**
- Maintains and accesses job records.
- Validates job records and checks appropriate bounds.
- Reviews string and buffer handling for memory safety.
- Uses fixed arrays of at most 100 jobs, 32-byte command buffers, and 256-byte message buffers.
- Uses bounded input, string-termination checks, numeric conversion checks, and status-transition validation.
- A Valgrind lifecycle test completed with zero reported errors and no outstanding heap allocations; see `docs/audit-verification.md` for its scope.

**Relevant files:** `src/job_manager.c` (`validate_job_record()`), `include/job_manager.h`, `src/menu.c` (`read_line()`), and `src/job_storage.c` (`job_storage_load()`).

**Demonstration:** Submit a valid job and observe the validation message when the corresponding validation check succeeds:

```text
Job record validated successfully.
```

**Limitation:** Record validation is not an implementation of virtual memory, page tables, demand paging, or copy-on-write. Those mechanisms are provided by the operating system.

### CO5 — File Systems and File I/O

**Syllabus topics:** Unix file abstraction, file descriptors, Linux I/O system calls, buffered and unbuffered I/O, inodes, VFS, memory-mapped I/O, and ext4.

**Project implementation:**
- Saves and retrieves job records.
- Uses files for persistence.
- Redirects command output to log files.
- Reads and writes information through file interfaces.

**Relevant files:** `src/job_storage.c`, `src/process_manager.c`, and `data/`.

**Demonstration:** View saved jobs, execute a command, and inspect the corresponding job log.

**Limitation:** The project does not implement its own filesystem, inode management, VFS, ext4, or memory-mapped I/O.

### CO6 — Concurrency and Synchronization

**Syllabus topics:** Threads, processes, race conditions, shared data, mutexes, condition variables, semaphores, deadlocks, and synchronization hazards.

**Project implementation:**
- `scheduler_run_due()` creates a POSIX worker for each due job.
- `run_job_worker()` executes the process manager on a copied Job record.
- `pthread_join()` synchronizes completion before the main thread reads worker results or updates stored jobs.
- Controlled `pthread_create()` failure is tested without printing a false worker-execution message.

**Relevant files:** `src/scheduler.c` (`scheduler_run_due()`, `run_job_worker()`), `src/process_manager.c`, and `tests/test_process_failures.c`.

**Demonstration:** A successfully launched command produces a message from the actual worker, for example:

```text
Job 17 executed by worker thread.
```

**Limitation:** Thread creation and joining are implemented and tested, but jobs
do not execute simultaneously. There are no mutexes, condition variables, or semaphores.

## 9. Requirements

- Ubuntu Linux or Ubuntu on WSL.
- GCC C compiler.
- GNU Make.
- Git, if cloning the repository.
- POSIX-compatible process and thread interfaces.
- GNU `date` (including `-d`), coreutils `timeout`, `mktemp`, `getconf`, `awk`, `sed`, `grep`, and `cmp` for tests.
- The four approved executables at `/usr/bin/date`, `/usr/bin/echo`, `/usr/bin/true`, and `/usr/bin/false`.
- A GNU-compatible linker supporting `--wrap` for the controlled failure harness.
- Optional testing tools such as Valgrind, if available.

## 10. Setup and Execution

### Step 1: Open the project directory

Open Ubuntu/WSL and navigate to the directory containing the project:

```bash
cd /path/to/Linux_Job_Scheduler
```

Replace the example path with the actual location of your project.

### Step 2: Compile the project

The existing `all` target builds `build/linux-job-scheduler`:

```bash
make
```

For the strict build used in the audit:

```bash
make clean
make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wformat=2 -Wshadow -Wconversion'
```

### Step 3: Run the application

Run the foreground application from the project root:

```bash
make run
```

Alternatively, run `./build/linux-job-scheduler`. Database and log paths are
relative to the current working directory. Use only one instance per working
directory: database updates are not protected by inter-instance locking.

### Step 4: Run the tests

The `test` target runs both shell suites and builds/runs the C failure harness:

```bash
make test
```

To run the suites separately:

```bash
sh tests/test_scheduler.sh
make build/test-process-failures
sh tests/test_verification.sh
```

The verification script supplies the temporary database required by the C
harness. Do not run the harness directly in the project root: it intentionally
updates the database in its working directory. Both shell suites use temporary
data and logs. Allow at least two minutes for the complete suite, rather than
terminating the entire run with a short outer timeout.

### Step 5: Clean generated build files

Remove generated binaries, objects, dependencies, and verification evidence:

```bash
make clean
```

This does not remove the real `data/jobs.db` or `logs/`.

## 11. Testing and Validation

Test the following scenarios:

1. Schedule a valid future job.
2. Reject an invalid command or invalid scheduled time.
3. Execute a successful command such as `date` or `echo`.
4. Execute a failing command such as `false`, if allowed.
5. Verify the recorded exit code and final job status.
6. View saved jobs and their execution logs.
7. Cancel a pending job.
8. Restart the application and verify persistent job information.
9. Verify that the CO3 pipe message is transferred through a real pipe.
10. Verify that the CO4 validation message follows an actual validation check.
11. Verify the CO6 message comes from an actual worker thread if threads are implemented.
12. Run the automated tests and available memory checks.

### Verified audit results (15 October 2026)

On Ubuntu WSL2 x86_64 with GCC 15.2.0, the strict clean build completed without
warnings. `make test` completed successfully before and after the test fixes:
both shell suites and the C failure harness passed. `date`, `echo`, and `true`
returned 0; `false` returned 1 and was correctly recorded as Failed. All four
detailed program sessions had a measured dispatch delay of 0 seconds.

Database-write errors at all six prompts exited with status 1. Invalid saved
records, exhausted IDs, the 99/100/101-job boundaries, input validation,
cancellation, restart persistence, actual pipe delivery, and worker creation
failure passed. Hash comparisons confirmed the real database and all logs were
unchanged. A Valgrind lifecycle run reported zero errors and no remaining heap
allocations; this is not exhaustive proof of memory safety.

Fresh transcripts and logs are generated under `build/verification-evidence/`
(ignored by Git and removed by `make clean`). Failure diagnostics are copied to
`build/verification-evidence/failed-run.*`. The audit and exact scope are in
[`docs/audit-verification.md`](docs/audit-verification.md).

### Demonstration

1. Run `make run`, choose 1, then program 1 (`date`), and enter a local time at least 10 seconds in the future using `YYYY-MM-DD HH:MM:SS`.
2. Choose 2 to view Pending jobs; leave the menu idle and observe automatic execution, pipe-result output, worker output, and the date log.
3. Choose 3 and the displayed job ID to inspect its status and log.
4. Repeat with program 2 (`echo`, with a message) and program 4 (`false`, expected Failed with exit code 1).
5. Schedule another future job, cancel its displayed ID with option 4, and verify it never executes. Finished jobs cannot be cancelled.
6. Schedule a future job, exit with option 5, then restart after its due time to demonstrate persistence and overdue execution.
7. Run `make test` for malformed-input and controlled-failure demonstrations; do not corrupt the real database to demonstrate these cases.

The 23 supplied PNGs in `results/outputs/` are historical demonstration images,
not evidence that the current test suite passed. The editor area of `CO4.png`
shows older allocation code; use current source and fresh output when presenting.

## 12. Limitations

- The application executes only commands permitted by its implementation.
- The scheduler's execution behavior depends on the application's running and restart logic.
- The project demonstrates selected operating-system concepts rather than implementing a complete operating system.
- An anonymous pipe does not cover every IPC topic in CO3.
- Job-record validation does not implement the virtual-memory mechanisms listed in CO4.
- File I/O does not mean that the project implements a filesystem.
- Worker-thread output does not establish concurrent execution by itself.
- Workers are joined sequentially, and input handling pauses during job execution.
- At most 100 total records are retained, including completed and cancelled jobs; no deletion UI is implemented.
- Storage uses temporary-file replacement, but has no inter-instance locking or `fsync()` power-loss durability guarantee.
- Signal-caused status decoding exists but was not exercised by an end-to-end signal-termination test.
- Rare pipe/fcntl/read/write/waitpid/pthread_join failures are not all fault-injected.
- ShellCheck and Clang were unavailable for this audit; Valgrind was available and used.

## 13. Conclusion

Linux Job Scheduling Alerts is a C-based Linux application that demonstrates scheduled command execution, process management, persistent job records, file I/O, inter-process communication, and selected memory-safety and concurrency concepts.

The project provides a practical way to connect operating-system concepts with a working application. Its final evaluation should be supported by the source code, successful tests, documentation, and a live terminal demonstration.



