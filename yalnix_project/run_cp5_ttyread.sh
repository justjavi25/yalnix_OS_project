#!/bin/sh
set -eu

make clean
make

rm -f TRACE TTYLOG TTYLOG.0 TTYLOG.1 TTYLOG.2 TTYLOG.3 cp5_tty_read_input.txt
cat > cp5_tty_read_input.txt <<'INPUT'
abcdefghi
second
INPUT

echo "Running: timeout 8 ./yalnix -I0cp5_tty_read_input.txt test/cp5_tty_read"
timeout 8 ./yalnix -I0cp5_tty_read_input.txt test/cp5_tty_read || true

echo "== TRACE summary =="
grep -E "cp5_tty_read|TtyRead|TRAP_TTY|memory trap|aborting|ERROR|FAIL|PASS" TRACE || true

if grep -q "cp5_tty_read: PASS" TRACE &&
   grep -q "cp5_tty_read: first read returned 4" TRACE &&
   grep -q "cp5_tty_read: second read returned 6" TRACE &&
   grep -q "cp5_tty_read: third read returned 7" TRACE &&
   ! grep -Eq "memory trap|aborting|FAIL" TRACE; then
    echo "CP5 TtyRead smoke test PASS"
else
    echo "CP5 TtyRead smoke test FAIL"
    exit 1
fi
