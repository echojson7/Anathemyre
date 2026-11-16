# Проверки CI параллельно (docs/build.md, «CI»). Подключение: . tools/ci_par.sh (bash).
#   par ИМЯ ПРЕДЕЛ_С команда…   фоном, вывод в ИМЯ.log; дольше ПРЕДЕЛ_С секунд — снять (зависание падает быстро)
#   par_wait [ХВОСТ]             дождаться всех: время и хвост журнала каждой (ХВОСТ строк, при ошибке 80);
#                                код 1, если упала хоть одна
# Все задачи на одной машине: годится для однопоточного (шаг мира, создание конвейеров lavapipe), где ядра
# простаивают. Окна под Xvfb — каждое со своим номером экрана (xvfb-run -n), у -a гонка при одновременном старте.
_par_pids=()
_par_names=()

par() {
    local name=$1 limit=$2
    shift 2
    (
        t0=$SECONDS
        rc=0  # «|| rc=$?»: под bash -e упавшая команда сняла бы подоболочку до записи .rc
        timeout -k 20 "$limit" "$@" > "$name.log" 2>&1 || rc=$?
        echo "$rc $((SECONDS - t0))" > "$name.rc"
        exit "$rc"
    ) &
    _par_pids+=($!)
    _par_names+=("$name")
}

par_wait() {
    local keep=${1:-5} fail=0 i n rc sec
    for i in "${!_par_pids[@]}"; do
        n=${_par_names[$i]}
        wait "${_par_pids[$i]}" || true  # код читаем из .rc: под bash -e упавший wait снял бы шаг без журнала
        read -r rc sec < "$n.rc"
        if [ "$rc" = 0 ]; then
            echo "=== $n: ок, $sec с"
            tail -n "$keep" "$n.log"
        else
            fail=1
            if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
                echo "::error::$n: снято по пределу времени ($sec с) — зависло?"
            else
                echo "::error::$n: код $rc, $sec с"
            fi
            tail -n 80 "$n.log"
        fi
    done
    _par_pids=()
    _par_names=()
    return "$fail"
}
