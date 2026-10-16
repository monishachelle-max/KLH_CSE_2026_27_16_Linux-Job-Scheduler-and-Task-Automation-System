#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
application="$project_root/build/linux-job-scheduler"
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

run_application()
{
    timeout 20 "$application"
}

mkdir -p "$temporary_directory/main" "$temporary_directory/invalid"
cd "$temporary_directory/main"
{
    for program in 1 2 4 2 3; do
        printf '1\n%s\n' "$program"
        if test "$program" = 2; then
            printf 'Hello from my scheduler!\n'
        fi
        date -d '+3 seconds' '+%Y-%m-%d %H:%M:%S'
    done
    printf '2\n4\n4\n'
    sleep 5
    printf '2\n3\n1\n3\n2\n3\n3\n4\n1\n4\n999\n5\n'
} | run_application >scheduler-output.txt 2>&1

grep -F 'Job 1: Succeeded (exit code 0).' scheduler-output.txt >/dev/null
grep -F 'Job 2: Succeeded (exit code 0).' scheduler-output.txt >/dev/null
grep -F 'Job 3: Failed (exit code 1).' scheduler-output.txt >/dev/null
test "$(grep -Fxc 'Job 1 executed by worker thread.' scheduler-output.txt)" -eq 1
test "$(grep -Fxc 'Job 2 executed by worker thread.' scheduler-output.txt)" -eq 1
test "$(grep -Fxc 'Job 3 executed by worker thread.' scheduler-output.txt)" -eq 1
test "$(grep -Fxc 'Job 5 executed by worker thread.' scheduler-output.txt)" -eq 1
grep -F 'Received execution result from child: Job completed successfully.' \
    scheduler-output.txt >/dev/null
grep -F 'Received execution result from child: Job failed with exit code 1.' \
    scheduler-output.txt >/dev/null
grep -F 'Job record validated successfully.' scheduler-output.txt >/dev/null
for job_id in 5; do
    grep -F "Job $job_id: Succeeded (exit code 0)." scheduler-output.txt >/dev/null
done
awk '
    /^Job scheduled successfully!$/ {
        getline; if ($0 != "Job ID: 1") exit 1
        getline; if ($0 != "Program: date") exit 1
        getline; if ($0 != "Status: PENDING") exit 1
        found = 1
        exit
    }
    END { if (!found) exit 1 }
' scheduler-output.txt
grep -F '1. date' scheduler-output.txt >/dev/null
grep -F '2. echo' scheduler-output.txt >/dev/null
grep -F '3. true' scheduler-output.txt >/dev/null
grep -F '4. false' scheduler-output.txt >/dev/null
if grep -E '^[5-7]\. (pwd|uname|whoami)$' scheduler-output.txt >/dev/null; then
    echo "Removed programs are still offered." >&2
    exit 1
fi
grep -Fx 'Hello from my scheduler!' logs/job_2.log >/dev/null
grep -F 'Process exited with status 0.' logs/job_1.log >/dev/null
grep -F 'Process exited with status 1.' logs/job_3.log >/dev/null
grep -F 'Job 4 was cancelled.' logs/job_4.log >/dev/null
grep -F 'Only pending jobs can be cancelled.' scheduler-output.txt >/dev/null
grep -F 'Job 999 was not found.' scheduler-output.txt >/dev/null
grep -F 'Pending' scheduler-output.txt >/dev/null
if grep -F 'Starting' logs/job_4.log >/dev/null; then
    echo "Cancelled job unexpectedly ran." >&2
    exit 1
fi
if grep -F 'Arguments (' scheduler-output.txt >/dev/null; then
    echo "An arguments prompt was unexpectedly displayed." >&2
    exit 1
fi

printf '2\n5\n' | run_application >restart-output.txt 2>&1
grep -F 'Succeeded' restart-output.txt >/dev/null
grep -F 'Failed' restart-output.txt >/dev/null
grep -F 'Cancelled' restart-output.txt >/dev/null

mkdir -p "$temporary_directory/interrupted/data"
cd "$temporary_directory/interrupted"
printf '9\t%s\t1\t0\techo\tInterrupted\n' "$(date +%s)" >data/jobs.db
printf '2\n3\n9\n5\n' | run_application >interrupted-output.txt 2>&1
grep -F 'Job 9: Failed' interrupted-output.txt >/dev/null
grep -F 'marked failed after an interrupted run' logs/job_9.log >/dev/null

cd "$temporary_directory/invalid"
printf '1\nrm\n1\n0\n1\n5\n1\n-1\n1\n1x\n1\n1\n2099-02-30 00:00:00\n1\n1\n2000-01-01 00:00:00\n1\n1\nnot a date\n0\n3\n-1\n4\nabc\n5\n' |
    run_application >invalid-output.txt 2>&1
grep -F 'Invalid program selection' invalid-output.txt >/dev/null
grep -F 'Invalid scheduled time' invalid-output.txt >/dev/null
grep -F 'Invalid selection' invalid-output.txt >/dev/null
grep -F 'Enter a positive numeric job ID.' invalid-output.txt >/dev/null
if test -e data/jobs.db; then
    echo "Invalid input unexpectedly created a job." >&2
    exit 1
fi

mkdir -p "$temporary_directory/inconsistent-record/data"
cd "$temporary_directory/inconsistent-record"
printf '1\t%s\t2\t1\tdate\t\n' "$(date -d '+1 minute' +%s)" >data/jobs.db
if printf '5\n' | run_application >record-output.txt 2>&1; then
    echo "Inconsistent job record was unexpectedly accepted." >&2
    exit 1
fi
grep -F 'Job record status and exit code are inconsistent.' record-output.txt >/dev/null

mkdir -p "$temporary_directory/legacy/data"
cd "$temporary_directory/legacy"
past_time=$(date -d '-1 minute' +%s)
printf '1\t%s\t0\t0\techo\tOld custom message\n2\t%s\t0\t0\tdate\t\n' \
    "$past_time" "$past_time" >data/jobs.db
printf '2\n5\n' | run_application >legacy-output.txt 2>&1
grep -Fx 'Old custom message' logs/job_1.log >/dev/null
grep -F 'Job 2: Succeeded (exit code 0).' legacy-output.txt >/dev/null
{
    printf '1\n2\nA persisted custom message\n'
    date -d '+1 minute' '+%Y-%m-%d %H:%M:%S'
    printf '5\n'
} | run_application >new-id-output.txt 2>&1
grep -Fx 'Job ID: 3' new-id-output.txt >/dev/null

for program in rm pwd uname whoami; do
    mkdir -p "$temporary_directory/unapproved-$program/data"
    cd "$temporary_directory/unapproved-$program"
    printf '1\t%s\t0\t0\t%s\t\n' "$past_time" "$program" >data/jobs.db
    if printf '5\n' | run_application >unapproved-output.txt 2>&1; then
        echo "An unapproved saved program was unexpectedly accepted: $program" >&2
        exit 1
    fi
    grep -F 'That program is not approved' unapproved-output.txt >/dev/null
done

echo "All scheduler integration tests passed."
