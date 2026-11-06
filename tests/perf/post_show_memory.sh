#!/bin/bash
# Menü açılıp kapandıktan sonraki kalıcı bellek (private dirty) regresyon testi.
# Her döngü: servisi yeniden başlat -> 2 s -> göster -> 1 s -> gizle -> 1 s -> ölç.
# Neden ayrı: E2E çalışan daemon'u ölçer ve menüyü hiç açmaz (boşta ~32 MB);
# 2026-11-06'da ilk açılıştan sonra zaman zaman 51-58 MB görüldü (JS heap +1.4-3 MB,
# C heap +1.2-4 MB, kapanıştan sonra kalıcı). Doğal tetikleyici yeniden üretilemedi;
# aynı sınıf açılışta ~3 MB JS çöpü enjekte edilerek üretildi: gc'siz 65.5 MB, kapanışta
# gc() ile 49.6 MB. Menu.qml closeTimer artık gc() çağırır.
# Kullanım: tests/perf/post_show_memory.sh [döngü=8] [sınır_kB=50000]
# UYARI: ravenspot.service'i yeniden başlatır ve menüyü ekranda açıp kapatır.
set -u
CYCLES=${1:-8}
LIMIT_KB=${2:-50000}
BIN=${RAVENSPOT_BIN:-$HOME/.local/bin/ravenspot}
fail=0
for i in $(seq 1 "$CYCLES"); do
    systemctl --user restart ravenspot.service
    sleep 2
    pid=$(systemctl --user show -p MainPID --value ravenspot.service)
    "$BIN" --show >/dev/null; sleep 1; "$BIN" --hide >/dev/null; sleep 1
    kb=$(awk '/^Private_Dirty:/{s+=$2} END{print s}' "/proc/$pid/smaps")
    if [ "$kb" -ge "$LIMIT_KB" ]; then status=FAIL; fail=$((fail+1)); else status=ok; fi
    echo "cycle $i: private dirty ${kb} kB  $status"
done
echo "failures: $fail/$CYCLES (limit ${LIMIT_KB} kB)"
[ "$fail" -eq 0 ]
