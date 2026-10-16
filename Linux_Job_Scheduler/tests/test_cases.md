# Scheduler test coverage

Run the automated integration tests from the project root:

```sh
make test
```

The test script uses an isolated temporary working directory so it does not
change the user's `data/jobs.db` or `logs/` files. It checks:

- Numbered program selection and the requested scheduling confirmation format.
- Scheduling and listing pending tasks without an Arguments prompt.
- Automatic execution while the application waits for input.
- Capturing program output and exit information in per-job logs.
- Execution of all four approved programs, including a custom `echo` message.
- Successful and unsuccessful process exit statuses.
- Exactly one worker-thread execution message for each executed job; no
  message for the cancelled job.
- Controlled worker-thread creation failure is logged as a failed job and
  does not print a worker-execution message.
- Real anonymous-pipe result delivery for successful commands, `false` exit
  code 1, and controlled exec failure exit code 127.
- Job-record validation output only after valid scheduling records pass field
  checks, and rejection of inconsistent persisted status/exit-code fields.
- Cancelling a pending task and verifying it stays unexecuted after its due time.
- Rejecting invalid program numbers, job IDs, calendar dates, and past times.
- Rejecting cancellation of finished or unknown jobs.
- Retaining job statuses after restarting the application.
- Recovery of interrupted jobs and continuing unique IDs after restart.
- Preserving saved custom echo messages across restart.
- Rejecting unapproved programs in a saved database.
- Rejecting the removed programs `pwd`, `uname`, and `whoami`.

The detailed suite (`tests/test_verification.sh`) additionally checks:

- Each of the four programs in its own scheduling session, with Pending
  status inspected before execution and final status/logs inspected afterward.
- Actual output compared to the current environment, including date/time,
  custom echo text and empty true/false output.
- No early dispatch, and no more than two seconds' delay in the test run.
- Execution while an unfinished input line is waiting for its newline.
- Clean fatal-error exit on a database-write failure at each input prompt.
- Duplicate, negative, zero, overflowing, and exhausted persisted IDs rejected at startup.
- Invalid saved status/time, extra fields, embedded NUL, and 101-record databases rejected.
- Calendar boundaries, invalid leap dates, a valid leap date, past/current
  times, overlong lines, and unknown job IDs.
- The 100-job storage limit, including acceptance of job 100 from 99 saved records and rejection of attempt 101.
- Controlled execv/fork failures and child reaping using a test-only binary.
- Pending jobs retained across shutdown and executed after an overdue restart.
- Recompilation of dependent modules when the job header changes.
- Automatic display of the real execution log after completion.
- Explicit foreground-stop reminder when exiting.
- Rejection of embedded NUL/carriage returns instead of silently changing text.
- CRLF input support and exact date/time separators.
- Acceptance of a 255-byte echo message and rejection of overlong messages.
- Preservation of the real exit code during a controlled post-exit log failure.
- Worker completion is joined before final status, logs, and scheduler output
  are read or updated.

`make test` builds and runs both suites. Actual transcripts and logs are
retained under `build/verification-evidence/`; `make clean` removes them.
Database-write failure transcripts and actual exit codes are retained for all
six input prompts. Failed verification runs copy their temporary files to
`build/verification-evidence/failed-run.*` before cleanup; termination signals
exit rather than resuming in a deleted directory. The C failure harness has a
20-second deadline and runs only against the verification script's temporary
database. Do not invoke that harness directly against real project data.

Allow at least two minutes for the full suite. Individual application sessions
have shorter explicit timeouts; a missing transcript after external termination
is not evidence of a database failure. The audit results and optional Valgrind
command are recorded in `docs/audit-verification.md`.
