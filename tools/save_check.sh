#!/bin/sh
# Проверка сохранений: мир после записи и чтения идёт бит в бит так же, как без них.
# Пробные отряды бьются телами на дороге; запись посреди боя, затем ещё минута мира.
#   1) прогон 775 с, запись, ещё 60 с — сумма мира
#   2) новый процесс: чтение, 60 с — сумма
#   3) прогон 835 с без записи — сумма
# Все три должны совпасть. ./tools/save_check.sh [бинарник] [папка], на Linux без экрана — под xvfb-run
set -e
BIN=${1:-./build/anathemyre}
DIR=${2:-build}
CAM="--cam 1026 249 2.0 0.3 9"
sum() { grep "МИР: шаг" | tail -1 | sed 's/.*сумма //'; }
a=$($BIN --bands-test $CAM --warp 775 --save "$DIR/check.sav" --sum-after 60 2>&1 | tee "$DIR/save1.txt" | sum)
b=$($BIN $CAM --load "$DIR/check.sav" --sum-after 60 2>&1 | tee "$DIR/save2.txt" | sum)
c=$($BIN --bands-test $CAM --warp 835 --sum-after 0 2>&1 | tee "$DIR/save3.txt" | sum)
echo "с записью $a, после чтения $b, без записи $c"
grep -h "СОХРАНЕНИЕ" "$DIR/save1.txt" "$DIR/save2.txt"
! grep -q "СБОЙ" "$DIR/save1.txt" "$DIR/save2.txt" "$DIR/save3.txt"
test -n "$a" && test "$a" = "$b" && test "$a" = "$c"
