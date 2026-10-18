#!/bin/bash
# Microsoft Store paketi → yapi/TurkceAltyaziBulucu.msix  (önce ./derle.sh)
#
#   ./paketle.sh          Store'a yüklenecek imzasız paket (Store kendi imzalar)
#   ./paketle.sh --test   deneme sertifikasıyla imzalı paket (yalnız sertifikası yüklenen makinede kurulur)
#
# Kimlik paket/kimlik.txt'ten gelir; Partner Center'da ad ayrılınca "Ürün kimliği" sayfasındaki
# Package/Identity/Name, Publisher ve PublisherDisplayName değerleri buraya yazılır.
# Araçlar: araclar/sdk (NuGet Microsoft.Windows.SDK.BuildTools → bin/<sürüm>/x64); deneme makinesinde çalışır.
set -euo pipefail
cd "$(dirname "$0")"
SDK=araclar/sdk
TEST=0; [ "${1:-}" = --test ] && TEST=1

[ -f paket/kimlik.txt ] || cat > paket/kimlik.txt <<'E'
AD=AliPolat.TurkceAltyaziBulucu
YAYINCI=CN=Ali Polat Test
YAYINCI_AD=Ali Polat
SURUM=1.0.0.0
E
while IFS='=' read -r k v; do [ -n "$k" ] && printf -v "$k" '%s' "$v"; done < paket/kimlik.txt

D=yapi/msix
rm -rf "$D" && mkdir -p "$D"
cp yapi/TurkceAltyaziBulucu.exe yapi/TabMenu.dll yapi/LISANSLAR.txt "$D"/
cp -r paket/Assets "$D"/
unzip -q ucuncu/mpc-hc.zip -d "$D"/mpc-hc
# MPC-HC taşınabilir kip ayarı (kurulum.cpp ile aynı): UTF-16, güncelleme sorusu çıkmasın
printf '\xff\xfe' > "$D"/mpc-hc/mpc-hc64.ini
printf '[Settings]\r\nUpdaterAutoCheck=0\r\n' | iconv -f utf-8 -t utf-16le >> "$D"/mpc-hc/mpc-hc64.ini

MENU=""
for uz in .mkv .mp4 .avi .m4v .mov .wmv .ts .m2ts .webm .mpg .mpeg .flv; do   # ana.cpp VIDEO_UZANTI
    MENU+="            <desktop5:ItemType Type=\"$uz\"><desktop5:Verb Id=\"TurkceAltyazi\" Clsid=\"696CDC41-8DB2-499E-9289-3B9F78BE9A39\" /></desktop5:ItemType>"$'\n'
done
python3 - "$D/AppxManifest.xml" "$AD" "$YAYINCI" "$YAYINCI_AD" "$SURUM" "$MENU" <<'P'
import sys, html
hedef, ad, yay, yay_ad, surum, menu = sys.argv[1:]
s = open('paket/AppxManifest.xml', encoding='utf-8').read()
for k, v in (('@AD@', ad), ('@YAYINCI@', yay), ('@YAYINCI_AD@', yay_ad), ('@SURUM@', surum)):
    s = s.replace(k, html.escape(v))
s = s.replace('@MENU@', menu.rstrip('\n'))
open(hedef, 'w', encoding='utf-8').write(s)
P

# MakeAppx/SignTool Wine'da sessizce başarısız oluyor: Windows deneme makinesinde çalıştır (../windows-test, açık olmalı)
VMD=../windows-test; PAY=$VMD/paylasim/msix-yapi
rm -rf "$PAY" yapi/TurkceAltyaziBulucu.msix && mkdir -p "$PAY"
cp -r "$D" "$PAY"/icerik && cp -r $SDK "$PAY"/sdk
IMZA=""
if [ $TEST = 1 ]; then
    # deneme sertifikası: konu = YAYINCI, kod imzalama; anahtar/ depoya girmez
    if [ ! -f anahtar/test.pfx ]; then
        openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -keyout anahtar/test.key -out anahtar/test.crt \
            -subj "/CN=${YAYINCI#CN=}" -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" \
            -addext "extendedKeyUsage=codeSigning" 2>/dev/null
        openssl pkcs12 -export -in anahtar/test.crt -inkey anahtar/test.key -out anahtar/test.pfx -passout pass:test
    fi
    cp anahtar/test.pfx "$PAY"/
    cp anahtar/test.crt yapi/TurkceAltyaziBulucu-test.cer
    IMZA=' && Z:\msix-yapi\sdk\signtool.exe sign /fd SHA256 /f Z:\msix-yapi\test.pfx /p test Z:\msix-yapi\paket.msix'
fi
"$VMD"/vm.sh komut "Z:\msix-yapi\sdk\makeappx.exe pack /o /d Z:\msix-yapi\icerik /p Z:\msix-yapi\paket.msix$IMZA" | tail -4
cp "$PAY"/paket.msix yapi/TurkceAltyaziBulucu.msix
rm -rf "$PAY"
ls -la yapi/TurkceAltyaziBulucu.msix
