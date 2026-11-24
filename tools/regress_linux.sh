#!/usr/bin/env bash
# regress_linux.sh - THPS 2.0's full regression on Linux (2026-11-24): the three parts of tools/regress.ps1
# (menu tour, split screen, storage = tests/storage.ps1) with the same input scripts, problem patterns and checks.
# Every run writes into a fresh folder (never overwrites evidence). The menu tour's screenshot compare needs the
# reference set of private/reference (Windows); without it here the tour's shots are listed for a manual visual
# check and the part passes on exit code + problem lines + the shot count.
#
#   tools/regress_linux.sh [exe] [out dir]
#   exe default: ../private/install/data/thps2x (the dev layout: game/, saves/, data/ under install/)
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
PRIV=$(cd "$REPO/../private" 2>/dev/null && pwd)
EXE=${1:-$PRIV/install/data/thps2x}
OUT=${2:-$PRIV/evidence/regress_linux/$(date +%Y%m%d_%H%M%S)}
[ -e "$OUT" ] && { echo "exists: $OUT (evidence is write-once)"; exit 2; }
mkdir -p "$OUT"
PROBLEMS='\[NULLPTR\]|\[UNIMPL\]|FATAL|Failed to resolve|is not code|access violation|Access violation|unhandled exception|\[CRASH\]'
KNOWN_UNIMPL='\[UNIMPL\].*at 0x(00013A42|0002040E|0001DE63)\b'
FAILED=0
SUMMARY=()

line() {   # ok part detail path
    local s; s=$(printf '%-10s %s  %s  (%s)' "$2" "$([ "$1" = 1 ] && echo PASS || echo FAIL)" "$3" "$4")
    echo "$s"; SUMMARY+=("$s"); [ "$1" = 1 ] || FAILED=$((FAILED + 1))
}

# game <saves> <data> <script> <seconds> <shot prefix> [ENV=VAL...] -> exit code in $RC
game() {
    local saves=$1 data=$2 script=$3 secs=$4 prefix=$5; shift 5
    (cd "$(dirname "$EXE")/.." && env RECOMP_AC97_READY=1 RECOMP_NO_DIALOGS=1 THPS_DISPLAY_MODE=windowed \
        RECOMP_SHOT_PREFIX="$prefix" "$@" timeout $((secs + 30)) "$EXE" --max-seconds "$secs" \
        --save-dir "$saves" --data-dir "$data" --input-script "$REPO/tests/input/$script" >/dev/null 2>&1)
    RC=$?
}

problems() {   # log -> count of problem lines (known UNIMPL excluded)
    [ -f "$1" ] || { echo 1; return; }
    grep -a -E "$PROBLEMS" "$1" | grep -a -v -E "$KNOWN_UNIMPL" | wc -l
}

echo "regress_linux: $EXE -> $OUT"

# ── 1. menu tour ──
W=$OUT/work_menu_tour; mkdir -p "$W/saves" "$W/data" "$OUT/menu_tour"
game "$W/saves" "$W/data" menu_tour.txt 135 "$OUT/menu_tour/"
cp "$W/data/thps2x.log" "$OUT/menu_tour.log" 2>/dev/null
want=$(grep -c -E '^[0-9]+:shot=' "$REPO/tests/input/menu_tour.txt")
got=$(ls "$OUT/menu_tour/"*.bmp 2>/dev/null | wc -l)
bad=$(problems "$OUT/menu_tour.log")
ok=0; [ "$RC" = 0 ] && [ "$bad" = 0 ] && [ "$got" = "$want" ] && ok=1
line $ok "menu tour" "$got of $want shots, exit $RC, problem lines $bad; no reference set here: all shots manual visual check" "$OUT/menu_tour"

# ── 2. split screen ──
W=$OUT/work_split; mkdir -p "$W/saves" "$W/data" "$OUT/split"
game "$W/saves" "$W/data" split_handicap.txt 130 "$OUT/split/"
cp "$W/data/thps2x.log" "$OUT/split.log" 2>/dev/null
want=$(grep -c -E '^[0-9]+:shot=' "$REPO/tests/input/split_handicap.txt")
got=$(ls "$OUT/split/"*.bmp 2>/dev/null | wc -l)
bad=$(problems "$OUT/split.log")
ok=0; [ "$RC" = 0 ] && [ "$bad" = 0 ] && [ "$got" = "$want" ] && ok=1
line $ok "split" "$got of $want shots, exit $RC, problem lines $bad" "$OUT/split"

# ── 3. storage (tests/storage.ps1) ──
S=$OUT/storage; mkdir -p "$S"
SAVES=$OUT/work_storage/saves; DATA=$OUT/work_storage/data; SAVEROOT=$SAVES/UDATA/41560001
SFAIL=0; SPASS=0
check() { if [ "$1" = 1 ]; then echo "  PASS  $2" | tee -a "$S/storage.txt"; SPASS=$((SPASS + 1)); \
          else echo "  FAIL  $2" | tee -a "$S/storage.txt"; SFAIL=$((SFAIL + 1)); fi; }
srun() { local name=$1 script=$2 secs=$3; shift 3
         game "$SAVES" "$DATA" "$script" "$secs" "$S/${name}_" "$@"; cp "$DATA/thps2x.log" "$S/$name.log" 2>/dev/null
         echo "[$name] exit $RC" | tee -a "$S/storage.txt"; }
hashes() { [ -d "$SAVEROOT" ] && (cd "$SAVEROOT" && find . -mindepth 2 -type f -exec sha256sum {} + | sort); }
nfiles() { hashes | grep -c . ; }
same_as() { local a=$1 b; b=$(hashes); [ -n "$a" ] && [ -z "$(comm -23 <(echo "$a") <(echo "$b"))" ]; }
b() { if eval "$1"; then echo 1; else echo 0; fi; }
mkdir -p "$SAVES" "$DATA"

echo "1. new save" | tee -a "$S/storage.txt"
srun 1_save_new save_new.txt 75
h1=$(hashes); n1=$(nfiles)
check "$(b '[ "$n1" -ge 2 ]')" "save files written under saves/UDATA/41560001 ($n1 files)"
check "$(b '[ -f "$S/1_save_new_save_new_done.bmp" ]')" "screenshot after saving"
check "$(b '[ -f "$SAVES/storage_layout.txt" ]')" "saves/storage_layout.txt written"

echo "2. second save, killed before it is committed" | tee -a "$S/storage.txt"
srun 2_save_crash save_new.txt 75 RECOMP_SAVE_CRASH_TEST="CAREER B"
check "$(b '[ "$RC" = 3 ] && grep -a -q "RECOMP_SAVE_CRASH_TEST: terminating" "$S/2_save_crash.log"')" "run ended inside the save write (exit $RC)"
check "$(b 'same_as "$h1"')" "the existing save is byte-identical to before"
check "$(b '[ -z "$(find "$SAVEROOT" -type f -name "*CAREER B*" 2>/dev/null)" ]')" "the interrupted save file was not committed"
check "$(b '[ "$(find "$SAVES/.pending" -maxdepth 1 -type f 2>/dev/null | wc -l)" -ge 1 ]')" "unfinished copy left in saves/.pending"

echo "3. restart, load" | tee -a "$S/storage.txt"
srun 3_load_after_crash load_game_after_crash.txt 75
check "$(b 'grep -a -q "dropped unfinished save write" "$S/3_load_after_crash.log"')" "unfinished copy dropped at start"
check "$(b '[ "$(find "$SAVES/.pending" -maxdepth 1 -type f 2>/dev/null | wc -l)" = 0 ]')" "saves/.pending empty"
check "$(b '[ -f "$S/3_load_after_crash_load_done.bmp" ]')" "screenshot after loading"

echo "4. delete data/, load" | tee -a "$S/storage.txt"
h3=$(hashes)
rm -r -- "$OUT/work_storage/data"; mkdir -p "$DATA"
srun 4_load_without_data load_game_after_crash.txt 75
check "$(b 'same_as "$h3"')" "saves untouched by deleting data/"
check "$(b '[ -f "$S/4_load_without_data_load_done.bmp" ]')" "screenshot after loading"
check "$(b '[ "$(find "$DATA/cache/41560001" -maxdepth 1 -type f 2>/dev/null | wc -l)" -gt 0 ]')" "data/cache/41560001 rebuilt"

echo "5. overwrite the save from Options -> Load/Save, keeping its name (5 times)" | tee -a "$S/storage.txt"
rm -r -- "$OUT/work_storage/saves" "$OUT/work_storage/data"; mkdir -p "$SAVES" "$DATA"
srun 5_save_new save_new.txt 75
n4=$(nfiles)
check "$(b '[ "$n4" -ge 2 ] && [ "$(find "$SAVEROOT" -mindepth 1 -maxdepth 1 -type d | wc -l)" = 1 ]')" "one fresh save to overwrite"
for i in 1 2 3 4 5; do
    srun 5_overwrite_$i save_overwrite.txt 85 RECOMP_SAVE_TRACE=1
    L=$S/5_overwrite_$i.log
    check "$(b '[ "$RC" = 0 ] && grep -a -q "\[SAVE\] overwrite \"CAREER A\"" "$L"')" "run $i: overwrite mode taken for the kept name (exit $RC)"
    check "$(b '! grep -a -q -E "name-exists \"CAREER A\" .* -> 1 ret=0x00110759" "$L"')" "run $i: no 'already exists' check failure"
    check "$(b '[ "$(find "$SAVEROOT" -mindepth 1 -maxdepth 1 -type d | wc -l)" = 1 ]')" "run $i: still exactly one save folder"
    check "$(b '[ "$(nfiles)" = "$n4" ]')" "run $i: the save still has all its files"
done
ok=0; [ "$SFAIL" = 0 ] && ok=1
line $ok "storage" "$SPASS checks passed, $SFAIL failed" "$S"

printf '%s\n' "${SUMMARY[@]}" > "$OUT/summary.txt"
if [ "$FAILED" -gt 0 ]; then echo "regress_linux: $FAILED part(s) FAILED"; exit 1; fi
echo "regress_linux: all parts passed"; exit 0
