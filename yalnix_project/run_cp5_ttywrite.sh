#!/bin/sh
set -eu

make clean
make
rm -f TRACE TTYLOG TTYLOG.0 TTYLOG.1 TTYLOG.2 TTYLOG.3

timeout 8 ./yalnix test/cp5_tty_write || true

echo "== TRACE summary =="
grep -E "cp5_tty_write|TtyWrite|TRAP_TTY|memory trap|aborting|ERROR|FAIL|PASS" TRACE || true

echo "== TTYLOG.1 markers =="
grep -E "CP5_TTY_WRITE_BEGIN|CP5_TTY_WRITE_END" TTYLOG.1

if grep -q "CP5_TTY_WRITE_BEGIN" TTYLOG.1 &&
   grep -q "CP5_TTY_WRITE_END" TTYLOG.1 &&
   grep -q "cp5_tty_write: PASS" TRACE &&
   ! grep -Eq "memory trap|aborting|FAIL" TRACE; then
    echo "CP5 TtyWrite smoke test PASS"
else
    echo "CP5 TtyWrite smoke test FAIL"
    exit 1
fi
