#!/bin/sh
set -eu
export LC_ALL=C

project_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
application="$project_root/build/linux-job-scheduler"
failure_application="$project_root/build/test-process-failures"
evidence="$project_root/build/verification-evidence"
temporary_directory=$(mktemp -d)
cleanup()
{
    result=$?
    trap - EXIT
    if test "$result" -ne 0; then
        failure_evidence=$(mktemp -d "$evidence/failed-run.XXXXXX")
        cp -R "$temporary_directory/." "$failure_evidence/" || :
        printf 'Verification exited with status %s. Diagnostics: %s\n' \
            "$result" "$failure_evidence" >&2
    fi
    rm -rf "$temporary_directory"
    exit "$result"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$evidence"

for program in date echo true false; do
    case "$program" in
        date) selection=1 ;;
        echo) selection=2 ;;
        true) selection=3 ;;
        false) selection=4 ;;
    esac
    mkdir "$temporary_directory/$program"
    cd "$temporary_directory/$program"
    {
        scheduled=$(date -d '+3 seconds' +%s)
        printf '%s\n' "$scheduled" >scheduled-epoch.txt
        printf '1\n%s\n' "$selection"
        if test "$program" = echo; then
            printf 'Hello from my scheduler!\n'
        fi
        date -d "@$scheduled" '+%Y-%m-%d %H:%M:%S'
        printf '2\n3\n1\n'
        sleep 1
        test ! -e logs/job_1.log
        : >not-executed-early.txt
        sleep 4
        if ! grep -F 'Process exited with status' logs/job_1.log >/dev/null; then
            echo "$program did not finish automatically while waiting for input." >&2
            exit 1
        fi
        : >executed-before-input.txt
        printf '2\n3\n1\n5\n'
    } | timeout 15 "$application" >transcript.txt 2>&1

    grep -Fx 'Job ID: 1' transcript.txt >/dev/null
    grep -Fx "Program: $program" transcript.txt >/dev/null
    grep -Fx 'Status: PENDING' transcript.txt >/dev/null
    test -f executed-before-input.txt
    test -f not-executed-early.txt
    grep -F "Job 1: Pending; program: $program" transcript.txt >/dev/null
    grep -F 'Job 1 is now running.' logs/job_1.log >/dev/null
    code=0
    status=Succeeded
    database_status=2
    if test "$program" = false; then
        code=1
        status=Failed
        database_status=3
    fi
    grep -F "Job 1: $status; program: $program" transcript.txt >/dev/null
    grep -F "Process exited with status $code." logs/job_1.log >/dev/null
    if test "$program" = false; then
        grep -F 'Received execution result from child: Job failed with exit code 1.' \
            transcript.txt >/dev/null
    else
        grep -F 'Received execution result from child: Job completed successfully.' \
            transcript.txt >/dev/null
    fi
    awk -F '\t' -v status="$database_status" -v code="$code" \
        '$1 == 1 && $3 == status && $4 == code { found = 1 } END { exit !found }' data/jobs.db
    sed '/^\[/d' logs/job_1.log >actual-output.txt
    case "$program" in
        date)
            actual_epoch=$(date -d "$(cat actual-output.txt)" +%s)
            scheduled=$(cat scheduled-epoch.txt)
            test "$actual_epoch" -ge "$scheduled"
            test "$actual_epoch" -le "$((scheduled + 2))"
            ;;
        echo) printf 'Hello from my scheduler!\n' >expected-output.txt ;;
        false|true) : >expected-output.txt ;;
    esac
    if test "$program" != date; then
        cmp expected-output.txt actual-output.txt
    fi
    if test "$program" = echo || test "$program" = date; then
        grep -Fx "$(cat actual-output.txt)" transcript.txt >/dev/null
    fi
    started=$(sed -n '1s/^\[\([^]]*\)\].*/\1/p' logs/job_1.log)
    started_epoch=$(date -d "$started" +%s)
    scheduled=$(cat scheduled-epoch.txt)
    test "$started_epoch" -ge "$scheduled"
    test "$started_epoch" -le "$((scheduled + 2))"
    mkdir -p "$evidence/$program"
    cp transcript.txt actual-output.txt scheduled-epoch.txt "$evidence/$program/"
    cp logs/job_1.log "$evidence/$program/"
    printf '\nProgram: %s | Status: %s | Exit code: %s | Dispatch delay: %ss\n' \
        "$program" "$status" "$code" "$((started_epoch - scheduled))"
    if test -s actual-output.txt; then
        cat actual-output.txt
    else
        echo '(no standard output)'
    fi
done

cd "$temporary_directory"
mkdir partial
cd partial
{
    printf '1\n2\nPartial input message\n'
    date -d '+2 seconds' '+%Y-%m-%d %H:%M:%S'
    printf '2'
    sleep 4
    grep -F 'Process exited with status 0.' logs/job_1.log >/dev/null
    : >executed-before-input.txt
    printf '\n5\n'
} | timeout 10 "$application" >transcript.txt 2>&1
grep -F 'Job 1: Succeeded' transcript.txt >/dev/null
test -f executed-before-input.txt
echo 'PASS: unfinished input does not block scheduled execution.'

for where in menu program time message status cancel; do
    mkdir "$temporary_directory/error-$where"
    cd "$temporary_directory/error-$where"
    mkdir data data/jobs.db.tmp
    printf '1\t%s\t0\t0\tdate\t\n' "$(date -d '+2 seconds' +%s)" >data/jobs.db
    case "$where" in
        menu) prefix='' ;;
        program) prefix='1\n' ;;
        time) prefix='1\n1\n' ;;
        message) prefix='1\n2\n' ;;
        status) prefix='3\n' ;;
        cancel) prefix='4\n' ;;
    esac
    set +e
    {
        printf '%b' "$prefix"
        sleep 3
    } | timeout 5 "$application" >transcript.txt 2>&1
    result=$?
    set -e
    mkdir -p "$evidence/error-$where"
    cp transcript.txt "$evidence/error-$where/"
    printf '%s\n' "$result" >"$evidence/error-$where/exit-code.txt"
    test "$result" -eq 1
    grep -F 'Cannot write temporary job database' transcript.txt >/dev/null
done
echo 'PASS: database-write failure exits with status 1 instead of looping.'

for bad_record in duplicate negative zero overflow max-id status time fields nul overcapacity; do
    mkdir "$temporary_directory/record-$bad_record"
    cd "$temporary_directory/record-$bad_record"
    mkdir data
    case "$bad_record" in
        duplicate) printf '1\t1\t2\t0\tdate\t\n1\t1\t2\t0\ttrue\t\n' >data/jobs.db ;;
        negative) printf '%s\t1\t2\t0\tdate\t\n' '-1' >data/jobs.db ;;
        zero) printf '0\t1\t2\t0\ttrue\t\n' >data/jobs.db ;;
        overflow) printf '999999999999999999999999999999999\t1\t2\t0\ttrue\t\n' >data/jobs.db ;;
        max-id) printf '%s\t1\t2\t0\ttrue\t\n' "$(getconf ULONG_MAX)" >data/jobs.db ;;
        status) printf '1\t1\t99\t0\ttrue\t\n' >data/jobs.db ;;
        time) printf '1\t-1\t2\t0\ttrue\t\n' >data/jobs.db ;;
        fields) printf '1\t1\t2\t0\ttrue\t\textra\n' >data/jobs.db ;;
        nul) printf '1\t1\t2\t0\techo\tvalid\000hidden\n' >data/jobs.db ;;
        overcapacity) awk 'BEGIN { for (i = 1; i <= 101; i++) printf "%d\t1\t2\t0\ttrue\t\n", i }' >data/jobs.db ;;
    esac
    set +e
    printf '5\n' | timeout 3 "$application" >transcript.txt 2>&1
    result=$?
    set -e
    mkdir -p "$evidence/record-$bad_record"
    cp transcript.txt "$evidence/record-$bad_record/"
    test "$result" -eq 1
done
echo 'PASS: corrupt records, invalid/exhausted IDs and 101-record databases are rejected.'

mkdir "$temporary_directory/validation"
cd "$temporary_directory/validation"
{
    for invalid_time in \
        '2099-02-29 12:00:00' \
        '2099-04-31 12:00:00' \
        '2099-13-01 12:00:00' \
        '2099-01-00 12:00:00' \
        '2099-01-01 24:00:00' \
        '2099-01-01 12:60:00' \
        '2099-01-01 12:00:60' \
        '2099-01-01 12:00:00extra' \
        '2000-01-01 12:00:00'; do
        printf '1\n1\n%s\n' "$invalid_time"
    done
    printf '1\n1\n'
    date '+%Y-%m-%d %H:%M:%S'
    awk 'BEGIN { for (i = 0; i < 600; i++) printf "x"; print "" }'
    printf '3\n999\n2\n5\n'
} | timeout 10 "$application" >transcript.txt 2>&1
test "$(grep -c 'Invalid scheduled time' transcript.txt)" -eq 10
grep -F 'Input is too long' transcript.txt >/dev/null
grep -F 'Job 999 was not found' transcript.txt >/dev/null
grep -F 'No scheduled jobs' transcript.txt >/dev/null
test ! -e data/jobs.db
printf '1\n1\n2096-02-29 12:00:00\n5\n' |
    timeout 5 "$application" >leap-year.txt 2>&1
grep -Fx 'Status: PENDING' leap-year.txt >/dev/null
echo 'PASS: calendar, leap-year, past/current-time, overlong input and unknown-job validation.'

mkdir "$temporary_directory/messages"
cd "$temporary_directory/messages"
{
    for message in 'Hello with spaces!' '$(touch should-not-exist); %s \n' ''; do
        printf '1\n2\n%s\n' "$message"
        date -d '+3 seconds' '+%Y-%m-%d %H:%M:%S'
    done
    sleep 5
    printf '5\n'
} | timeout 10 "$application" >transcript.txt 2>&1
grep -Fx 'Hello with spaces!' logs/job_1.log >/dev/null
grep -Fx '$(touch should-not-exist); %s \n' logs/job_2.log >/dev/null
grep -Fx '' logs/job_3.log >/dev/null
test ! -e should-not-exist
printf '1\n2\n-n\n1\n2\n-e\n1\n2\nbad\tmessage\n5\n' |
    timeout 5 "$application" >invalid-messages.txt 2>&1
test "$(grep -c 'Message cannot be an echo option' invalid-messages.txt)" -eq 2
grep -F 'Input cannot contain embedded control characters.' invalid-messages.txt >/dev/null
echo 'PASS: custom and empty echo messages, literal shell text, and message validation.'

mkdir "$temporary_directory/input-boundaries"
cd "$temporary_directory/input-boundaries"
{
    printf '1\n2\nEmbedded\000NUL\n1\n2\nEmbedded\rCR\n1\n1\n2099-01-01\t12:00:00\n'
    printf '1\n1\n2099-1-01 012:00:00\n'
    printf '3\n -1\n'
    printf '1\n2\n'
    awk 'BEGIN { for (i = 0; i < 256; i++) printf "a"; print "" }'
    printf '1\r\n2\r\n'
    awk 'BEGIN { for (i = 0; i < 255; i++) printf "b"; print "\r" }'
    date -d '+3 seconds' '+%Y-%m-%d %H:%M:%S'
    sleep 5
    printf '5\n'
} | timeout 12 "$application" >transcript.txt 2>&1
test "$(grep -c 'Input cannot contain embedded control characters.' transcript.txt)" -eq 3
grep -F 'Invalid scheduled time.' transcript.txt >/dev/null
grep -F 'Input is too long' transcript.txt >/dev/null
grep -F 'Enter a positive numeric job ID.' transcript.txt >/dev/null
awk 'BEGIN { for (i = 0; i < 255; i++) printf "b"; print "" }' >expected.txt
sed '/^\[/d' logs/job_1.log >actual.txt
cmp expected.txt actual.txt
grep -F 'will NOT run until you restart' transcript.txt >/dev/null
echo 'PASS: exact-format time, NUL/CR rejection, CRLF input and 255-byte message boundary.'

mkdir "$temporary_directory/capacity"
cd "$temporary_directory/capacity"
mkdir data
awk 'BEGIN { for (i = 1; i <= 100; i++) printf "%d\t1\t2\t0\ttrue\t\n", i }' >data/jobs.db
printf '1\n1\n2099-01-01 12:00:00\n5\n' |
    timeout 5 "$application" >transcript.txt 2>&1
grep -F 'The job limit of 100 has been reached.' transcript.txt >/dev/null
test "$(wc -l <data/jobs.db)" -eq 100
echo 'PASS: the 100-job limit is enforced without corrupting saved jobs.'

mkdir "$temporary_directory/capacity-boundary"
cd "$temporary_directory/capacity-boundary"
mkdir data
awk 'BEGIN { for (i = 1; i <= 99; i++) printf "%d\t1\t2\t0\ttrue\t\n", i }' >data/jobs.db
printf '1\n1\n2099-01-01 12:00:00\n1\n1\n2099-01-01 12:00:00\n5\n' |
    timeout 5 "$application" >transcript.txt 2>&1
grep -Fx 'Job ID: 100' transcript.txt >/dev/null
grep -F 'The job limit of 100 has been reached.' transcript.txt >/dev/null
test "$(wc -l <data/jobs.db)" -eq 100
echo 'PASS: the 100th job is accepted and the 101st scheduling attempt is rejected.'

mkdir "$temporary_directory/failures"
cd "$temporary_directory/failures"
mkdir data
printf '1\t1\t0\t0\tdate\t\n2\t%s\t0\t0\tdate\t\n3\t%s\t0\t0\ttrue\t\n4\t%s\t0\t0\ttrue\t\n' \
    "$(date -d '+2 seconds' +%s)" "$(date -d '+5 seconds' +%s)" \
    "$(date -d '+8 seconds' +%s)" >data/jobs.db
timeout 20 "$failure_application" >transcript.txt 2>&1
grep -F 'Controlled execv/fork/thread-creation failures' transcript.txt
if grep -F 'Job 4 executed by worker thread.' transcript.txt >/dev/null; then
    echo 'A job without a worker thread reported worker execution.' >&2
    exit 1
fi
grep -F 'Received execution result from child: Job failed with exit code 127.' \
    transcript.txt >/dev/null
grep -F 'Received execution result from child: Job completed successfully.' \
    transcript.txt >/dev/null
mkdir -p "$evidence/failures"
cp transcript.txt "$evidence/failures/"
cp logs/job_1.log "$evidence/failures/"
cp logs/job_3.log "$evidence/failures/job_3.log"

mkdir "$temporary_directory/restart"
cd "$temporary_directory/restart"
{
    printf '1\n2\nSaved message after restart\n'
    date -d '+2 seconds' '+%Y-%m-%d %H:%M:%S'
    printf '5\n'
} | timeout 5 "$application" >before.txt 2>&1
test ! -e logs/job_1.log
sleep 3
test ! -e logs/job_1.log
printf '3\n1\n5\n' | timeout 5 "$application" >after.txt 2>&1
grep -F 'Job 1: Succeeded' after.txt >/dev/null
grep -Fx 'Saved message after restart' logs/job_1.log >/dev/null
echo 'PASS: pending job survives exit and executes when restarted after its due time.'

cd "$project_root"
make -n -W include/job_manager.h all >"$evidence/header-rebuild.txt"
for source in menu job_manager scheduler process_manager job_storage; do
    grep -F "src/$source.c" "$evidence/header-rebuild.txt" >/dev/null
done
echo 'PASS: changing the job header rebuilds dependent source files.'

echo "All detailed verification tests passed. Evidence: $evidence"
