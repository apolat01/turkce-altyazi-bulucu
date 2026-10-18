#!/bin/bash
# Windows için çapraz derleme → yapi/TurkceAltyaziBulucu.exe
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p yapi
# OpenSubtitles uygulama anahtarı: anahtar/opensubtitles.txt (depoya girmez) → src/gomulu.h
K=$( [ -f anahtar/opensubtitles.txt ] && tr -d ' \r\n' < anahtar/opensubtitles.txt || true )
printf '// derle.sh üretir — depoya koyma\n#define GOMULU_OS_ANAHTAR "%s"\n' "$K" > src/gomulu.h
CC=x86_64-w64-mingw32-gcc; CXX=x86_64-w64-mingw32-g++; WR=x86_64-w64-mingw32-windres
$CC -O2 -c ucuncu/miniz/miniz.c -o yapi/miniz.o -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
( cd src && $WR -c 65001 ana.rc -O coff -o ../yapi/ana_res.o )
$CXX -std=c++17 -O2 -s -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type -municode -mwindows \
    -static -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0A00 \
    src/ana.cpp src/kaynaklar.cpp src/yardimci.cpp yapi/miniz.o yapi/ana_res.o -o yapi/TurkceAltyaziBulucu.exe \
    -lwinhttp -lcomctl32 -lshell32 -lole32 -luuid -lpropsys -ladvapi32 -lcomdlg32 -lgdi32
# Windows 11 sağ tık menüsü (yalnız Store paketinde kullanılır)
$CXX -std=c++17 -O2 -s -Wall -Wextra -Wno-missing-field-initializers -shared -municode -static -static-libgcc -static-libstdc++ \
    -D_WIN32_WINNT=0x0A00 src/menu.cpp -o yapi/TabMenu.dll -lshell32 -lole32 -luuid -lshlwapi
{ echo "Türkçe Altyazı Bulucu — üçüncü taraf bileşenler"; echo
  echo "MPC-HC 2.8.3 (clsid2) — GPL-2.0 — kaynak: https://github.com/clsid2/mpc-hc (lisans metni mpc-hc\\COPYING.txt)"
  echo "LAV Filters (MPC-HC içinde) — GPL-2.0 — https://github.com/Nevcairiel/LAVFilters"
  echo "miniz — MIT — https://github.com/richgel999/miniz"; cat ucuncu/miniz/LICENSE; echo
  echo "nlohmann/json — MIT — https://github.com/nlohmann/json"; } > yapi/LISANSLAR.txt
( cd src && $WR -c 65001 kurulum.rc -O coff -o ../yapi/kurulum_res.o )
$CXX -std=c++17 -O2 -s -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -static -static-libgcc -static-libstdc++ \
    -D_WIN32_WINNT=0x0A00 src/kurulum.cpp yapi/miniz.o yapi/kurulum_res.o -o yapi/TurkceAltyaziBulucu-Kurulum.exe \
    -lshell32 -lole32 -luuid -ladvapi32
ls -la yapi/*.exe
