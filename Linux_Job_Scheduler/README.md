# Linux Job Scheduling Alerts

A foreground job scheduler written in C for Linux. The application stores
scheduled jobs in `data/jobs.db`, runs due jobs while it is open, and saves
each job's output and execution history in `logs/job_<id>.log`.

## Project details

- **Project title:** Linux Job Scheduling Alerts
- **Project description:** A terminal-based scheduler that persists one-time
  jobs and launches a small allowlist of Linux programs at future local times.
- **Objectives:** validate scheduled jobs, preserve status across restarts,
  execute approved programs as child processes, and record their output and
  exit results.
- **Team name:** [Enter team name, if applicable]
- **Team members and IDs:** [Enter names and IDs, if required]
- **Faculty/ma'am:** [Enter faculty name]
- **Class, section, and department:** [Enter details, if required]
- **Technologies:** C11, POSIX/Linux system calls, GCC, GNU Make, Ubuntu/WSL.

## Project overview

The menu accepts a program selection and future local time. The job manager
assigns an ID, validates and stores the record, and applies allowed status
transitions. The foreground scheduler checks due times while the menu waits
for input. The process manager runs only `date`, `echo`, `true`, or `false`.
It redirects a command's standard output and error to its per-job log and
collects the real command exit result. A new anonymous pipe reports that
result from the command's supervising child to the original parent. Job
records are stored in `data/jobs.db`; output and status history are stored in
`logs/`. Due jobs are dispatched one at a time through a joined POSIX worker
thread.

## Course outcome mapping

This mapping follows the CO topic descriptions provided with the project
request. It is a project-level mapping, not an official institutional
verification; confirm exact wording with the course handout.

| CO | Concept | Actual implementation | Source files | Purpose and evidence |
| --- | --- | --- | --- | --- |
| CO1 | Linux system calls and OS service interaction | Uses `poll`, `read`, file operations, `fork`, `exec`, `waitpid`, and directory/file APIs. | `src/menu.c`, `src/process_manager.c`, `src/job_storage.c` | Linux OS interfaces drive input, process execution, and persistence. Exercised by `make test`; this is not a demonstration of every Linux command or service. |
| CO2 | Process creation, execution, waiting, and termination | Parent creates a supervisor child; it creates the command process; approved programs run with `execv`/`execve`; both process levels are waited for and reaped. | `src/process_manager.c` | Captured `date`/`echo`/`true` exit 0 and `false` exit 1; controlled exec/fork failures are covered by `tests/test_process_failures.c` and `tests/test_verification.sh`. |
| CO3 | Inter-process communication with an anonymous pipe | The original parent creates a pipe before `fork`; the supervisor waits for the command and writes a fixed execution-result record; parent reads and reports the received result. | `src/process_manager.c` | `tests/test_scheduler.sh` and `tests/test_verification.sh` check success, failure, and exec-failure result messages. This does not implement FIFO, signals, process groups, or sessions. |
| CO4 | Memory safety and memory management | Production records remain in a bounded static array. Record validation checks ID, time, string termination, status, and exit-code consistency; storage APIs guard invalid arrays. A separate `calloc`/`free` example demonstrates heap allocation and release. | `src/job_manager.c`, `src/job_storage.c`, `include/job_manager.h`, `examples/memory_management_demo.c` | Scheduler tests reject inconsistent records; Valgrind was run on startup/shutdown, a scheduled child execution, and the memory example. The application does not implement virtual memory, paging, or a dynamic job list. |
| CO5 | File descriptors, persistent storage, and file I/O | Saves the database by temporary-file replacement; appends and reads execution logs; redirects child stdout/stderr to the log. | `src/job_storage.c`, `src/process_manager.c` | Persistence/restart, log output, and controlled write failures are tested by `make test`. |
| CO6 | Concurrency and synchronization | A POSIX worker thread executes each due job via `pthread_create`; the scheduler joins it before reading its result or updating the record. The thread receives a private `Job` copy; job records, database writes, and status transitions remain on the scheduler thread, so no mutex is needed for this serialized design. | `src/scheduler.c`, `src/process_manager.c`, `Makefile` | Integration tests assert one worker-thread message per executed job and verify success/failure status and logs. Jobs are not run in parallel; no condition variables or semaphores are used. |

## File-by-file explanation

| File or directory | Responsibility | CO relevance |
| --- | --- | --- |
| `src/main.c` | Starts the interactive application through `menu_run`. | Entry point only; no separate CO claim. |
| `src/menu.c` | Reads menu actions and job input, validates date/time, displays jobs/status/logs, polls while awaiting input. | CO1 system interfaces; scheduling user flow. |
| `src/job_manager.c` | Owns the fixed-capacity job array; validates records, allocates IDs, updates allowed states, and cancels pending jobs. | CO4 record bounds/invariants; supports scheduling/status. |
| `src/scheduler.c` | Finds due Pending records, starts one worker thread per due job, joins it, then updates status and displays logs. | CO6 pthread execution and join synchronization; job-state changes remain serialized. |
| `src/process_manager.c` | Validates the allowlist, forks supervisor/command processes, redirects output, executes approved programs, waits, and transfers result by pipe. | CO1 system calls; CO2 processes; CO3 anonymous-pipe IPC. |
| `src/job_storage.c` | Loads and saves `data/jobs.db`; creates paths and appends/prints logs. | CO1 file APIs; CO4 storage argument bounds; CO5 persistence and file I/O. |
| `include/job_manager.h` | Defines `Job`, `JobStatus`, capacity/string bounds, and manager APIs. | CO4 data layout/bounds. |
| `include/process_manager.h` | Declares program validation and execution APIs. | CO2/CO3 API surface. |
| `include/menu.h`, `include/scheduler.h`, `include/job_storage.h` | Declare their respective module APIs. | Supports the corresponding modules; no independent feature claims. |
| `Makefile` | Builds the scheduler and runs integration/verification tests; `clean` removes build output. | Reproducible build/test evidence. |
| `data/jobs.db` | Runtime persistence file, created/updated by the application. | CO5. |
| `logs/` | Runtime per-job logs containing program output and execution/status metadata. | CO5. |
| `tests/test_scheduler.sh` | End-to-end scheduling, IPC result, record validation, cancellation, and persistence checks. | CO2, CO3, CO4, CO5. |
| `tests/test_verification.sh` | Real-program timing/output checks and additional regression/failure tests. | CO2, CO3, CO4, CO5. |
| `tests/test_process_failures.c` | Test-only controlled fork/exec/log failures and child-reaping checks. | CO2 and CO3 failure evidence. |
| `tests/test_cases.md` | Describes covered test scenarios. | Test reference. |
| `examples/memory_management_demo.c` | Separate educational `calloc`/bounds-check/`free` example; not used by the scheduler. | CO4 demonstration only. |
| `docs/module_mapping.md`, `docs/submission_audit.md`, `VERIFICATION_REPORT.md` | Existing architecture, audit, and verification documentation. | Supporting context; see `docs/CO_MAPPING.md` for the updated mapping. |

## Build and run instructions

Open the project in Ubuntu/WSL (in VS Code use **WSL: Reopen Folder in WSL**),
then run:

```sh
cd /mnt/c/MONISHA/KLH/TriSem/VSCode/src/OSSP_Project/Linux_Job_Scheduler
make
make run
```

Run the test suite and clean/rebuild with the existing Makefile targets:

```sh
make test
make clean
make CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror" test
```

The memory-management example is separate from the scheduler build:

```sh
mkdir -p build
cc -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Iinclude \
  -std=c11 -Wall -Wextra -Wpedantic -Werror \
  examples/memory_management_demo.c -o build/memory-management-demo
./build/memory-management-demo
```

If Valgrind is installed, run it from an isolated working directory so the
application does not use the project's saved job database:

```sh
mkdir -p /tmp/job-scheduler-valgrind
cd /tmp/job-scheduler-valgrind
printf '5\n' | valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=definite,indirect --error-exitcode=99 \
  /mnt/c/MONISHA/KLH/TriSem/VSCode/src/OSSP_Project/Linux_Job_Scheduler/build/linux-job-scheduler
```

From the project directory, compile and check the separate allocation example:

```sh
mkdir -p build
cc -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Iinclude \
  -std=c11 -Wall -Wextra -Wpedantic -Werror \
  examples/memory_management_demo.c -o build/memory-management-demo
valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=definite,indirect --error-exitcode=99 \
  ./build/memory-management-demo
```

## Requirements

- Linux (the process manager uses `fork`, `execv`, `poll`, and `waitpid`).
- GCC and GNU Make.
- The approved programs listed below must be installed at their standard
  `/usr/bin` paths.

On Ubuntu, install the compiler and build tools with:

```sh
sudo apt update
sudo apt install build-essential
```

On Windows, use the Ubuntu WSL terminal in VS Code, not a Windows C runner.
Build the entire project with Make; compiling an individual source or header
file is not sufficient. The tests also use Ubuntu's standard `sh`, `awk`,
`grep`, `cmp`, `timeout`, and GNU `date` utilities.

If VS Code reports that `stdio.h` or `stddef.h` cannot be found, its C/C++
extension is probably running in Windows without a Linux toolchain. Reopen
the project using **WSL: Reopen Folder in WSL**, install/enable C/C++ in the
WSL window, and select `/usr/bin/gcc` as its compiler. Do not add Linux system
headers to a Windows compiler configuration or try to run a header file.
The Makefile build in Ubuntu is the authoritative compilation check.

## Build and run

```sh
make
make run
```

You can also launch `./build/linux-job-scheduler` directly. The menu provides:

1. **Schedule a Task** — choose an approved program by its number, enter a
   custom message if you selected `echo`, then enter a future local time in
   `YYYY-MM-DD HH:MM:SS` format.
2. **View Scheduled Jobs** — list saved jobs and their scheduled times/status.
3. **View Job Status and Logs** — enter a job ID to see its current status and
   complete log.
4. **Cancel a Pending Task** — enter a job ID. Only a job that has not started
   can be cancelled.
5. **Exit**.

Approved programs are `date`, `echo`, `true`, and `false`.
The program is selected from this fixed allowlist and executed by
absolute path with `execv` or `execve`; user input is never passed to a shell.
There is no general Arguments prompt. Only `echo` displays `Enter message:`.
Enter up to 255 characters, such as `Hello from my scheduler!`. The entire
message is passed as one literal argument; spaces, quotes, backslashes, and
shell syntax are not interpreted. Empty messages print a blank line.
Control characters and echo option-only messages such as `-n` or `-e` are
rejected. Every other program runs without arguments.

For example, choosing option 1 and then program 1 (`date`) produces this
interaction (enter a date/time that is still in the future on your machine):

```text
Select an option: 1

Available programs:
1. date
2. echo
3. true
4. false

Select program: 1

Enter scheduled date and time (YYYY-MM-DD HH:MM:SS):
2099-10-15 18:40:00

Job scheduled successfully!
Job ID: 1
Program: date
Status: PENDING
```

IDs continue from the saved database, so an existing project may show an ID
other than 1. When the scheduled time arrives, the terminal shows the final
status, such as `Job 1: Succeeded (exit code 0).`, followed by the recorded
execution log including the actual program output. That output (the current
date for `date`, or your exact custom message for `echo`) is also retained in
`logs/job_<id>.log`. Use menu option 3 and the job ID to view it again.
Display happens after the child finishes, not as a live output stream.
Exit and end-of-input explicitly warn that pending jobs will not run until
the application is restarted.

Existing echo messages are preserved across restart, including messages saved
by the previous fixed-message version. The database's arguments column stores
the echo message; it is not a source of arbitrary command options. Other
programs must have an empty arguments column. Unsupported saved programs or
invalid message data cause an explicit startup error rather than execution.

## How automatic execution works

The menu calls the scheduler while waiting for keyboard input. `poll` waits
for input or the next due time; even an unfinished input line does not stop
the timer. A due job moves from Pending to Running, then the process manager:

1. Creates an anonymous pipe and calls `fork` to create a supervising child.
2. The supervisor opens the job log and forks the command process.
3. The command process redirects stdout/stderr with `dup2` and calls `execv`
   (or `execve` for echo with a controlled environment).
4. The supervisor uses `waitpid` to collect/reap the command and writes a
   fixed execution-result record through the pipe. The command process closes
   the pipe descriptor and cannot interfere with the message.
5. The original parent reads the complete result, checks it, and uses
   `waitpid` to reap the supervisor.

The pipe message contains the command's exit code, terminating signal if any,
and a concise success/failure description. Pipe reads and writes handle
partial transfers and `EINTR`; the unused ends are closed so the parent does
not wait for EOF from an unrelated process. The command's ordinary output
continues to go only to its job log.

Exit code 0 produces Succeeded; a nonzero code produces Failed. For example,
`false` deliberately returns 1: that means it executed correctly but its job
result is Failed. The parent reports the result actually received from the
pipe. Failure to execute an executable is recorded with exit code 127; the
job log identifies the approved program whose launch failed. Failure to
create a process is reported and the job is marked Failed with
application-assigned code 1.
Termination by a signal is represented as `128 + signal number`. An
infrastructure/logging error is reported explicitly; if the child has already
exited, its real exit code is retained rather than replaced with an invented
process failure code.

Pending jobs can only become Running or Cancelled. Running jobs can only
become Succeeded or Failed. Completed and cancelled jobs cannot be rerun or
cancelled again. On restart, interrupted Running records become Failed.
IDs increase from the highest saved ID; duplicate or negative IDs in a
database are rejected rather than silently used.

## Program output

| Choice | Program | Expected log output | Expected exit code / status |
| --- | --- | --- | --- |
| 1 | `date` | Current date/time from Linux at execution | 0 / Succeeded |
| 2 | `echo` | Your custom message, for example `Hello from my scheduler!` | 0 / Succeeded |
| 3 | `true` | No program output | 0 / Succeeded |
| 4 | `false` | No program output | 1 / Failed |

An actual Ubuntu WSL verification run on 2026-10-15 captured the following
program output, excluding timestamped log metadata:

```text
date:   Thu Oct 15 22:43:47 IST 2026
echo:   Hello from my scheduler!
false:  (no standard output)
true:   (no standard output)
```

These are recorded observations, not hardcoded application results. The
date will depend on the next run. Tests use temporary working directories
to protect project data and compare output with the actual environment.

## Demonstration and expected output

The following result lines were captured by the test suite on 2026-10-15.
Job IDs and the date/time vary by run.

Successful scheduled job (`date`, `echo`, or `true`):

```text
Received execution result from child: Job completed successfully.
Job 1: Succeeded (exit code 0).
```

Failing approved command (`false`, whose real exit code is 1):

```text
Received execution result from child: Job failed with exit code 1.
Job 1: Failed (exit code 1).
```

Successful record validation, printed only after the new record passes its
field/invariant checks:

```text
Job record validated successfully.
Job scheduled successfully!
```

Menu option 3 displays the selected job's stored status and log, including
the command's output. The end-to-end tests also verify that a saved Pending
job has no execution log before its due time, and that the final status and
exit code match the stored record.

**CO6 worker-thread demonstration:** the following message is printed by the
worker thread after the process manager has received and validated the real
child result and confirmed through a close-on-exec pipe that the approved
program reached `exec`. Job IDs and result status vary by run:

```text
Received execution result from child: Job completed successfully.
Job 1 executed by worker thread.
Job 1: Succeeded (exit code 0).
```

The message is not printed if thread creation fails, program execution does
not reach `exec`, or the process manager cannot complete result handling.
Nonzero command exits still count as executed jobs and print the message
alongside their real failure status.
`pthread_join` synchronizes the worker's result with the scheduler before it
updates the job record. Jobs remain one-at-a-time; this is not a parallel
worker pool.

## Testing

Run both automated test suites with:

```sh
make test
```

The tests create their job database and logs inside temporary directories;
they do not execute or change your saved project jobs. The detailed suite
schedules each program individually, confirms Pending status, waits with no
further input, then checks the final status, output, exit code, logs, and start
time. It asserts that dispatch is not early and is within two seconds of the
requested time under the test environment; this is not a real-time guarantee.

Regression tests cover cancellation, invalid calendar/past/current times,
valid leap dates, job IDs, the 100-job limit, partial input, database-write
errors, and persistence across application exit and restart. A test-only
executable uses GNU linker wrappers to simulate `execv`, `fork`, and
`pthread_create` failures. Those wrappers do not affect the normal application
or system executables. Tests also check exit-code preservation during a
controlled logging failure,
exact-format dates, embedded control-character rejection, CRLF terminal input,
the 255-byte echo boundary, automatic output display, and the exit reminder.
The new tests check that the anonymous pipe conveys success, nonzero exit,
and exec-failure results, and that malformed job-record state/exit-code
combinations are rejected. Header dependency rebuilding is checked too.

Valgrind was available and executed on 2026-10-15 against scheduler
startup/shutdown, a real scheduled `date` child/pipe result, and
`examples/memory_management_demo.c`; each reported
`ERROR SUMMARY: 0 errors from 0 contexts`. These are bounded runs, not proof
that every execution path is free of memory errors.

Detailed transcripts and actual program logs from the latest run are copied
to `build/verification-evidence/` before temporary directories are removed.
The test output distinguishes controlled failure simulation from real
program execution. A full run takes roughly a minute because it waits for
real future scheduling times.

For a strict build treating warnings as errors:

```sh
make clean
make CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror" test
```

Remove compiled output with:

```sh
make clean
```

## Foreground scheduler limitations

- This is a foreground application, not a daemon or system service. It checks
  due times and runs jobs only while it is running; exiting the application
  stops scheduling. Pending jobs saved before exit remain in the database and
  overdue pending jobs run when the application is started again. Jobs left in
  the `Running` state by an interrupted application are marked failed on
  restart.
- Saved jobs and logs remain on disk after application exit or a system
  restart, provided the files are preserved. The application does not start
  itself at boot. Restart it from the same working directory to load pending
  jobs; overdue pending jobs run then, not while the application was stopped.
  Abrupt power loss is not covered by a crash-durable storage guarantee.
- Run only one scheduler instance per data directory. Concurrent instances
  are not coordinated and could overwrite each other's saved state.
- Jobs run one at a time. The scheduler waits for each child process with
  `waitpid` and joins its worker thread, so a slow approved program
  temporarily blocks menu interaction.
- A POSIX worker thread executes each job, but jobs are not run in parallel.
  The scheduler passes a private job snapshot to the worker and performs all
  job-state and database updates only after joining it; this serialized
  ownership means no mutex, condition variable, or semaphore is needed.
- The process manager's post-`fork()` child paths use only async-signal-safe
  operations before `exec`/`_exit`, so forking from the worker does not enter
  unsafe stdio or string-formatting routines in the child.
- The production job list is a bounded static array of 100 records; it does
  not use `malloc`, `calloc`, or `realloc`. The separate memory example is
  educational and is not linked into the application.
- The pipe carries a result between related processes. No named FIFO,
  signal handler, process-group/session management, or interactive shell
  job-control feature is implemented.
- Times use the machine's local timezone and have one-second precision.
  This is not a real-time scheduler: OS load, another running task, system
  sleep, and manual clock changes can delay dispatch. Interrupted parent
  processes do not guarantee that an already-started child is terminated.
- Jobs are one-time tasks; recurring schedules, editing, and cancellation of
  already-running jobs are not supported.
- The scheduler stores up to 100 jobs. Only four approved programs and a
  custom echo message are supported; arbitrary program options are not.
- The database and logs are relative to the current working directory, so run
  the application from the project directory to use the project's `data/` and
  `logs/` folders.

See [docs/module_mapping.md](docs/module_mapping.md) for the source module
responsibilities and [tests/test_cases.md](tests/test_cases.md) for test
coverage. See [docs/submission_audit.md](docs/submission_audit.md) for the
submission checklist, limitations, verification results, and viva notes.
See [docs/CO_MAPPING.md](docs/CO_MAPPING.md) for the implemented CO mapping
and unsupported topics. The mapping follows the topic descriptions supplied
for this project and must be checked against the official course handout.
