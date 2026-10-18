// HTTP, parmak izi, gzip/zip, kodlama ve SRT yardımcıları
#include "ortak.h"
#include "gomulu.h"   // derle.sh üretir: GOMULU_OS_ANAHTAR (kaynakta sır yok)

#include <winhttp.h>
#include <shlobj.h>
#include <propkey.h>
#include <propsys.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "../ucuncu/miniz/miniz.h"

// ---------------------------------------------------------------- metin
string utf8(const wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

wstring genis(const string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

string kucuk(string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

string url_kodla(const string &s) {
    string o;
    char b[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}

// ---------------------------------------------------------------- ayarlar
static const wchar_t *AYAR_ANAHTAR = L"Software\\TurkceAltyaziBulucu";

static string reg_oku(HKEY k, const wchar_t *ad) {
    wchar_t b[1024];
    DWORD n = sizeof b;
    if (RegGetValueW(k, nullptr, ad, RRF_RT_REG_SZ, nullptr, b, &n) != ERROR_SUCCESS) return {};
    return utf8(b);
}

static void reg_yaz(HKEY k, const wchar_t *ad, const string &v) {
    wstring w = genis(v);
    RegSetValueExW(k, ad, 0, REG_SZ, (const BYTE *)w.c_str(), DWORD((w.size() + 1) * sizeof(wchar_t)));
}

Ayarlar ayar_oku() {
    Ayarlar a;
    a.os_anahtar = GOMULU_OS_ANAHTAR;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AYAR_ANAHTAR, 0, KEY_READ, &k) != ERROR_SUCCESS) return a;
    if (string elle = reg_oku(k, L"OpenSubtitlesAnahtar"); !elle.empty()) a.os_anahtar = elle;   // ileri kullanıcı için
    a.os_kullanici = reg_oku(k, L"OpenSubtitlesKullanici");
    a.os_sifre = reg_oku(k, L"OpenSubtitlesSifre");
    a.subdl_anahtar = reg_oku(k, L"SubdlAnahtar");
    a.ingilizce = reg_oku(k, L"Ingilizce") != "0";
    RegCloseKey(k);
    return a;
}

void ayar_yaz(const Ayarlar &a) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, AYAR_ANAHTAR, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    reg_yaz(k, L"OpenSubtitlesKullanici", a.os_kullanici);
    reg_yaz(k, L"OpenSubtitlesSifre", a.os_sifre);
    reg_yaz(k, L"SubdlAnahtar", a.subdl_anahtar);
    reg_yaz(k, L"Ingilizce", a.ingilizce ? "1" : "0");
    RegCloseKey(k);
}

// ---------------------------------------------------------------- HTTP (WinHTTP, sistem vekili ve yönlendirmeler otomatik)
Yanit http(const string &yontem, const string &url, const string &basliklar, const string &govde, int zaman_asimi_ms) {
    Yanit y;
    wstring wurl = genis(url);
    URL_COMPONENTS uc{sizeof uc};
    wchar_t host[256], yol[4096];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = yol;
    uc.dwUrlPathLength = 4096;
    uc.dwSchemeLength = 1;
    uc.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return y;
    wstring tam_yol = wstring(yol, uc.dwUrlPathLength) + (uc.lpszExtraInfo ? wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");
    HINTERNET s = WinHttpOpen(L"TurkceAltyaziBulucu/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!s) s = WinHttpOpen(L"TurkceAltyaziBulucu/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!s) return y;
    WinHttpSetTimeouts(s, zaman_asimi_ms, zaman_asimi_ms, zaman_asimi_ms, zaman_asimi_ms);
    HINTERNET c = WinHttpConnect(s, wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, genis(yontem).c_str(), tam_yol.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    if (r) {
        DWORD sikistirma = WINHTTP_DECOMPRESSION_FLAG_ALL;   // gzip/deflate aktarım sıkıştırmasını WinHTTP açsın
        WinHttpSetOption(r, WINHTTP_OPTION_DECOMPRESSION, &sikistirma, sizeof sikistirma);
        wstring wb = genis(basliklar);
        if (WinHttpSendRequest(r, wb.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wb.c_str(), (DWORD)-1L,
                               govde.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)govde.data(), (DWORD)govde.size(),
                               (DWORD)govde.size(), 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD kod = 0, n = sizeof kod;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &kod, &n, nullptr);
            y.durum = (int)kod;
            char b[16384];
            DWORD okunan;
            while (WinHttpReadData(r, b, sizeof b, &okunan) && okunan) y.govde.append(b, okunan);
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return y;
}

// ---------------------------------------------------------------- parmak izi (OpenSubtitles/BSPlayer): boyut + ilk ve son 64 KB'ın 64 bit toplamı
bool parmak_izi(const wstring &yol, string &hash, uint64_t &boyut) {
    HANDLE h = CreateFileW(yol.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    boyut = (uint64_t)sz.QuadPart;
    uint64_t t = boyut;
    std::vector<uint64_t> b(8192);
    for (int i = 0; i < 2; i++) {
        LARGE_INTEGER konum;
        konum.QuadPart = i == 0 ? 0 : std::max<LONGLONG>(0, sz.QuadPart - 65536);
        SetFilePointerEx(h, konum, nullptr, FILE_BEGIN);
        DWORD n = 0;
        std::fill(b.begin(), b.end(), 0);
        ReadFile(h, b.data(), 65536, &n, nullptr);
        for (DWORD k = 0; k < n / 8; k++) t += b[k];
    }
    CloseHandle(h);
    char s[17];
    snprintf(s, sizeof s, "%016llx", (unsigned long long)t);
    hash = s;
    return boyut >= 131072;
}

// ---------------------------------------------------------------- dosya adından film bilgisi + Windows'tan süre
void video_cozumle(Video &v) {
    wstring dosya = v.yol.substr(v.yol.find_last_of(L"\\/") + 1);
    wstring klasor = v.yol.substr(0, v.yol.find_last_of(L"\\/"));
    klasor = klasor.substr(klasor.find_last_of(L"\\/") + 1);
    string ad = utf8(dosya.substr(0, dosya.find_last_of(L'.')));
    v.surum = kucuk(ad);
    std::smatch m;
    if (std::regex_search(ad, m, std::regex(R"([Ss](\d{1,2})[ ._-]?[Ee](\d{1,3}))"))) {
        v.sezon = std::stoi(m[1]);
        v.bolum = std::stoi(m[2]);
        ad = ad.substr(0, m.position(0));
    } else if (std::regex_search(ad, m, std::regex(R"((?:^|[ ._(\[])((?:19|20)\d\d)(?:$|[ ._)\]]))"))) {
        v.yil = std::stoi(m[1]);
        ad = ad.substr(0, m.position(0));
    }
    // YTS düzeni: "Film Adı (2019) [1080p]" klasör adı daha temiz
    std::smatch k;
    string ka = utf8(klasor);
    if (std::regex_search(ka, k, std::regex(R"(^(.+?) \(((?:19|20)\d\d)\))")) && !v.sezon) {
        ad = k[1];
        v.yil = std::stoi(k[2]);
    }
    ad = std::regex_replace(ad, std::regex(R"([._]+)"), " ");
    ad = std::regex_replace(ad, std::regex(R"(\s*[\[(].*$)"), "");
    ad = std::regex_replace(ad, std::regex(R"(\s+$|^\s+)"), "");
    v.baslik = ad;

    // süre: Windows özellik sistemi (K-Lite/LAV gibi bileşenler MKV için de doldurur)
    IPropertyStore *ps = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreFromParsingName(v.yol.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&ps)))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(PKEY_Media_Duration, &pv)) && pv.vt == VT_UI8) v.sure_sn = pv.uhVal.QuadPart / 1e7;
        PropVariantClear(&pv);
        ps->Release();
    }
    v.izler = mkv_izler(v.yol);
    for (auto &iz : v.izler)
        if (std::find(v.gomulu.begin(), v.gomulu.end(), iz.dil) == v.gomulu.end()) v.gomulu.push_back(iz.dil);
}

// ---------------------------------------------------------------- MKV gömülü altyazılar
// Matroska (EBML) başlığından altyazı izlerinin dilini okur. Tracks öğesi neredeyse her zaman dosyanın ilk birkaç MB'ındadır.
namespace {
struct Ebml {
    const unsigned char *p, *son;
    bool kimlik(uint32_t &id) {
        if (p >= son || !*p) return false;
        int n = 1;
        while (n <= 4 && !(*p & (0x80 >> (n - 1)))) n++;
        if (n > 4 || p + n > son) return false;
        id = 0;
        for (int i = 0; i < n; i++) id = id << 8 | p[i];
        p += n;
        return true;
    }
    bool boyut(uint64_t &b) {   // bilinmeyen boyut (hepsi 1) → kalan her şey
        if (p >= son || !*p) return false;
        int n = 1;
        while (n <= 8 && !(*p & (0x80 >> (n - 1)))) n++;
        if (n > 8 || p + n > son) return false;
        b = *p & (0xFF >> n);
        bool hepsi_bir = b == (uint64_t)(0xFF >> n);
        for (int i = 1; i < n; i++) { b = b << 8 | p[i]; hepsi_bir &= p[i] == 0xFF; }
        p += n;
        if (hepsi_bir || b > (uint64_t)(son - p)) b = son - p;
        return true;
    }
};
string dil_kisa(string d) {
    d = kucuk(d.substr(0, d.find_first_of("-_")));
    if (d == "tur" || d == "tr") return "tr";
    if (d == "eng" || d == "en") return "en";
    return d;
}
}

std::vector<GomuluIz> mkv_izler(const wstring &yol) {
    std::vector<GomuluIz> izler;
    HANDLE f = CreateFileW(yol.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return izler;
    std::vector<unsigned char> b(4 << 20);
    DWORD okunan = 0;
    ReadFile(f, b.data(), (DWORD)b.size(), &okunan, nullptr);
    CloseHandle(f);
    Ebml e{b.data(), b.data() + okunan};
    uint32_t id;
    uint64_t n;
    if (!e.kimlik(id) || id != 0x1A45DFA3 || !e.boyut(n)) return izler;   // EBML başlığı değil → MKV değil
    e.p += n;
    if (!e.kimlik(id) || id != 0x18538067 || !e.boyut(n)) return izler;   // Segment
    while (e.kimlik(id) && e.boyut(n)) {
        if (id == 0x1F43B675) break;   // Cluster: buradan sonrası görüntü verisi
        if (id != 0x1654AE6B) { e.p += n; continue; }
        Ebml iz{e.p, e.p + n};          // Tracks
        while (iz.kimlik(id) && iz.boyut(n)) {
            if (id != 0xAE) { iz.p += n; continue; }
            Ebml t{iz.p, iz.p + n};     // TrackEntry
            iz.p += n;
            uint64_t tur = 0;
            GomuluIz g;
            string dil = "eng", bcp;   // Matroska'da Language yoksa varsayılan "eng"
            g.varsayilan = true;       // FlagDefault yoksa varsayılan 1
            while (t.kimlik(id) && t.boyut(n)) {
                string deger((const char *)t.p, (size_t)n);
                uint64_t sayi = 0;
                for (auto c : deger) sayi = sayi << 8 | (unsigned char)c;
                if (id == 0x83) tur = sayi;
                else if (id == 0xD7) g.no = (int)sayi;
                else if (id == 0x22B59C) dil = deger.c_str();
                else if (id == 0x22B59D) bcp = deger.c_str();
                else if (id == 0x536E) g.ad = deger.c_str();
                else if (id == 0x86) g.codec = deger.c_str();
                else if (id == 0x55AA) g.forced = sayi;
                else if (id == 0x88) g.varsayilan = sayi;
                t.p += n;
            }
            if (tur != 0x11) continue;   // yalnız altyazı izleri
            g.dil = dil_kisa(bcp.empty() ? dil : bcp);
            string ad = kucuk(g.ad);
            if (ad.find("türk") != string::npos || ad.find("turk") != string::npos) g.dil = "tr";
            if (ad.find("forced") != string::npos || ad.find("zorunlu") != string::npos) g.forced = true;
            izler.push_back(g);
        }
        break;
    }
    return izler;
}

// Tüm dosyayı baştan sona tarar ama yalnız öğe başlıklarını okur; görüntü/ses blokları atlanır.
namespace {
struct Okuyucu {
    HANDLE f;
    std::vector<unsigned char> tampon = std::vector<unsigned char>(1 << 20);
    size_t bas = 0, son = 0;
    uint64_t dosya_konum = 0;   // tampon[son]'un dosyadaki yeri
    bool doldur(size_t gerek) {
        if (son - bas >= gerek) return true;
        memmove(tampon.data(), tampon.data() + bas, son - bas);
        son -= bas; bas = 0;
        DWORD n = 0;
        if (!ReadFile(f, tampon.data() + son, (DWORD)(tampon.size() - son), &n, nullptr)) return false;
        son += n; dosya_konum += n;
        return son - bas >= gerek;
    }
    bool vint(uint64_t &v, bool kimlik, bool *bilinmeyen = nullptr) {
        if (!doldur(1)) return false;
        unsigned char c = tampon[bas];
        if (!c) return false;
        int n = 1;
        while (!(c & (0x80 >> (n - 1)))) n++;
        if (n > (kimlik ? 4 : 8) || !doldur(n)) return false;
        v = kimlik ? c : c & (0xFF >> n);
        bool hepsi = !kimlik && v == (uint64_t)(0xFF >> n);
        for (int i = 1; i < n; i++) { v = v << 8 | tampon[bas + i]; hepsi &= tampon[bas + i] == 0xFF; }
        if (bilinmeyen) *bilinmeyen = hepsi;
        bas += n;
        return true;
    }
    bool atla(uint64_t n) {
        if (n <= son - bas) { bas += n; return true; }
        n -= son - bas;
        bas = son = 0;
        LARGE_INTEGER d; d.QuadPart = (LONGLONG)n;
        if (!SetFilePointerEx(f, d, nullptr, FILE_CURRENT)) return false;
        dosya_konum += n;
        return true;
    }
    bool oku(string &s, uint64_t n) {
        if (n > (16u << 20)) return false;
        s.clear();
        while (n) {
            if (!doldur(1)) return false;
            size_t k = std::min<uint64_t>(n, son - bas);
            s.append((const char *)tampon.data() + bas, k);
            bas += k; n -= k;
        }
        return true;
    }
};

string srt_zaman(long long ms) {
    if (ms < 0) ms = 0;
    char b[32];
    snprintf(b, sizeof b, "%02lld:%02lld:%02lld,%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
    return b;
}

// ASS olay satırı: ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text → yalnız metin
string ass_metin(const string &s) {
    size_t p = 0;
    for (int i = 0; i < 8 && p != string::npos; i++) p = s.find(',', p ? p + 1 : 0);
    string t = p == string::npos ? s : s.substr(p + 1), out;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '{') { size_t k = t.find('}', i); if (k == string::npos) break; i = k; continue; }
        if (t[i] == '\\' && i + 1 < t.size() && (t[i + 1] == 'N' || t[i + 1] == 'n')) { out += '\n'; i++; continue; }
        if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == 'h') { out += ' '; i++; continue; }
        out += t[i];
    }
    return out;
}
}

string mkv_srt_cikar(const wstring &yol, const GomuluIz &iz) {
    Okuyucu o{CreateFileW(yol.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (o.f == INVALID_HANDLE_VALUE) return "";
    struct Satir { long long bas, sure; string metin; };
    std::vector<Satir> satirlar;
    uint64_t olcek = 1000000;   // TimecodeScale (ns), varsayılan 1 ms
    long long kume = 0;         // Cluster Timecode
    bool ass = iz.codec != "S_TEXT/UTF8";
    // blok verisi: iz no (vint) + int16 göreli zaman + bayrak + yük
    auto blok = [&](const string &v, long long sure) {
        size_t n = 1;
        unsigned char c = v.empty() ? 0 : v[0];
        while (n <= 8 && c && !(c & (0x80 >> (n - 1)))) n++;
        if (!c || n > 8 || v.size() < n + 3) return;
        uint64_t no = c & (0xFF >> n);
        for (size_t i = 1; i < n; i++) no = no << 8 | (unsigned char)v[i];
        if ((int)no != iz.no) return;
        int16_t goreli = (int16_t)((unsigned char)v[n] << 8 | (unsigned char)v[n + 1]);
        string metin = v.substr(n + 3);
        while (!metin.empty() && (metin.back() == '\0' || metin.back() == '\r' || metin.back() == '\n')) metin.pop_back();
        if (ass) metin = ass_metin(metin);
        if (metin.empty()) return;
        satirlar.push_back({(long long)((kume + goreli) * olcek / 1000000), sure < 0 ? -1 : (long long)(sure * olcek / 1000000), metin});
    };
    uint64_t id, n;
    bool bilinmeyen;
    if (!o.vint(id, true) || id != 0x1A45DFA3 || !o.vint(n, false) || !o.atla(n)) { CloseHandle(o.f); return ""; }
    if (!o.vint(id, true) || id != 0x18538067 || !o.vint(n, false, &bilinmeyen)) { CloseHandle(o.f); return ""; }
    while (o.vint(id, true) && o.vint(n, false, &bilinmeyen)) {
        if (id == 0x1F43B675) continue;   // Cluster: içine gir (boyutu bilinmese de olur)
        if (id == 0x1549A966) {           // Info: TimecodeScale
            string v;
            if (!o.oku(v, n)) break;
            Ebml e{(const unsigned char *)v.data(), (const unsigned char *)v.data() + v.size()};
            uint32_t i2; uint64_t n2;
            while (e.kimlik(i2) && e.boyut(n2)) {
                if (i2 == 0x2AD7B1) { olcek = 0; for (uint64_t k = 0; k < n2; k++) olcek = olcek << 8 | e.p[k]; }
                e.p += n2;
            }
            if (!olcek) olcek = 1000000;
            continue;
        }
        if (id == 0xE7) {                 // Cluster Timecode
            string v;
            if (!o.oku(v, n)) break;
            kume = 0;
            for (unsigned char c : v) kume = kume << 8 | c;
            continue;
        }
        if (id == 0xA3) {                 // SimpleBlock: önce iz numarasına bak, başkasınınsa atla
            if (!o.doldur(1)) break;
            unsigned char c = o.tampon[o.bas];
            int k = 1;
            while (k <= 8 && c && !(c & (0x80 >> (k - 1)))) k++;
            if (!c || k > 8 || !o.doldur(k)) break;
            uint64_t no = c & (0xFF >> k);
            for (int i = 1; i < k; i++) no = no << 8 | o.tampon[o.bas + i];
            if ((int)no != iz.no) { if (!o.atla(n)) break; continue; }
            string v;
            if (!o.oku(v, n)) break;
            blok(v, -1);
            continue;
        }
        if (id == 0xA0) {                 // BlockGroup: Block + BlockDuration (altyazılar genelde böyle)
            string v;
            if (!o.oku(v, n)) break;
            Ebml e{(const unsigned char *)v.data(), (const unsigned char *)v.data() + v.size()};
            uint32_t i2; uint64_t n2;
            string b;
            long long sure = -1;
            while (e.kimlik(i2) && e.boyut(n2)) {
                if (i2 == 0xA1) b.assign((const char *)e.p, (size_t)n2);
                else if (i2 == 0x9B) { sure = 0; for (uint64_t k = 0; k < n2; k++) sure = sure << 8 | e.p[k]; }
                e.p += n2;
            }
            if (!b.empty()) blok(b, sure);
            continue;
        }
        if (bilinmeyen || !o.atla(n)) break;
    }
    CloseHandle(o.f);
    std::stable_sort(satirlar.begin(), satirlar.end(), [](const Satir &a, const Satir &b) { return a.bas < b.bas; });
    string srt;
    for (size_t i = 0; i < satirlar.size(); i++) {
        long long son = satirlar[i].sure >= 0 ? satirlar[i].bas + satirlar[i].sure
                        : i + 1 < satirlar.size() ? std::min(satirlar[i + 1].bas, satirlar[i].bas + 5000) : satirlar[i].bas + 3000;
        srt += std::to_string(i + 1) + "\r\n" + srt_zaman(satirlar[i].bas) + " --> " + srt_zaman(son) + "\r\n" + satirlar[i].metin + "\r\n\r\n";
    }
    return srt;
}

// ---------------------------------------------------------------- sıkıştırma
string gzip_ac(const string &veri) {
    if (veri.size() < 18 || (unsigned char)veri[0] != 0x1f || (unsigned char)veri[1] != 0x8b) return veri;   // gzip değil: olduğu gibi
    size_t p = 10;
    unsigned char bayrak = (unsigned char)veri[3];
    if (bayrak & 4) p += 2 + ((unsigned char)veri[p] | ((unsigned char)veri[p + 1] << 8));
    if (bayrak & 8) while (p < veri.size() && veri[p++]) {}
    if (bayrak & 16) while (p < veri.size() && veri[p++]) {}
    if (bayrak & 2) p += 2;
    if (p >= veri.size()) return {};
    size_t n = 0;
    void *o = tinfl_decompress_mem_to_heap(veri.data() + p, veri.size() - p, &n, 0);
    if (!o) return {};
    string s((char *)o, n);
    mz_free(o);
    return s;
}

string zipten_altyazi(const string &veri, string &dosya_adi) {
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, veri.data(), veri.size(), 0)) return {};
    int sec = -1, oncelik = 9;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); i++) {
        char ad[512];
        mz_zip_reader_get_filename(&z, i, ad, sizeof ad);
        string a = kucuk(ad);
        int o = a.size() > 4 && a.compare(a.size() - 4, 4, ".srt") == 0 ? 0
              : (a.find(".ass") != string::npos || a.find(".ssa") != string::npos) ? 1
              : a.find(".sub") != string::npos ? 2 : 9;
        if (o < oncelik) oncelik = o, sec = (int)i, dosya_adi = ad;
    }
    string s;
    if (sec >= 0) {
        size_t n = 0;
        void *p = mz_zip_reader_extract_to_heap(&z, (mz_uint)sec, &n, 0);
        if (p) s.assign((char *)p, n), mz_free(p);
    }
    mz_zip_reader_end(&z);
    return s;
}

// ---------------------------------------------------------------- kodlama: Türkçe altyazılar çoğunlukla Windows-1254
static bool gecerli_utf8(const string &s) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0) > 0;
}

string utf8e_cevir(const string &ham, const string &dil) {
    string s = ham;
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s = s.substr(3);
    else if (s.size() >= 2 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) {   // UTF-16 LE
        return utf8(wstring((const wchar_t *)(s.data() + 2), (s.size() - 2) / 2));
    }
    if (!gecerli_utf8(s)) {
        // Windows-1254'e özgü Türkçe harfler: Ş ş Ğ ğ İ ı (1252'de Þ þ Ð ð Ý ý olarak bozulurlar)
        size_t tr = 0;
        for (unsigned char c : s) tr += c == 0xDE || c == 0xFE || c == 0xD0 || c == 0xF0 || c == 0xDD || c == 0xFD;
        UINT cp = (dil == "tr" || tr * 400 > s.size()) ? 1254 : 1252;
        int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
        wstring w(n, L'\0');
        MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), w.data(), n);
        s = utf8(w);
    }
    // satır sonlarını Windows biçimine getir
    string o;
    o.reserve(s.size() + s.size() / 20);
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\r') continue;
        if (s[i] == '\n') o += "\r\n";
        else o += s[i];
    }
    return o;
}

// ---------------------------------------------------------------- SRT zaman işlemleri
static const std::regex ZAMAN(R"((\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3}))");

static long long ms_oku(const std::smatch &m) {
    string ms = m[4].str();
    while (ms.size() < 3) ms += '0';
    return ((std::stoll(m[1]) * 60 + std::stoll(m[2])) * 60 + std::stoll(m[3])) * 1000 + std::stoll(ms);
}

static string ms_yaz(long long t) {
    if (t < 0) t = 0;
    char b[16];
    snprintf(b, sizeof b, "%02lld:%02lld:%02lld,%03lld", t / 3600000, t / 60000 % 60, t / 1000 % 60, t % 1000);
    return b;
}

double son_satir_sn(const string &srt) {
    long long en = 0;
    for (auto it = std::sregex_iterator(srt.begin(), srt.end(), ZAMAN); it != std::sregex_iterator(); ++it)
        en = std::max(en, ms_oku(*it));
    return en / 1000.0;
}

string kaydir(const string &srt, int ms) {
    if (!ms) return srt;
    string o;
    auto son = srt.cbegin();
    for (auto it = std::sregex_iterator(srt.begin(), srt.end(), ZAMAN); it != std::sregex_iterator(); ++it) {
        o.append(son, srt.cbegin() + it->position(0));
        o += ms_yaz(ms_oku(*it) + ms);
        son = srt.cbegin() + it->position(0) + it->length(0);
    }
    o.append(son, srt.cend());
    return o;
}

long long deneme_ani_ms(const string &srt) {
    // filmin ~%30'undan sonraki, en az 3 kelimelik ilk konuşma satırı → 2 sn öncesi
    double toplam = son_satir_sn(srt) * 1000;
    std::regex blok(R"((\d{1,2}:\d{2}:\d{2}[,.]\d{1,3}) *--> *[^\r\n]*\r?\n([^\r\n]+))");
    for (auto it = std::sregex_iterator(srt.begin(), srt.end(), blok); it != std::sregex_iterator(); ++it) {
        string zaman = (*it)[1].str(), metin = (*it)[2].str();
        std::smatch m;
        std::regex_search(zaman, m, ZAMAN);
        long long t = ms_oku(m);
        if (t >= toplam * 0.3 && std::count(metin.begin(), metin.end(), ' ') >= 2) return std::max(0LL, t - 2000);
    }
    return (long long)(toplam * 0.3);
}
