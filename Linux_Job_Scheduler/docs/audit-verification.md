# Linux Job Scheduling Alerts: Audit and Verification

Build/test date: 15 October 2026; final documentation checks continued after
midnight on 16 October local time. Intended submission: 17 October 2026.
Environment: Ubuntu WSL2, Linux 6.6.87.2-microsoft-standard-WSL2,
x86_64, GCC 15.2.0, Valgrind 3.26.0. Results describe the supplied working
tree with the corrections listed below, not a published GitHub revision.

## A. Overall status

**READY FOR DEVELOPMENT UPLOAD, BUT NOT FINAL SUBMISSION**

The strict clean build, complete automated suite, extra boundary checks,
signal-cleanup regression probes, and focused Valgrind run completed
successfully. No application source or menu behavior was changed.
The final academic report, PPT, team-name/class details, and separate project
repository upload remain outstanding. The corrected README is in an already
dirty VS Code editor buffer; its older on-disk copy still has broken links.
Persisting/reconciling that README is also required before final upload.
This audit is not the academic report.

## B. Build status

Executed from the project root inside WSL:

```sh
make clean
make CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wformat=2 -Wshadow -Wconversion'
make test CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wformat=2 -Wshadow -Wconversion'
```

Result: build exit 0, no compiler warnings or errors. The same flags built
the wrapped C failure harness. The Makefile supplies `-pthread` during
compilation and linking, POSIX/XSI feature macros, and header dependencies
through `-MMD -MP`. The `all`, `run`, `test`, and `clean` targets exist.
`make -n -W include/job_manager.h all` demonstrated dependent-source rebuilds.
No Makefile correction was necessary.

The complete `make test` target passed before the added regression coverage
and passed again afterward. Generated records of the final run are in
`build/verification-evidence/strict-build.txt` and `full-suite.txt`.
`make clean` intentionally removes these generated files.

## C. Test results

| Test | Command | Result | Actual evidence | Correction/qualification |
|---|---|---|---|---|
| Scheduler integration | `sh tests/test_scheduler.sh` via `make test` | PASS | `All scheduler integration tests passed.` | Fixed signal exits; application unchanged |
| Complete detailed verification | `sh tests/test_verification.sh` via `make test` | PASS | `All detailed verification tests passed.` | Fixed signal cleanup; improved diagnostics and boundaries |
| C controlled-failure harness | `timeout 20 "$failure_application"` inside verification | PASS | `Controlled execv/fork/thread-creation failures, exit-code preservation and child-reaping checks passed.` | Must run with the suite's temporary four-job fixture, never against real data |
| All four real commands | Detailed verification program sessions | PASS | `date`, `echo`, `true`: Succeeded/0; `false`: Failed/1; dispatch delays all 0 seconds | Failed status for `false` is the expected successful test outcome |
| Database-write failure | Six `error-*` cases in verification | PASS | All application exits 1; `Cannot write temporary job database: Is a directory` | `data/jobs.db.tmp` is deliberately a directory only inside temporary fixtures |
| Input, messages, time | Both suites | PASS | Invalid commands/IDs, calendar/leap dates, past/current times, NUL/CR, overlong lines, literal shell text, CRLF, 255-byte messages | No arbitrary shell command execution |
| Record and capacity boundaries | Expanded verification | PASS | Rejects duplicate/negative/zero/overflow/exhausted IDs, invalid status/time, extra fields, embedded NUL, 101 saved records; accepts record 100 from 99 then rejects 101 | No source change required |
| Cancellation and restart | Integration and detailed verification | PASS | Cancelled job never starts; finished job cannot be cancelled; saved results and IDs survive restart; overdue pending job executes; interrupted Running record becomes Failed | Foreground-only scheduler |
| CO3/CO4/CO6 paths | Both suites and failure harness | PASS | Real pipe-result messages, validated-record message, exactly one worker message per executed integration job, none for cancelled/creation-failed jobs | See source-level mapping below |
| Header dependency rebuild | `make -n -W include/job_manager.h all` inside verification | PASS | menu, job_manager, scheduler, process_manager, job_storage compile commands present | Dry run verifies dependency routing, not a second compilation |
| Signal cleanup and diagnostics | Isolated probes using the scripts' actual trap definitions | PASS | HUP/INT/TERM exits 129/130/143; ordinary exit 7 preserved; transcript retained; no continuation | Probes do not test graceful application signal handling |
| Memory lifecycle | `timeout 30 valgrind ... "$application"` in a temporary directory | PASS | `ERROR SUMMARY: 0 errors`; `in use at exit: 0 bytes in 0 blocks`; 78 allocations/78 frees | Scoped lifecycle, not exhaustive memory-safety proof |
| Real-data preservation | Sorted SHA-256 manifests before/after complete tests and probes, compared with `cmp` | PASS | Manifest comparisons exit 0 for database, all 17 logs, and `.gitkeep` files | Real database retained its 17 records |
| Shell syntax | `sh -n tests/test_verification.sh tests/test_scheduler.sh` | PASS | Exit 0 | Not a substitute for executed suites |
| On-disk README links | PowerShell local Markdown-link existence check | FAIL | Missing `docs/module_mapping.md`, `docs/submission_audit.md`, `docs/CO_MAPPING.md` | The corrected README is in the pre-existing dirty editor buffer, not yet on disk; the other edited files persisted |
| ShellCheck / Clang | Tool availability checks | NOT RUN | Neither executable installed | Optional tools, not missing requirements for `make test` |

All executable tests supplied by the project were run. `tests/test_cases.md`
is coverage documentation, not another executable test. No project suite
failed in this audit. Initial terminal-wrapper attempts encountered Windows
quoting/carriage-return errors; corrected invocations produced the results above.

### Verification-suite termination investigation

The original scripts installed the same cleanup-only trap for EXIT, HUP,
INT, and TERM. A caught signal ran `rm -rf "$temporary_directory"` but did
not terminate the script. An isolated reproduction created a transcript,
entered its directory, sent TERM to its own shell, then attempted to read
the transcript. Actual output:

```text
cat: transcript.txt: No such file or directory
```

Thus a missing transcript can be a secondary consequence of deleting the
working directory after interruption. It is not evidence that transcript
creation or database handling caused the interruption. The original external
termination trigger cannot be established from the supplied files. A short
outer timeout is a plausible explanation, not a verified historical fact.
No hang was reproduced: both complete suites finished, including all six
database-write failure cases. Allow at least two minutes for `make test`;
its sequential sleeps make it unsuitable for a very short outer timeout.

Signal traps now exit explicitly and delegate cleanup to EXIT. The detailed
suite copies failed-run files before deleting its temporary directory, preserves
the original exit status, archives each database-error transcript/exit code,
and places a 20-second deadline around the previously unbounded C harness.
Neither shell suite reads or writes the user's real job database or logs.
The C harness does mutate its current directory's database: invoke it only
through the verification suite's isolated fixture.

### Valgrind scope and reproduction

The executed lifecycle started with four overdue temporary jobs (`date`,
`echo` with `Memory audit message`, `true`, `false`), scheduled and cancelled
job 5, queried job 4, rejected `rm` and an invalid time, then exited. A second
application run confirmed the saved Cancelled status. Exact Valgrind options:

```sh
timeout 30 valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=definite,indirect --error-exitcode=99 \
  --child-silent-after-fork=yes \
  --log-file="$evidence/memory/valgrind.txt" "$application"
```

For a safe repeat, set `application` and `evidence` to absolute project paths,
create a `mktemp -d` working directory, and run there, never in the real data
directory. For example, this reproduces the normal four-program memory path:

```sh
project_root=$PWD
(
  sandbox=$(mktemp -d)
  trap 'rm -rf "$sandbox"' EXIT
  cd "$sandbox" || exit 1
  mkdir data
  printf '1\t1\t0\t0\tdate\t\n2\t1\t0\t0\techo\tMemory audit message\n3\t1\t0\t0\ttrue\t\n4\t1\t0\t0\tfalse\t\n' >data/jobs.db
  printf '5\n' | timeout 30 valgrind --leak-check=full \
    --errors-for-leak-kinds=definite,indirect --error-exitcode=99 \
    --child-silent-after-fork=yes "$project_root/build/linux-job-scheduler"
)
```

The shorter repeat example is not the full cancellation/validation lifecycle
used in this audit. Its separate invocation has not been reported as a passed
test. Existing memory output and transcript are under
`build/verification-evidence/memory/`. Forked-child diagnostics were silenced;
this was not a separate memory audit of each system executable.

## D. Fixes made

| File | Exact correction |
|---|---|
| `tests/test_scheduler.sh` | Separate signal exits 129/130/143 from EXIT cleanup, preventing continuation in a deleted directory |
| `tests/test_verification.sh` | Same signal fix; copy failure diagnostics; retain per-prompt database error evidence and exit codes; enforce harness deadline; add invalid saved-record and 99/100/101 boundary regression cases |
| `.gitignore` | Exclude `data/jobs.db.tmp`, object/dependency files outside build, and common editor swap/backup files; retain existing build/database/log exclusions |
| `README.md` | Editor-buffer changes only: consistent title/path; repair team formatting without inventing details; team-name/class placeholders; accurate architecture/CO mapping, tested commands, results, limitations, safe demo and harness instructions, submission checklist. The older disk file has not been replaced |
| `tests/test_cases.md` | Document expanded boundaries, signal/failure evidence, isolation, runtime and harness precautions |
| `docs/audit-verification.md` | Add this evidence-based audit, including remaining submission work |

No changes to `src/`, `include/`, Makefile, real database/logs, menu messages,
job IDs, scheduling, persistence format, or status transitions. Build products
and generated verification evidence were recreated as requested; old build
evidence was removed by `make clean`. Existing screenshots were retained.

## E. CO1-CO6 audit

| CO | Source files and functions | Actual implemented concept | Executed evidence | Not implemented / not demonstrated |
|---|---|---|---|---|
| CO1 | `src/main.c`: `main()`; `src/menu.c`: `menu_run()`, `read_line()`; `src/process_manager.c`: `process_manager_run()`, `run_job_child()` | Terminal interaction, OS services, `poll/read`, process execution, descriptors | Integration menu/validation checks and real `date`/`echo` output | No shell interpreter, kernel implementation, or user/kernel instrumentation |
| CO2 | `src/process_manager.c`: `run_job_child()`, `wait_for_child()`, `set_execution_result()`; `src/scheduler.c`: `scheduler_run_due()`, `scheduler_milliseconds_until_next()`; `src/job_manager.c`: `job_manager_cancel()`, `job_manager_update()` | Two-level fork/exec execution, wait/reap, exit status, time-based sequential dispatch, pending cancellation | Success 0, false 1, exec failure 127, forced fork failure, `ECHILD` reaping check, no early dispatch, cancellation/restart tests | No CPU-scheduling algorithms, preemption, or termination of an already-running job |
| CO3 | `src/process_manager.c`: `process_manager_run()`, `run_job_child()`, `report_child_result()`, `write_all()`, `read_all()`, `set_execution_result()` | Anonymous pipe carries actual `ExecutionResult`; second FD_CLOEXEC pipe reports command-launch failure; wait status decodes signals; EINTR retries | Received success, false/1 and forced exec/127 messages checked against statuses/logs | No named FIFO, custom signal handler, SIGALRM, kill-based control, process-group/session management; signal-caused execution result not exercised end to end |
| CO4 | `include/job_manager.h`: `Job` and size constants; `src/job_manager.c`: `validate_job_record()`, schedule/update/get; `src/menu.c`: `read_line()`, `parse_scheduled_time()`; `src/job_storage.c`: `job_storage_load()` | Fixed 100-record array, bounds and termination checks, validated numbers/status transitions, bounded input/messages | Validation message tied to accepted record; corrupt-record, capacity, 255/256-byte, NUL/CR/time tests; Valgrind zero-error lifecycle | No custom virtual-memory management, paging, page tables, allocation of the job array, or comprehensive fuzz proof |
| CO5 | `src/job_storage.c`: `job_storage_load()`, `job_storage_save()`, `job_storage_log()`, `job_storage_show_log()`; `src/process_manager.c`: `run_job_child()` | FILE I/O, descriptor open/dup2/close, persistent tab-separated records, temporary-file rename, append logs and stdout/stderr capture | Real output matched; status/exit-code database fields checked; cancelled/failed/success states survive restart; write errors exit 1 | No filesystem/VFS/inode/ext4 implementation, mmap, inter-instance locking, or fsync-based crash durability |
| CO6 | `src/scheduler.c`: `scheduler_run_due()`, `run_job_worker()`, `JobExecution`; `tests/test_process_failures.c`: `__wrap_pthread_create()` | Actual POSIX worker creation, copied job input, results synchronized by join before storage updates | One worker message per executed job; none for cancelled or creation-failed job; controlled EAGAIN failure marks Failed and propagates error | No simultaneous jobs, mutex, semaphore, condition variable, thread pool, or fault injection of pthread_join failure |

The README's conditional thread/pipe descriptions were replaced by verified
implementation details. Vague memory-tool claims were replaced by the scoped
Valgrind result. No FIFO, paging, mutex, semaphore, or parallel-job claim is
supported or made. Course mapping demonstrates selected concepts, not every
topic in the syllabus. Other rare pipe/fcntl/read/write/wait failures remain
un-injected, and no claim of exhaustive error-path coverage is made.

## F. README and GitHub checklist

- Complete in the corrected editor draft: title/description, supplied member names/roll numbers and faculty,
  section/course/department, objectives, technologies, architecture, source and
  folder descriptions, verified CO mapping, build/run/test commands, results,
  limitations, and live demonstration steps.
- Missing/needs confirmation: dedicated team name, class, and confirmation of
  existing personal/institutional details. No new details were invented.
- Unresolved: the pre-existing dirty README editor buffer and disk file differ.
  Terminal validation read the older 23,753-byte disk copy and found three
  missing-document links. The corrected draft links to this existing audit
  instead. No supported save-editor command was available to this audit, and
  the older disk version was not used to overwrite the user's draft. Documentation
  validation is therefore not fully passed; do not publish the stale disk copy.
- Present: all six C source files, five headers, Makefile, both shell suites,
  C test harness, coverage notes, and 23 PNG demonstrations.
- `docs/` was empty before this audit. No academic project report or PPT was
  found anywhere within the project. They still need to be prepared.
- `results/outputs/CO3_1.png`, `CO4.png`, and `CO6.png` were visually reviewed.
  CO6 shows worker output; the CO4 editor area contains older allocation code
  inconsistent with today's static job array. Refresh it before presenting.
  All PNGs were inventoried, but not all 23 were individually visually reviewed.
- Generated binaries/objects/dependencies, real database, temporary database,
  and logs are ignored. `.gitkeep`, source, docs, tests, and screenshots remain
  eligible for upload; `git check-ignore` verified representative paths.
- Git root is the parent `OSSP_Project`, not this project. No project files
  were tracked there (`git ls-files -- .` was empty). The scheduler folder was
  untracked. Do not treat an existing parent remote as proof of project upload.
- Verified parent remote:
  `https://github.com/ShaikSabiyaThabassum/2520030537_502_453_OS-SP_Project.git`.
  `git ls-remote --heads origin` returned main at
  `52c3e64dbf594382eb3b52912d5863e16909e4c6`.
  A separate scheduler repository and its uploaded contents are not verified.
  No repository, branch, commit, or upload was created by this audit.
- No project license file was found. Agree on licensing before public reuse;
  do not invent a license or copyright ownership without team approval.

## G. Remaining work

1. Reconcile/persist the corrected README editor buffer and rerun local-link validation.
2. Prepare the academic report and PPT, with accurate selected-concept CO mapping.
3. Supply the team name/class and confirm member/faculty details.
4. Create or verify the required separate project repository and upload the
   source, tests, README, docs, and screenshots, not generated data or binaries.
5. Refresh stale screenshots and rehearse the foreground-only demonstration.
6. Agree on a project license if public reuse is intended.
7. Optional future development: expand rare syscall/join failure tests and
   signal-result coverage. Locking, background execution, and parallel jobs
   are limitations, not requirements silently added to this application.

## H. Final recommendation

The code can be uploaded for development now: the complete existing suite
and expanded regression cases passed, with the real data preserved. Do not
call the hackathon submission final until the README persistence issue, report,
PPT, missing details, dedicated upload, and demonstration preparation are completed. No application
correctness blocker was reproduced in the tested scope; untested paths and
known limitations are explicitly listed above.