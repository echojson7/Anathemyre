#!/bin/sh
# Проверка описаний и модов (docs/content.md):
#   1) порядок описаний другой: мир, записанный со встроенными, читается, пишется и снова читается со встроенными —
#      сумма та же, что без записи (номера вещей пересчитаны по именам туда и обратно)
#   2) мод правит поле и добавляет запись через parent
#   3) сохранение с вещью мода без мода не читается, мир не тронут
#   4) ошибка в файле мода: файл и строка в журнале, игра не стартует
#   5) мод правит материал (цвет, impact) и добавляет свой; оружие с несуществующим материалом не стартует
#   6) неизвестный класс impact у материала мода: файл и строка в журнале, игра не стартует
#   7) мод правит числа боя (tuning combat): бой арены идёт иначе; неизвестное число — файл и строка, не стартует
# Код выхода прогонов с окном не смотрим (под ASan драйвер Vulkan оставляет утечку), только журнал.
# ./tools/defs_check.sh [бинарник] [папка], на Linux без экрана — под xvfb-run
set -e
# Упало — последние строки журнала того прогона
trap 'st=$?; [ $st -ne 0 ] && for f in "$DIR"/defs[0-9].txt; do echo "== $f"; tail -15 "$f"; done; exit $st' EXIT
BIN=${1:-./build/anathemyre}
DIR=${2:-build}
DEFS=data/defs
CAM="--cam 1026 249 2.0 0.3 9"
sum() { grep "МИР: шаг" | tail -1 | sed 's/.*сумма //'; }
rm -rf "$DIR/defs_rot" "$DIR/mods_ok" "$DIR/mods_bad" "$DIR/mods_bad2" "$DIR/mods_bad3" "$DIR/mods_none" "$DIR/mods_boj" \
    "$DIR/mods_boj_bad"
mkdir -p "$DIR/defs_rot" "$DIR/mods_ok/proba" "$DIR/mods_bad/slom" "$DIR/mods_bad2/kraska" "$DIR/mods_bad3/zvuk" \
    "$DIR/mods_none" "$DIR/mods_boj/boj" "$DIR/mods_boj_bad/boj"
cp $DEFS/*.txt "$DIR/defs_rot/"
# Заголовки раньше всех файлов: номера идут задом наперёд, поля заполняются из обычных файлов
cat > "$DIR/defs_rot/0order.txt" <<'END'
weapon balka
weapon bagor
weapon sekach
weapon ressora
armor pants
armor greaves
armor bucket
armor plate
armor quilt
armor jacket
armor rags
goods belt
goods pack
goods mitts
goods boots
goods wrap
goods token
goods fuel
goods part
goods scrap
goods bandage
goods meat
goods rusk
goods water
END
cat > "$DIR/mods_ok/proba/weapons.txt" <<'END'
# Проба: секач потяжелее и рессора из лучшей стали
weapon sekach
    weight 8
weapon ressora_stal
    parent ressora
    name "СТАЛЬНАЯ РЕССОРА"
    common yes
    damage 28 4 14
END
cat > "$DIR/mods_ok/proba/materials.txt" <<'END'
# Проба: ржавчина темнее везде и звучит деревом, своя сталь от клинковой
material rust
    color 0.38 0.2 0.11
    impact wood
material proba_stal
    parent blade_steel
    color 0.3 0.3 0.31
END
printf 'material rust\n    impact plastic\n' > "$DIR/mods_bad3/zvuk/materials.txt"
printf 'weapon kosa_kraska\n    parent sekach\n    paint nosuch\n    rod 0 0.5 0.02 0\n' > "$DIR/mods_bad2/kraska/weapons.txt"
printf 'tuning combat\n    morale_flee 60\n    speed_ready 2\n' > "$DIR/mods_boj/boj/combat.txt"
printf 'tuning combat\n    morale_flee 60\n    nosuch 1\n' > "$DIR/mods_boj_bad/boj/combat.txt"
printf 'weapon kosa_slom\n    parent sekach\n    damage 30 много 0\n' > "$DIR/mods_bad/slom/weapons.txt"

a=$($BIN --bands-test $CAM --warp 775 --save "$DIR/defs1.sav" --sum-after 60 2>&1 | tee "$DIR/defs1.txt" | sum)
$BIN --mods "$DIR/mods_none" --defs "$DIR/defs_rot" $CAM --load "$DIR/defs1.sav" --save "$DIR/defs2.sav" --sum-after 0 > "$DIR/defs2.txt" 2>&1 || true
grep -q "мир записан" "$DIR/defs2.txt"
b=$($BIN --mods "$DIR/mods_none" $CAM --load "$DIR/defs2.sav" --sum-after 60 2>&1 | tee "$DIR/defs3.txt" | sum)
echo "без записи $a, через другой порядок описаний $b"
test -n "$a" && test "$a" = "$b"

# Мод: секач 8 кг, новая рессора от прежней; в общем запасе — попадёт в мир
$BIN --mods "$DIR/mods_ok" --bands-test $CAM --warp 1 --save "$DIR/defs_mod.sav" --sum-after 0 > "$DIR/defs4.txt" 2>&1 || true
n=$(grep -c "^weapon " $DEFS/weapons.txt)
grep -q "оружия $((n + 1)), .* модов 1" "$DIR/defs4.txt"
m=$(grep -c "^material " $DEFS/materials.txt)
grep -q "материалов $((m + 1))," "$DIR/defs4.txt"
grep -q "мир записан" "$DIR/defs4.txt"
# Без мода не читается
if $BIN --mods "$DIR/mods_none" $CAM --load "$DIR/defs_mod.sav" --sum-after 0 > "$DIR/defs5.txt" 2>&1; then exit 1; fi
grep -q "ressora_stal" "$DIR/defs5.txt"
grep -q "мир не тронут" "$DIR/defs5.txt"
# Ошибка в файле мода
if $BIN --mods "$DIR/mods_bad" --bench arena 1 > "$DIR/defs6.txt" 2>&1; then exit 1; fi
grep -q "slom/weapons.txt:3: не число" "$DIR/defs6.txt"
if $BIN --mods "$DIR/mods_bad2" --bench arena 1 > "$DIR/defs7.txt" 2>&1; then exit 1; fi
grep -q "kosa_kraska красится материалом nosuch" "$DIR/defs7.txt"
if $BIN --mods "$DIR/mods_bad3" --bench arena 1 > "$DIR/defs8.txt" 2>&1; then exit 1; fi
grep -q "zvuk/materials.txt:2: неизвестное слово: plastic" "$DIR/defs8.txt"
# Числа боя мода: другой бой арены; неизвестное число не пускает игру
c0=$($BIN --mods "$DIR/mods_none" --bench arena 20 2>&1 | tail -1)
c1=$($BIN --mods "$DIR/mods_boj" --bench arena 20 2>&1 | tail -1)
echo "бой арены: без мода — $c0, с модом чисел — $c1"
test -n "$c0" && test "$c0" != "$c1"
if $BIN --mods "$DIR/mods_boj_bad" --bench arena 1 > "$DIR/defs9.txt" 2>&1; then exit 1; fi
grep -q "boj/combat.txt:3: у tuning combat нет числа nosuch" "$DIR/defs9.txt"
echo "моды: правка и parent читаются, материал мода читается, сохранение без мода отклонено, числа боя правятся, ошибки указаны"
