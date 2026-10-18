// Türkçe Altyazı Bulucu — ana pencere
// Videoyu sürükle → BSPlayer + OpenSubtitles + Subdl aynı anda aranır → Türkçe önce, uyum puanına göre liste
// → "Dene" MPC-HC/VLC'de konuşma olan bir anda açar → gerekirse ± kaydır → "Kaydet" videonun yanına film.tr.srt
#include "ortak.h"
#include "kaynaklar.h"

#include <appmodel.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#define SURUM L"1.0.0"
static const wchar_t *UYG = L"Türkçe Altyazı Bulucu";
static const wchar_t *BILDIR_URL = L"https://github.com/boslukkk/turkce-altyazi-bulucu/issues/new";

// Microsoft Store (MSIX) paketinden mi çalışıyor: kurulum, kaldırma ve sağ tık menüsünü Windows yönetir
static bool paketli() {
    static int p = -1;
    if (p < 0) {
        UINT32 n = 0;
        p = GetCurrentPackageFullName(&n, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
    }
    return p;
}

enum : UINT { WM_SONUC = WM_APP + 1, WM_INDI, WM_INDIRILEN };
enum : UINT {
    K_AC = 100, K_DENE, K_GERI, K_ILERI, K_KAYDET, K_VIDEO_OYNAT, K_AYARLAR, K_HAKKINDA, K_BILDIR, K_CIKIS, K_YENIDEN, K_SITE,
    K_LISTE = 200,
};

using AdayP = std::shared_ptr<Aday>;

struct Sonuc { int nesil; int kaynak; std::vector<Aday> adaylar; string durum; };
struct Indi { int nesil; AdayP aday; bool ok; string hata; int sonra; };   // sonra: 0 hiçbir şey, 1 dene, 2 kaydet

static HINSTANCE g_inst;
static HWND g_pen, g_bilgi, g_kaynak_durum, g_liste, g_durum, g_kaydirma_etiket;
static HWND g_dugme[7];
static HFONT g_font, g_kalin;
static Video g_video;
static std::vector<AdayP> g_adaylar;
static Ayarlar g_ayar;
static int g_nesil = 0, g_kaydirma = 0, g_bekleyen = 0, g_ulasilan = 0;   // g_ulasilan: yanıt veren kaynak sayısı
static string g_kaynak_metin[3] = {"—", "—", "—"};
static const char *KAYNAK_AD[3] = {"BSPlayer", "OpenSubtitles", "Subdl"};
static string g_gunluk;   // "Sorun bildir" için

static void gunluk(const string &s) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[16];
    snprintf(b, sizeof b, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    g_gunluk += b + s + "\r\n";
    if (g_gunluk.size() > 20000) g_gunluk.erase(0, g_gunluk.size() - 20000);
}

static void durum(const wstring &s) { SendMessageW(g_durum, SB_SETTEXTW, 0, (LPARAM)s.c_str()); }

static int dpi(int x) { return MulDiv(x, GetDpiForWindow(g_pen ? g_pen : GetDesktopWindow()), 96); }

// ---------------------------------------------------------------- liste
static AdayP secili() {
    int i = ListView_GetNextItem(g_liste, -1, LVNI_SELECTED);
    if (i < 0) return nullptr;
    LVITEMW it{};
    it.mask = LVIF_PARAM;
    it.iItem = i;
    ListView_GetItem(g_liste, &it);
    return (size_t)it.lParam < g_adaylar.size() ? g_adaylar[it.lParam] : nullptr;
}

static void liste_doldur() {
    AdayP onceki = secili();
    std::vector<size_t> sira(g_adaylar.size());
    for (size_t i = 0; i < sira.size(); i++) sira[i] = i;
    std::stable_sort(sira.begin(), sira.end(), [](size_t a, size_t b) {
        auto &x = *g_adaylar[a], &y = *g_adaylar[b];
        if (x.dil != y.dil) return x.dil == "tr";                       // Türkçe önce
        if (x.uyum != y.uyum) return x.uyum > y.uyum;
        return x.indirme_sayisi > y.indirme_sayisi;
    });
    SendMessageW(g_liste, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_liste);
    int satir = 0, sec = -1;
    for (size_t i : sira) {
        auto &c = *g_adaylar[i];
        if (c.dil != "tr" && !g_ayar.ingilizce) continue;
        wstring dil = c.dil == "tr" ? L"Türkçe" : L"İngilizce";
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = satir;
        it.pszText = dil.data();
        it.lParam = (LPARAM)i;
        ListView_InsertItem(g_liste, &it);
        wstring uyum = std::to_wstring(c.uyum) + L"%" + (c.hash_eslesti ? L" ✓" : L"");
        wstring kaynak = genis(c.kaynak), ad = genis(c.ad);
        wstring ind = c.indirme_sayisi ? std::to_wstring(c.indirme_sayisi) : L"";
        wstring not_ = genis(c.not_);
        ListView_SetItemText(g_liste, satir, 1, uyum.data());
        ListView_SetItemText(g_liste, satir, 2, kaynak.data());
        ListView_SetItemText(g_liste, satir, 3, ad.data());
        ListView_SetItemText(g_liste, satir, 4, ind.data());
        ListView_SetItemText(g_liste, satir, 5, not_.data());
        if (g_adaylar[i] == onceki) sec = satir;
        satir++;
    }
    if (sec < 0 && satir) sec = 0;
    if (sec >= 0) ListView_SetItemState(g_liste, sec, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(g_liste, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_liste, nullptr, TRUE);
}

// anahtarı olmayan kaynak hiç görünmez (kullanıcı uğraşmasın)
static bool kaynak_etkin(int k) { return k == 0 || (k == 1 ? !g_ayar.os_anahtar.empty() : !g_ayar.subdl_anahtar.empty()); }

static void kaynak_durum_yaz() {
    wstring s;
    for (int i = 0; i < 3; i++)
        if (kaynak_etkin(i) && !g_kaynak_metin[i].empty() && g_kaynak_metin[i] != "—")
            s += (s.empty() ? L"" : L"     ") + genis(KAYNAK_AD[i]) + L": " + genis(g_kaynak_metin[i]);
    (void)s;   // kaynak ayrıntısı yalnız "Sorun bildir" günlüğünde; arayüzde ilerleme çubuğu
    bool suruyor = g_bekleyen > 0;
    ShowWindow(g_kaynak_durum, suruyor ? SW_SHOW : SW_HIDE);
    SendMessageW(g_kaynak_durum, PBM_SETMARQUEE, suruyor, 30);
}

static void kaydirma_yaz() {
    wchar_t b[64];
    swprintf(b, 64, L"Kaydırma: %+.1f sn", g_kaydirma / 1000.0);
    SetWindowTextW(g_kaydirma_etiket, b);
}

// ---------------------------------------------------------------- arama
static const wchar_t *VIDEO_UZANTI[] = {L".mkv", L".mp4", L".avi", L".m4v", L".mov", L".wmv", L".ts", L".m2ts", L".webm", L".mpg", L".mpeg", L".flv"};

static bool altyazi_dosyasi_mu(const wstring &y) {
    auto n = y.find_last_of(L'.');
    if (n == wstring::npos) return false;
    wstring u = y.substr(n);
    for (auto &c : u) c = (wchar_t)towlower(c);
    for (auto *e : {L".srt", L".ass", L".ssa", L".sub", L".txt", L".zip", L".rar", L".7z"}) if (u == e) return true;
    return false;
}

static bool video_mu(const wstring &y) {
    auto n = y.find_last_of(L'.');
    if (n == wstring::npos) return false;
    wstring u = y.substr(n);
    for (auto &c : u) c = (wchar_t)towlower(c);
    for (auto *e : VIDEO_UZANTI) if (u == e) return true;
    return false;
}

static void indir_baslat(AdayP c, int sonra);
static wstring indirilenler();

static void ara() {
    if (g_video.yol.empty()) return;
    g_nesil++;
    g_adaylar.clear();
    g_kaydirma = 0;
    kaydirma_yaz();
    liste_doldur();
    for (auto &iz : g_video.izler) {   // videonun kendi içindeki metin altyazılar
        if (!iz.metin() || (iz.dil != "tr" && (iz.dil != "en" || !g_ayar.ingilizce))) continue;
        auto c = std::make_shared<Aday>();
        c->kaynak = "Video içi";
        c->dil = iz.dil;
        c->ad = iz.ad.empty() ? "İz " + std::to_string(iz.no) : iz.ad;
        c->hash_eslesti = !iz.forced;
        c->video_yol = g_video.yol;
        c->gomulu = iz;
        uyum_hesapla(*c, g_video);
        g_adaylar.push_back(c);
    }
    for (auto &c : g_adaylar)   // en iyi gömülü Türkçe izi şimdiden çıkar: büyük dosyada birkaç saniye sürer, Dene beklemesin
        if (c->kaynak == "Video içi" && c->dil == "tr" && !c->gomulu.forced) { indir_baslat(c, 0); break; }
    liste_doldur();
    g_bekleyen = g_ulasilan = 0;
    for (int k = 0; k < 3; k++) {
        g_kaynak_metin[k] = kaynak_etkin(k) ? "aranıyor…" : "";
        g_bekleyen += kaynak_etkin(k);
    }
    kaynak_durum_yaz();
    durum(L"Kaynaklarda aynı anda aranıyor…");
    gunluk("arama: " + utf8(g_video.yol) + " hash=" + g_video.hash + " ad='" + g_video.baslik + "' yil=" + std::to_string(g_video.yil));
    for (int k = 0; k < 3; k++) {
        if (!kaynak_etkin(k)) continue;
        std::thread([k, v = g_video, a = g_ayar, nesil = g_nesil] {
            auto *s = new Sonuc{nesil, k, {}, {}};
            if (k == 0) s->adaylar = bsplayer_ara(v, a, s->durum);
            else if (k == 1) s->adaylar = opensubtitles_ara(v, a, s->durum);
            else s->adaylar = subdl_ara(v, a, s->durum);
            if (!PostMessageW(g_pen, WM_SONUC, 0, (LPARAM)s)) delete s;
        }).detach();
    }
}

static void video_yukle(const wstring &yol) {
    Video v;
    v.yol = yol;
    if (!parmak_izi(yol, v.hash, v.boyut)) {
        MessageBoxW(g_pen, (L"Video okunamadı:\n" + yol + L"\n\nDosya çok küçük, bozuk ya da hâlâ iniyor olabilir.").c_str(), UYG, MB_ICONWARNING);
        return;
    }
    video_cozumle(v);
    g_video = v;
    wchar_t b[1024];
    wstring dosya = yol.substr(yol.find_last_of(L"\\/") + 1);
    wstring ek;
    if (v.sezon) { wchar_t t[32]; swprintf(t, 32, L"  ·  S%02dE%02d", v.sezon, v.bolum); ek += t; }
    else if (v.yil) ek += L"  ·  " + std::to_wstring(v.yil);
    if (v.sure_sn >= 3600) { wchar_t t[32]; swprintf(t, 32, L"  ·  %d sa %02d dk", (int)v.sure_sn / 3600, (int)v.sure_sn / 60 % 60); ek += t; }
    else if (v.sure_sn > 0) { wchar_t t[32]; swprintf(t, 32, L"  ·  %d dk", (int)v.sure_sn / 60); ek += t; }
    if (!v.gomulu.empty()) {
        wstring d;
        for (auto &x : v.gomulu) d += (d.empty() ? L"" : L", ") + (x == "tr" ? wstring(L"Türkçe") : x == "en" ? wstring(L"İngilizce") : genis(x));
        ek += L"  ·  içinde altyazı: " + d;
    }
    wchar_t boy[32];
    if (v.boyut >= 1073741824ULL) swprintf(boy, 32, L"%.2f GB", v.boyut / 1073741824.0);
    else swprintf(boy, 32, L"%llu MB", (unsigned long long)(v.boyut >> 20));
    swprintf(b, 1024, L"%ls\nAranan ad: %ls%ls  ·  %ls  ·  parmak izi %ls", dosya.c_str(),
             v.baslik.empty() ? L"(çıkarılamadı)" : genis(v.baslik).c_str(), ek.c_str(), boy, genis(v.hash).c_str());
    SetWindowTextW(g_bilgi, b);
    SetFocus(g_liste);   // ok tuşları + Enter (Dene) hemen çalışsın
    ara();
}

// ---------------------------------------------------------------- indirme / dene / kaydet
static void indir_baslat(AdayP c, int sonra) {
    if (sonra) durum(((c->kaynak == "Video içi" ? L"Videodan çıkarılıyor: " : L"İndiriliyor: ") + genis(c->ad)).c_str());
    if (c->indiriliyor) { c->bekleyen_eylem = sonra; return; }   // zaten iniyor: bitince bu eylem yapılır
    c->indiriliyor = true;
    std::thread([c, sonra, a = g_ayar, nesil = g_nesil] {
        auto *s = new Indi{nesil, c, false, {}, sonra};
        s->ok = indir(*c, a, s->hata);
        if (!PostMessageW(g_pen, WM_INDI, 0, (LPARAM)s)) delete s;
    }).detach();
}

static wstring gecici_dizin() {
    wchar_t b[MAX_PATH];
    GetTempPathW(MAX_PATH, b);
    wstring d = wstring(b) + L"TurkceAltyaziBulucu";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static bool dosya_yaz(const wstring &yol, const string &metin, bool bom) {
    HANDLE h = CreateFileW(yol.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n;
    if (bom) WriteFile(h, "\xEF\xBB\xBF", 3, &n, nullptr);
    BOOL ok = WriteFile(h, metin.data(), (DWORD)metin.size(), &n, nullptr);
    CloseHandle(h);
    return ok;
}

static bool var_mi(const wstring &y) { return GetFileAttributesW(y.c_str()) != INVALID_FILE_ATTRIBUTES; }

static wstring reg_metin(HKEY kok, const wchar_t *anahtar, const wchar_t *ad) {
    wchar_t b[MAX_PATH];
    DWORD n = sizeof b;
    if (RegGetValueW(kok, anahtar, ad, RRF_RT_REG_SZ, nullptr, b, &n) == ERROR_SUCCESS) return b;
    return L"";
}

// Oynatıcı: ayardaki seçim ya da otomatik (MPC-HC → VLC → varsayılan)
static wstring oynatici_bul(bool &mpc) {
    HKEY k;
    wchar_t b[16] = L"otomatik";
    DWORD n = sizeof b;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegGetValueW(k, nullptr, L"Oynatici", RRF_RT_REG_SZ, nullptr, b, &n);
        RegCloseKey(k);
    }
    wstring tercih = b;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wstring dahili = wstring(exe).substr(0, wstring(exe).find_last_of(L'\\')) + L"\\mpc-hc\\mpc-hc64.exe";
    if ((tercih == L"otomatik" || tercih == L"dahili") && var_mi(dahili)) { mpc = true; return dahili; }
    std::vector<wstring> mpc_yollari{reg_metin(HKEY_CURRENT_USER, L"Software\\MPC-HC\\MPC-HC", L"ExePath")};
    for (const wchar_t *pf : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432"}) {
        wchar_t d[MAX_PATH];
        if (!GetEnvironmentVariableW(pf, d, MAX_PATH)) continue;
        for (const wchar_t *alt : {L"\\K-Lite Codec Pack\\MPC-HC64\\mpc-hc64.exe", L"\\K-Lite Codec Pack\\MPC-HC\\mpc-hc.exe",
                                   L"\\MPC-HC\\mpc-hc64.exe", L"\\MPC-HC\\mpc-hc.exe"})
            mpc_yollari.push_back(wstring(d) + alt);
    }
    wstring vlc = reg_metin(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VideoLAN\\VLC", nullptr);
    if (vlc.empty()) {
        wstring d = reg_metin(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VideoLAN\\VLC", L"InstallDir");
        if (!d.empty()) vlc = d + L"\\vlc.exe";
    }
    if (tercih != L"vlc" && tercih != L"varsayilan")
        for (auto &y : mpc_yollari) if (!y.empty() && var_mi(y)) { mpc = true; return y; }
    if (tercih != L"varsayilan" && !vlc.empty() && var_mi(vlc)) { mpc = false; return vlc; }
    mpc = false;
    return L"";
}

static void dene(AdayP c) {
    wstring srt = gecici_dizin() + L"\\deneme." + genis(c->dil) + L".srt";
    if (!dosya_yaz(srt, kaydir(c->icerik, g_kaydirma), true)) { durum(L"Geçici dosya yazılamadı"); return; }
    long long an = deneme_ani_ms(c->icerik) + g_kaydirma;
    bool mpc;
    wstring oyn = oynatici_bul(mpc);
    if (oyn.empty()) {
        ShellExecuteW(g_pen, L"open", g_video.yol.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        MessageBoxW(g_pen, (L"MPC-HC ya da VLC bulunamadı; video varsayılan oynatıcıda açıldı.\nAltyazıyı elle yükleyin:\n" + srt).c_str(), UYG, MB_ICONINFORMATION);
        return;
    }
    wchar_t arg[2048];
    if (mpc) swprintf(arg, 2048, L"\"%ls\" /sub \"%ls\" /start %lld /play", g_video.yol.c_str(), srt.c_str(), std::max(0LL, an));
    else swprintf(arg, 2048, L"\"%ls\" --sub-file=\"%ls\" --start-time=%lld", g_video.yol.c_str(), srt.c_str(), std::max(0LL, an) / 1000);
    ShellExecuteW(g_pen, L"open", oyn.c_str(), arg, nullptr, SW_SHOWNORMAL);
    wchar_t b[256];
    swprintf(b, 256, L"Deneniyor (%ls) — %02lld:%02lld:%02lld anından. Uymazsa listeden başkasını seçip tekrar Dene'ye basın.",
             mpc ? L"MPC-HC" : L"VLC", an / 3600000, an / 60000 % 60, an / 1000 % 60);
    durum(b);
}

static void kaydet(AdayP c) {
    wstring stem = g_video.yol.substr(0, g_video.yol.find_last_of(L'.'));
    HKEY k;
    wchar_t bicim[16] = L"dil";
    DWORD n = sizeof bicim;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegGetValueW(k, nullptr, L"KayitBicimi", RRF_RT_REG_SZ, nullptr, bicim, &n);
        RegCloseKey(k);
    }
    wstring hedef = wcscmp(bicim, L"sade") == 0 ? stem + L".srt" : stem + L"." + genis(c->dil) + L".srt";
    if (var_mi(hedef)) {
        int r = MessageBoxW(g_pen, (L"Bu adla bir altyazı zaten var:\n" + hedef + L"\n\nÜzerine yazılsın mı?\n(Hayır: yanına yeni adla kaydedilir)").c_str(),
                            UYG, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDNO) {
            wstring kok = hedef.substr(0, hedef.size() - 4);
            for (int i = 2; var_mi(hedef); i++) hedef = kok + L"." + std::to_wstring(i) + L".srt";
        }
    }
    if (!dosya_yaz(hedef, kaydir(c->icerik, g_kaydirma), true)) {
        if (MessageBoxW(g_pen, (L"Bu klasöre yazılamadı (salt okunur ya da izin yok):\n" + hedef + L"\n\nBaşka bir yere kaydedilsin mi?").c_str(),
                        UYG, MB_YESNO | MB_ICONWARNING) != IDYES)
            return;
        // tam yol veriyoruz: yalnız ad verilirse Windows son klasörü (yani yazılamayan video klasörünü) açar
        wstring bas = indirilenler();
        wchar_t y[MAX_PATH * 2];
        wcsncpy(y, (bas + L"\\" + hedef.substr(hedef.find_last_of(L"\\/") + 1)).c_str(), MAX_PATH * 2 - 1);
        y[MAX_PATH * 2 - 1] = 0;
        OPENFILENAMEW o{sizeof o};
        o.hwndOwner = g_pen;
        o.lpstrFilter = L"Altyazı (*.srt)\0*.srt\0";
        o.lpstrDefExt = L"srt";
        o.lpstrFile = y;
        o.nMaxFile = MAX_PATH * 2;
        o.lpstrInitialDir = bas.c_str();
        o.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&o)) return;
        hedef = y;
        if (!dosya_yaz(hedef, kaydir(c->icerik, g_kaydirma), true)) {
            MessageBoxW(g_pen, (L"Kaydedilemedi:\n" + hedef).c_str(), UYG, MB_ICONERROR);
            return;
        }
    }
    gunluk("kaydedildi: " + utf8(hedef) + " kaynak=" + c->kaynak + " kaydirma=" + std::to_string(g_kaydirma));
    bool yaninda = hedef.substr(0, hedef.find_last_of(L"\\/")) == g_video.yol.substr(0, g_video.yol.find_last_of(L"\\/"));
    durum((L"Kaydedildi: " + (yaninda ? hedef.substr(hedef.find_last_of(L"\\/") + 1) + L"  — oynatıcınız bu altyazıyı kendiliğinden bulur."
                                     : hedef + L"  — video klasöründe olmadığı için oynatıcıda elle açın.")).c_str());
}

static void eylem(int sonra) {
    AdayP c = secili();
    if (!c) { durum(L"Önce listeden bir altyazı seçin."); return; }
    if (!c->indirildi) { indir_baslat(c, sonra); return; }
    if (sonra == 1) dene(c);
    else if (sonra == 2) kaydet(c);
}

// ---------------------------------------------------------------- Türkçe sitelerde ara + İndirilenler'i izle
// Site bot korumalı: program siteyi kullanıcının tarayıcısında açar, kullanıcı indirir; inen dosyayı program yakalar.
static std::atomic<long long> g_izle_bitis{0};   // GetTickCount64 sınırı
static std::atomic<bool> g_izleyici_var{false};

static wstring indirilenler() {
    wchar_t *p = nullptr;
    wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p)) && p) d = p;
    CoTaskMemFree(p);
    return d;
}

static bool altyazi_arsivi(const wstring &ad) {
    auto n = ad.find_last_of(L'.');
    if (n == wstring::npos) return false;
    wstring u = ad.substr(n);
    for (auto &c : u) c = (wchar_t)towlower(c);
    return u == L".zip" || u == L".rar" || u == L".7z" || u == L".srt" || u == L".ass" || u == L".ssa" || u == L".sub";
}

static void izleyici_baslat() {
    g_izle_bitis = (long long)GetTickCount64() + 30 * 60 * 1000;   // 30 dakika izler
    if (g_izleyici_var.exchange(true)) return;
    std::thread([] {
        wstring dizin = indirilenler();
        FILETIME bas;
        GetSystemTimeAsFileTime(&bas);
        std::vector<wstring> gorulen;
        HANDLE h = FindFirstChangeNotificationW(dizin.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
        while (h != INVALID_HANDLE_VALUE && (long long)GetTickCount64() < g_izle_bitis) {
            WaitForSingleObject(h, 3000);
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileW((dizin + L"\\*").c_str(), &fd);
            for (BOOL ok = f != INVALID_HANDLE_VALUE; ok; ok = FindNextFileW(f, &fd)) {
                wstring ad = fd.cFileName;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !altyazi_arsivi(ad)) continue;
                if (CompareFileTime(&fd.ftLastWriteTime, &bas) < 0) continue;
                if (std::find(gorulen.begin(), gorulen.end(), ad) != gorulen.end()) continue;
                // tarayıcı yazmayı bitirsin: boyut 1 sn sabit kalmalı
                wstring yol = dizin + L"\\" + ad;
                WIN32_FILE_ATTRIBUTE_DATA a1, a2;
                GetFileAttributesExW(yol.c_str(), GetFileExInfoStandard, &a1);
                Sleep(1000);
                if (!GetFileAttributesExW(yol.c_str(), GetFileExInfoStandard, &a2) || a1.nFileSizeLow != a2.nFileSizeLow) continue;
                gorulen.push_back(ad);
                PostMessageW(g_pen, WM_INDIRILEN, 0, (LPARAM) new wstring(yol));
            }
            if (f != INVALID_HANDLE_VALUE) FindClose(f);
            FindNextChangeNotification(h);
        }
        if (h != INVALID_HANDLE_VALUE) FindCloseChangeNotification(h);
        g_izleyici_var = false;
    }).detach();
}

static void sitede_ara() {
    if (g_video.baslik.empty()) { durum(L"Önce bir video seçin."); return; }
    wstring url = L"https://turkcealtyazi.org/find.php?cat=sub&find=" + genis(url_kodla(g_video.baslik));
    ShellExecuteW(g_pen, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    izleyici_baslat();
    durum(L"turkcealtyazi.org tarayıcıda açıldı. Altyazıyı indirdiğinizde program İndirilenler'den kendiliğinden alıp listeye ekleyecek.");
}

static string arsivden_altyazi(const wstring &yol, string &ad) {
    HANDLE h = CreateFileW(yol.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    string veri((size_t)std::min<LONGLONG>(sz.QuadPart, 50 << 20), '\0');
    DWORD n = 0;
    ReadFile(h, veri.data(), (DWORD)veri.size(), &n, nullptr);
    CloseHandle(h);
    veri.resize(n);
    wstring kucuk_yol = yol;
    for (auto &c : kucuk_yol) c = (wchar_t)towlower(c);
    auto biter = [&](const wchar_t *e) { size_t l = wcslen(e); return kucuk_yol.size() > l && kucuk_yol.compare(kucuk_yol.size() - l, l, e) == 0; };
    if (biter(L".zip")) return zipten_altyazi(veri, ad);
    if (biter(L".rar") || biter(L".7z")) {   // Windows 10/11'deki tar.exe (libarchive) rar ve 7z açar
        wstring hedef = gecici_dizin() + L"\\arsiv";
        SHFILEOPSTRUCTW sil{};
        wstring cift = hedef + L'\0';
        sil.wFunc = FO_DELETE;
        sil.pFrom = cift.c_str();
        sil.fFlags = FOF_NO_UI;
        SHFileOperationW(&sil);
        CreateDirectoryW(hedef.c_str(), nullptr);
        wstring komut = L"tar.exe -xf \"" + yol + L"\" -C \"" + hedef + L"\"";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, komut.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 20000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW((hedef + L"\\*.srt").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) return {};
        FindClose(f);
        ad = utf8(fd.cFileName);
        return arsivden_altyazi(hedef + L"\\" + fd.cFileName, ad);
    }
    ad = utf8(yol.substr(yol.find_last_of(L"\\/") + 1));
    return veri;
}

static string dil_tahmin(const string &metin) {   // Türkçe harf yoğunluğu
    int tr = 0;
    for (const char *h : {"ş", "ğ", "ı", "Ş", "Ğ", "İ", "ç", "ö", "ü"})
        for (size_t p = 0; (p = metin.find(h, p)) != string::npos; p++) tr++;
    return tr > (int)metin.size() / 400 ? "tr" : "en";
}

static void indirilen_ekle(const wstring &yol, const char *kaynak = "İndirilen") {
    string ad, ham = arsivden_altyazi(yol, ad);
    if (ham.empty()) { durum((L"Dosyada altyazı bulunamadı: " + yol).c_str()); return; }
    auto c = std::make_shared<Aday>();
    c->kaynak = kaynak;
    c->dil = dil_tahmin(ham) ;
    c->icerik = utf8e_cevir(ham, c->dil);
    c->dil = dil_tahmin(c->icerik);
    c->ad = ad;
    c->indirildi = true;
    uyum_hesapla(*c, g_video);
    g_adaylar.push_back(c);
    gunluk("indirilen eklendi: " + utf8(yol));
    liste_doldur();
    for (int i = 0, n = ListView_GetItemCount(g_liste); i < n; i++) {   // yeni geleni seç
        LVITEMW it{};
        it.mask = LVIF_PARAM;
        it.iItem = i;
        ListView_GetItem(g_liste, &it);
        if (g_adaylar[it.lParam] == c) {
            ListView_SetItemState(g_liste, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_liste, i, FALSE);
        }
    }
    SetForegroundWindow(g_pen);
    durum((L"Altyazı eklendi: " + genis(ad) + L" — Dene'ye basın.").c_str());
}

// ---------------------------------------------------------------- kaldırma (Ayarlar → Uygulamalar → Kaldır)
static void sagtik_ayarla(bool ac);

static int kaldir() {
    if (MessageBoxW(nullptr, L"Türkçe Altyazı Bulucu kaldırılsın mı?\n(Kaydettiğiniz altyazılar silinmez.)", UYG, MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
    sagtik_ayarla(false);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TurkceAltyaziBulucu");
    for (REFKNOWNFOLDERID id : {FOLDERID_Desktop, FOLDERID_Programs}) {
        wchar_t *p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) DeleteFileW((wstring(p) + L"\\Türkçe Altyazı Bulucu.lnk").c_str());
        CoTaskMemFree(p);
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wstring dizin = exe;
    dizin = dizin.substr(0, dizin.find_last_of(L'\\'));
    // exe bu süreç kapanana kadar kilitli: klasör gidene kadar (en çok ~60 sn) yeniden dene
    wstring komut = L"cmd.exe /c rmdir /s /q \"" + gecici_dizin() + L"\" & for /l %i in (1,1,30) do (ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" +
                    dizin + L"\" 2>nul & if not exist \"" + dizin + L"\" exit)";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, komut.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, L"C:\\", &si, &pi))
        CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    MessageBoxW(nullptr, L"Türkçe Altyazı Bulucu kaldırıldı.", UYG, MB_ICONINFORMATION);
    return 0;
}

// ---------------------------------------------------------------- ayarlar penceresi
static HWND ay_alan[3], ay_ing, ay_sagtik, ay_oyn, ay_bicim;

void sagtik_ayarla(bool ac) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    for (auto *uz : VIDEO_UZANTI) {
        wstring anahtar = wstring(L"Software\\Classes\\SystemFileAssociations\\") + uz + L"\\shell\\TurkceAltyazi";
        if (!ac) { RegDeleteTreeW(HKEY_CURRENT_USER, anahtar.c_str()); continue; }
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, anahtar.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr)) continue;
        wstring ad = L"Türkçe altyazı bul", simge = wstring(exe) + L",0", komut = L"\"" + wstring(exe) + L"\" \"%1\"";
        RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE *)ad.c_str(), DWORD((ad.size() + 1) * 2));
        RegSetValueExW(k, L"Icon", 0, REG_SZ, (const BYTE *)simge.c_str(), DWORD((simge.size() + 1) * 2));
        HKEY kk;
        if (!RegCreateKeyExW(k, L"command", 0, nullptr, 0, KEY_WRITE, nullptr, &kk, nullptr)) {
            RegSetValueExW(kk, nullptr, 0, REG_SZ, (const BYTE *)komut.c_str(), DWORD((komut.size() + 1) * 2));
            RegCloseKey(kk);
        }
        RegCloseKey(k);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

static bool sagtik_acik() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\SystemFileAssociations\\.mkv\\shell\\TurkceAltyazi", 0, KEY_READ, &k)) return false;
    RegCloseKey(k);
    return true;
}

static HWND kontrol(HWND p, const wchar_t *sinif, const wchar_t *metin, DWORD stil, int x, int y, int w, int h, int id = 0) {
    HWND c = CreateWindowExW(wcscmp(sinif, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, sinif, metin, WS_CHILD | WS_VISIBLE | stil,
                             dpi(x), dpi(y), dpi(w), dpi(h), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK ayar_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case 1: {   // Kaydet
            Ayarlar a;
            wchar_t b[512];
            string *alan[] = {&a.os_kullanici, &a.os_sifre, &a.subdl_anahtar};
            for (int i = 0; i < 3; i++) { GetWindowTextW(ay_alan[i], b, 512); *alan[i] = utf8(b); }
            for (auto *s : alan) { while (!s->empty() && isspace((unsigned char)s->back())) s->pop_back(); while (!s->empty() && isspace((unsigned char)(*s)[0])) s->erase(0, 1); }
            a.ingilizce = Button_GetCheck(ay_ing) == BST_CHECKED;
            ayar_yaz(a);
            a.os_anahtar = ayar_oku().os_anahtar;
            HKEY k;
            if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr)) {
                const wchar_t *oyn[] = {L"otomatik", L"mpc", L"vlc", L"varsayilan"};
                const wchar_t *o = oyn[std::max(0, (int)ComboBox_GetCurSel(ay_oyn))];
                RegSetValueExW(k, L"Oynatici", 0, REG_SZ, (const BYTE *)o, DWORD((wcslen(o) + 1) * 2));
                const wchar_t *bc = ComboBox_GetCurSel(ay_bicim) == 1 ? L"sade" : L"dil";
                RegSetValueExW(k, L"KayitBicimi", 0, REG_SZ, (const BYTE *)bc, DWORD((wcslen(bc) + 1) * 2));
                RegCloseKey(k);
            }
            if (!paketli()) sagtik_ayarla(Button_GetCheck(ay_sagtik) == BST_CHECKED);
            g_ayar = a;
            liste_doldur();   // İngilizce kapatıldıysa hemen gizlensin
            DestroyWindow(w);
            return 0;
        }
        case 2: DestroyWindow(w); return 0;
        case 3: ShellExecuteW(w, L"open", L"https://www.opensubtitles.com/en/users/sign_up", nullptr, nullptr, SW_SHOWNORMAL); return 0;
        case 4: ShellExecuteW(w, L"open", L"https://subdl.com/panel/api", nullptr, nullptr, SW_SHOWNORMAL); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: EnableWindow(g_pen, TRUE); SetForegroundWindow(g_pen); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void ayarlar_ac() {
    HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TAB_Ayar", L"Ayarlar", WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
                             dpi(540), dpi(470), g_pen, nullptr, g_inst, nullptr);
    RECT r;
    GetWindowRect(g_pen, &r);
    SetWindowPos(w, nullptr, r.left + dpi(60), r.top + dpi(50), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    Ayarlar a = ayar_oku();
    int y = 12;
    HWND bas = kontrol(w, L"STATIC", L"Kaynaklar", 0, 16, y, 300, 20);
    SendMessageW(bas, WM_SETFONT, (WPARAM)g_kalin, TRUE);
    y += 24;
    bool os_var = !a.os_anahtar.empty();   // uygulama anahtarı gömülü değilse OpenSubtitles hiç aranmaz
    kontrol(w, L"STATIC", os_var ? L"BSPlayer ve OpenSubtitles hazır gelir; hiçbir şey girmeniz gerekmez. Aşağıdakiler isteğe bağlıdır."
                                 : L"BSPlayer hazır gelir; hiçbir şey girmeniz gerekmez. Aşağıdakiler isteğe bağlıdır.", 0, 16, y, 500, 20);
    y += 30;
    const wchar_t *etiket[] = {L"OpenSubtitles kullanıcı adı", L"OpenSubtitles şifre", L"Subdl API anahtarı"};
    string *deger[] = {&a.os_kullanici, &a.os_sifre, &a.subdl_anahtar};
    for (int i = 0; i < 3; i++) {
        kontrol(w, L"STATIC", etiket[i], 0, 16, y + 3, 180, 20);
        ay_alan[i] = kontrol(w, L"EDIT", genis(*deger[i]).c_str(), ES_AUTOHSCROLL | WS_TABSTOP | (i == 1 ? ES_PASSWORD : 0), 200, y, 220, 24);
        if (i == 0) kontrol(w, L"BUTTON", L"Üye ol…", WS_TABSTOP, 428, y, 92, 24, 3);
        if (i == 2) kontrol(w, L"BUTTON", L"Anahtar al…", WS_TABSTOP, 428, y, 92, 24, 4);
        y += 32;
        if (i == 1) {
            kontrol(w, L"STATIC", os_var ? L"Giriş yapılırsa günlük indirme hakkı artar (girişsiz ~5)."
                                            : L"Bu sürümde OpenSubtitles kapalı (uygulama anahtarı yok).", SS_LEFT, 200, y - 6, 330, 20);
            y += 22;
        } else if (i == 2) {
            kontrol(w, L"STATIC", L"Ek kaynak (Subscene arşivi); anahtar girilirse aranır.", SS_LEFT, 200, y - 6, 330, 20);
            y += 22;
        }
    }
    y += 6;
    bas = kontrol(w, L"STATIC", L"Davranış", 0, 16, y, 300, 20);
    SendMessageW(bas, WM_SETFONT, (WPARAM)g_kalin, TRUE);
    y += 26;
    ay_ing = kontrol(w, L"BUTTON", L"İngilizce altyazıları da listele (Türkçelerin altında)", BS_AUTOCHECKBOX | WS_TABSTOP, 16, y, 420, 22);
    Button_SetCheck(ay_ing, a.ingilizce ? BST_CHECKED : BST_UNCHECKED);
    y += 28;
    ay_sagtik = kontrol(w, L"BUTTON", L"Video dosyalarının sağ tık menüsüne \"Türkçe altyazı bul\" ekle", BS_AUTOCHECKBOX | WS_TABSTOP, 16, y, 480, 22);
    Button_SetCheck(ay_sagtik, paketli() || sagtik_acik() ? BST_CHECKED : BST_UNCHECKED);
    if (paketli()) EnableWindow(ay_sagtik, FALSE);   // paket bildirimi ekler, Windows yönetir
    y += 34;
    kontrol(w, L"STATIC", L"Deneme oynatıcısı", 0, 16, y + 3, 180, 20);
    ay_oyn = kontrol(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 200, y, 320, 200);
    for (auto *s : {L"Dahili MPC-HC (önerilen, tüm kodekler)", L"Sistemdeki MPC-HC / K-Lite", L"VLC", L"Windows varsayılanı"}) ComboBox_AddString(ay_oyn, s);
    wstring o = reg_metin(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu", L"Oynatici");
    ComboBox_SetCurSel(ay_oyn, o == L"mpc" ? 1 : o == L"vlc" ? 2 : o == L"varsayilan" ? 3 : 0);
    y += 34;
    kontrol(w, L"STATIC", L"Kayıt adı", 0, 16, y + 3, 180, 20);
    ay_bicim = kontrol(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 200, y, 320, 200);
    ComboBox_AddString(ay_bicim, L"film.tr.srt (dil etiketli)");
    ComboBox_AddString(ay_bicim, L"film.srt (televizyonlar için)");
    ComboBox_SetCurSel(ay_bicim, reg_metin(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu", L"KayitBicimi") == L"sade" ? 1 : 0);
    y += 46;
    kontrol(w, L"BUTTON", L"Kaydet", BS_DEFPUSHBUTTON | WS_TABSTOP, 324, y, 96, 28, 1);
    kontrol(w, L"BUTTON", L"Vazgeç", WS_TABSTOP, 428, y, 92, 28, 2);
    SetWindowPos(w, nullptr, 0, 0, dpi(548), dpi(y + 80), SWP_NOMOVE | SWP_NOZORDER);
    EnableWindow(g_pen, FALSE);
    ShowWindow(w, SW_SHOW);
}

// ---------------------------------------------------------------- hakkında / sorun bildir
static void hakkinda() {
    wstring os = g_ayar.os_anahtar.empty() ? L"  • OpenSubtitles.com — bu sürümde kapalı\n" : L"  • OpenSubtitles.com — parmak izi ve film adıyla\n";
    wstring subdl = g_ayar.subdl_anahtar.empty() ? L"  • Subdl — Ayarlar'da anahtar girilirse (Subscene arşivi)\n" : L"  • Subdl — film adıyla (Subscene arşivi)\n";
    MessageBoxW(g_pen,
                (L"Türkçe Altyazı Bulucu " SURUM L"\n\n"
                 L"Videoya uygun Türkçe altyazıyı bulur; İngilizceleri de altta listeler.\n\n"
                 L"Kaynaklar (aynı anda aranır):\n"
                 L"  • Videonun kendi içindeki altyazılar (MKV)\n"
                 L"  • BSPlayer altyazı servisi — videonun parmak iziyle (tam uyum)\n" +
                 os + subdl +
                 L"  • Türkçe altyazı siteleri — \"Türkçe sitelerde ara\" ile tarayıcıda; indirdiğiniz dosya listeye kendiliğinden eklenir\n\n"
                 L"Uyum puanı: parmak izi eşleşmesi, sürüm etiketleri (BluRay, WEB, 1080p, grup), altyazı süresi ve Türkçe harfler.\n"
                 L"✓ işareti: altyazı tam bu video dosyası için yüklenmiş.\n\n"
                 L"Altyazılar ilgili sitelerin kullanıcılarına aittir.\n"
                 L"Bileşenler: miniz (MIT), nlohmann/json (MIT), MPC-HC ve LAV Filters (GPL).\n"
                 L"Lisans metinleri program klasöründeki LISANSLAR.txt dosyasındadır.\n\n"
                 L"© 2026 Ali Polat").c_str(),
                L"Hakkında", MB_ICONINFORMATION);
}

static void sorun_bildir() {
    OSVERSIONINFOW os{sizeof os};
    typedef LONG(WINAPI * RGV)(OSVERSIONINFOW *);
    if (auto f = (RGV)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&os);
    string m = "Türkçe Altyazı Bulucu " + utf8(SURUM) + " · Windows " + std::to_string(os.dwMajorVersion) + "." +
               std::to_string(os.dwBuildNumber) + (paketli() ? " · Store paketi" : "") + "\r\n";
    m += "Video: " + utf8(g_video.yol) + "\r\nAd: " + g_video.baslik + " (" + std::to_string(g_video.yil) + ")  parmak izi " + g_video.hash + "\r\n";
    for (int i = 0; i < 3; i++) m += string(KAYNAK_AD[i]) + ": " + g_kaynak_metin[i] + "\r\n";
    Ayarlar a = ayar_oku();
    {
        RECT r;
        GetWindowRect(g_pen, &r);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(g_pen, MONITOR_DEFAULTTONEAREST), &mi);
        char b[200];
        snprintf(b, sizeof b, "Pencere: %ld,%ld %ldx%ld · çalışma alanı %ldx%ld · DPI %u\r\n", r.left, r.top, r.right - r.left, r.bottom - r.top,
                 mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top, GetDpiForWindow(g_pen));
        m += b;
    }
    m += "Listede: " + std::to_string(g_adaylar.size()) + " altyazı (tekrarlar ayıklandıktan sonra)\r\n";
    m += string("OpenSubtitles hesabı: ") + (a.os_kullanici.empty() ? "yok" : "var") + " · Subdl: " + (a.subdl_anahtar.empty() ? "kapalı" : "açık") + "\r\n";
    m += "\r\nSon işlemler:\r\n" + g_gunluk + "\r\nSorunu kısaca anlatın:\r\n";
    wstring w = genis(m);
    if (OpenClipboard(g_pen)) {
        EmptyClipboard();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * 2);
        memcpy(GlobalLock(h), w.c_str(), (w.size() + 1) * 2);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();
    }
    if (MessageBoxW(g_pen, L"Teşhis bilgisi (sürüm, video adı, kaynak durumları, son işlemler) panoya kopyalandı.\n"
                           L"API anahtarlarınız ve şifreniz eklenmez.\n\nBildirim sayfası açılsın mı? Açılınca Ctrl+V ile yapıştırın.",
                    L"Sorun bildir", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK)
        ShellExecuteW(g_pen, L"open", BILDIR_URL, nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- ana pencere
// pencereyi bulunduğu ekranın kullanılabilir alanına (görev çubuğu hariç) sığdırır
static void ekrana_sigdir(HWND w) {
    if (IsZoomed(w) || IsIconic(w)) return;
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) return;
    RECT r, a = mi.rcWork;
    GetWindowRect(w, &r);
    int en = std::min<int>(r.right - r.left, a.right - a.left), boy = std::min<int>(r.bottom - r.top, a.bottom - a.top);
    int x = std::max<int>(a.left, std::min<int>(r.left, a.right - en)), y = std::max<int>(a.top, std::min<int>(r.top, a.bottom - boy));
    if (x != r.left || y != r.top || en != r.right - r.left || boy != r.bottom - r.top)
        SetWindowPos(w, nullptr, x, y, en, boy, SWP_NOZORDER | SWP_NOACTIVATE);
}

static const ULONG_PTR KOPYA_VIDEO = 0x54414231;   // WM_COPYDATA etiketi
static void yerlesim(int W, int H) {
    int m = dpi(12), y = m;
    MoveWindow(g_dugme[0], m, y, dpi(130), dpi(30), TRUE);
    MoveWindow(g_bilgi, m + dpi(142), y, W - 2 * m - dpi(142), dpi(40), TRUE);
    y += dpi(46);
    MoveWindow(g_kaynak_durum, m, y + dpi(6), W - 2 * m, dpi(8), TRUE);
    y += dpi(24);
    RECT sr;
    GetWindowRect(g_durum, &sr);
    int alt = H - (sr.bottom - sr.top) - dpi(44);
    MoveWindow(g_liste, m, y, W - 2 * m, alt - y - dpi(8), TRUE);
    int x = m;
    MoveWindow(g_dugme[1], x, alt, dpi(110), dpi(32), TRUE); x += dpi(118);
    MoveWindow(g_dugme[2], x, alt, dpi(70), dpi(32), TRUE); x += dpi(74);
    MoveWindow(g_kaydirma_etiket, x, alt + dpi(7), dpi(130), dpi(20), TRUE); x += dpi(130);
    MoveWindow(g_dugme[3], x, alt, dpi(70), dpi(32), TRUE);
    MoveWindow(g_dugme[6], W - m - dpi(440), alt, dpi(182), dpi(32), TRUE);
    MoveWindow(g_dugme[5], W - m - dpi(250), alt, dpi(120), dpi(32), TRUE);
    MoveWindow(g_dugme[4], W - m - dpi(122), alt, dpi(122), dpi(32), TRUE);
    SendMessageW(g_durum, WM_SIZE, 0, 0);
    // sütun genişlikleri
    int lw = W - 2 * m - dpi(24);
    int sabit[] = {80, 70, 110, 0, 70, 0};
    int kalan = lw - dpi(80 + 70 + 110 + 70);
    for (int i = 0; i < 6; i++) ListView_SetColumnWidth(g_liste, i, i == 3 ? kalan * 6 / 10 : i == 5 ? kalan * 4 / 10 : dpi(sabit[i]));
}

static LRESULT CALLBACK pencere_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_pen = w;
        g_dugme[0] = kontrol(w, L"BUTTON", L"📂  Video Aç…", WS_TABSTOP, 0, 0, 10, 10, K_AC);
        g_bilgi = kontrol(w, L"STATIC", paketli() ? L"Videoyu bu pencereye sürükleyin ya da \"Video Aç\" ile seçin.\n"
                                                  L"İpucu: Gezgin'de videoya sağ tıklayıp \"Türkçe altyazı bul\" seçebilirsiniz."
                                                : L"Videoyu bu pencereye sürükleyin ya da \"Video Aç\" ile seçin.\n"
                                                  L"İpucu: Ayarlar'dan video dosyalarının sağ tık menüsüne ekleyebilirsiniz.", SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10);
        g_kaynak_durum = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_MARQUEE, 0, 0, 10, 10, w, nullptr, g_inst, nullptr);
        g_liste = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                  0, 0, 10, 10, w, (HMENU)K_LISTE, g_inst, nullptr);
        SendMessageW(g_liste, WM_SETFONT, (WPARAM)g_font, TRUE);
        ListView_SetExtendedListViewStyle(g_liste, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        const wchar_t *sutun[] = {L"Dil", L"Uyum", L"Kaynak", L"Sürüm / dosya adı", L"İndirme", L"Not"};
        for (int i = 0; i < 6; i++) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (LPWSTR)sutun[i];
            c.cx = 100;
            ListView_InsertColumn(g_liste, i, &c);
        }
        g_dugme[1] = kontrol(w, L"BUTTON", L"▶  Dene", WS_TABSTOP, 0, 0, 10, 10, K_DENE);
        g_dugme[2] = kontrol(w, L"BUTTON", L"− 0,5 sn", WS_TABSTOP, 0, 0, 10, 10, K_GERI);
        g_kaydirma_etiket = kontrol(w, L"STATIC", L"", SS_CENTER, 0, 0, 10, 10);
        g_dugme[3] = kontrol(w, L"BUTTON", L"+ 0,5 sn", WS_TABSTOP, 0, 0, 10, 10, K_ILERI);
        g_dugme[6] = kontrol(w, L"BUTTON", L"🌐  Türkçe sitelerde ara", WS_TABSTOP, 0, 0, 10, 10, K_SITE);
        g_dugme[5] = kontrol(w, L"BUTTON", L"Videoyu oynat", WS_TABSTOP, 0, 0, 10, 10, K_VIDEO_OYNAT);
        g_dugme[4] = kontrol(w, L"BUTTON", L"💾  Kaydet", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, K_KAYDET);
        g_durum = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, w, nullptr, g_inst, nullptr);
        SendMessageW(g_durum, WM_SETFONT, (WPARAM)g_font, TRUE);
        kaydirma_yaz();
        kaynak_durum_yaz();
        DragAcceptFiles(w, TRUE);
        return 0;
    }
    case WM_SIZE: yerlesim(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize = {dpi(760), dpi(420)}; return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        ekrana_sigdir(w);
        return 0;
    }
    case WM_COPYDATA: {   // ikinci kopyadan gelen video yolu (sağ tık / simgeye sürükleme)
        auto *cd = (COPYDATASTRUCT *)lp;
        if (cd->dwData != KOPYA_VIDEO || cd->cbData < sizeof(wchar_t)) return FALSE;
        video_yukle(wstring((const wchar_t *)cd->lpData, cd->cbData / sizeof(wchar_t) - 1));
        return TRUE;
    }
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
        wstring bulunan;
        std::vector<wstring> altyazilar;   // elle indirilmiş .srt/.zip… da bırakılabilir
        for (UINT i = 0; i < n; i++) {
            wchar_t y[MAX_PATH * 2];
            DragQueryFileW(d, i, y, MAX_PATH * 2);
            if (video_mu(y)) { if (bulunan.empty()) bulunan = y; }
            else if (altyazi_dosyasi_mu(y)) altyazilar.push_back(y);
        }
        DragFinish(d);
        SetForegroundWindow(w);   // bırakınca program öne gelsin
        if (!bulunan.empty()) video_yukle(bulunan);
        if (!altyazilar.empty() && g_video.yol.empty())
            MessageBoxW(w, L"Önce videoyu açın, sonra altyazı dosyasını pencereye bırakın: listeye eklenir, Dene ile denenir.", UYG, MB_ICONINFORMATION);
        else for (auto &y : altyazilar) indirilen_ekle(y, "Eklenen");
        if (bulunan.empty() && altyazilar.empty())
            MessageBoxW(w, L"Bırakılanlar arasında video ya da altyazı dosyası yok (mkv, mp4, avi… / srt, zip, rar…).", UYG, MB_ICONINFORMATION);
        return 0;
    }
    case WM_SONUC: {
        std::unique_ptr<Sonuc> s((Sonuc *)lp);
        if (s->nesil != g_nesil) return 0;
        g_kaynak_metin[s->kaynak] = s->durum;
        if (s->durum.find(" sonuç") != string::npos || s->durum == "bulunamadı") g_ulasilan++;
        gunluk(string(KAYNAK_AD[s->kaynak]) + ": " + s->durum);
        for (auto &c : s->adaylar) {
            bool ayni = std::any_of(g_adaylar.begin(), g_adaylar.end(), [&](const AdayP &x) {
                return (!c.ozet.empty() && x->ozet == c.ozet) || (x->kaynak == c.kaynak && x->dil == c.dil && x->ad == c.ad);
            });
            if (ayni) continue;
            auto p = std::make_shared<Aday>(c);
            uyum_hesapla(*p, g_video);
            g_adaylar.push_back(p);
        }
        kaynak_durum_yaz();
        liste_doldur();
        if (--g_bekleyen == 0) {
            kaynak_durum_yaz();
            int tr = 0;
            for (auto &c : g_adaylar) tr += c->dil == "tr";
            wchar_t b[200];
            bool gomulu_tr = std::count(g_video.gomulu.begin(), g_video.gomulu.end(), "tr");
            if (g_adaylar.empty() && !g_ulasilan)
                swprintf(b, 200, L"Altyazı kaynaklarına ulaşılamadı. İnternet bağlantınızı kontrol edip F5 ile yeniden deneyin.");
            else if (g_adaylar.empty() && gomulu_tr)
                swprintf(b, 200, L"Hazır altyazı bulunamadı, ama bu videonun İÇİNDE Türkçe altyazı var: oynatıcıda altyazı menüsünden seçin.");
            else if (g_adaylar.empty())
                swprintf(b, 200, L"Bu video için hazır altyazı bulunamadı. \"Türkçe sitelerde ara\" (Ctrl+T) ile sitelerde arayabilirsiniz.");
            else swprintf(b, 200, L"%d altyazı bulundu (%d Türkçe). Birini seçip Dene'ye basın; uyuyorsa Kaydet.", (int)g_adaylar.size(), tr);
            durum(b);
        }
        return 0;
    }
    case WM_INDI: {
        std::unique_ptr<Indi> s((Indi *)lp);
        s->aday->indiriliyor = false;
        if (s->aday->bekleyen_eylem) { s->sonra = s->aday->bekleyen_eylem; s->aday->bekleyen_eylem = 0; }
        if (s->nesil != g_nesil) return 0;
        if (!s->ok) {
            gunluk("indirme hatası (" + s->aday->kaynak + "): " + s->hata);
            durum((L"İndirilemedi: " + genis(s->hata)).c_str());
            return 0;
        }
        uyum_hesapla(*s->aday, g_video);   // süre kontrolü artık yapılabilir
        liste_doldur();
        if (s->sonra == 1) dene(s->aday);
        else if (s->sonra == 2) kaydet(s->aday);
        return 0;
    }
    case WM_INDIRILEN: {
        std::unique_ptr<wstring> y((wstring *)lp);
        indirilen_ekle(*y);
        return 0;
    }
    case WM_NOTIFY: {
        auto *n = (NMHDR *)lp;
        if (n->idFrom == K_LISTE && (n->code == NM_DBLCLK || n->code == NM_RETURN)) eylem(1);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case K_AC: {
            wchar_t y[MAX_PATH * 2] = L"";
            OPENFILENAMEW o{sizeof o};
            o.hwndOwner = w;
            o.lpstrFilter = L"Video dosyaları\0*.mkv;*.mp4;*.avi;*.m4v;*.mov;*.wmv;*.ts;*.m2ts;*.webm;*.mpg;*.mpeg;*.flv\0Tüm dosyalar\0*.*\0";
            o.lpstrFile = y;
            o.nMaxFile = MAX_PATH * 2;
            o.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
            // açık videonun klasörü; yoksa İndirilenler (Windows sonraki açılışlarda son kullanılan klasörü hatırlar)
            wstring bas = g_video.yol.empty() ? indirilenler() : g_video.yol.substr(0, g_video.yol.find_last_of(L"\\/"));
            o.lpstrInitialDir = bas.c_str();
            if (GetOpenFileNameW(&o)) video_yukle(y);
            return 0;
        }
        case K_DENE: eylem(1); return 0;
        case K_KAYDET: eylem(2); return 0;
        case K_GERI: case K_ILERI:
            g_kaydirma += LOWORD(wp) == K_GERI ? -500 : 500;
            kaydirma_yaz();
            durum(L"Kaydırma değişti; görmek için tekrar Dene'ye basın. Kaydet bu kaydırmayla yazar.");
            return 0;
        case K_VIDEO_OYNAT:
            if (!g_video.yol.empty()) {
                bool mpc;
                wstring oyn = oynatici_bul(mpc);
                wstring arg = L"\"" + g_video.yol + L"\"";
                if (oyn.empty()) ShellExecuteW(w, L"open", g_video.yol.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                else ShellExecuteW(w, L"open", oyn.c_str(), arg.c_str(), nullptr, SW_SHOWNORMAL);
            }
            return 0;
        case K_YENIDEN: ara(); return 0;
        case K_SITE: sitede_ara(); return 0;
        case K_AYARLAR: ayarlar_ac(); return 0;
        case K_HAKKINDA: hakkinda(); return 0;
        case K_BILDIR: sorun_bildir(); return 0;
        case K_CIKIS: DestroyWindow(w); return 0;
        }
        break;
    case WM_DESTROY: {   // pencere boyutu ve konumu bir sonraki açılışa
        WINDOWPLACEMENT wpl{sizeof wpl};
        GetWindowPlacement(w, &wpl);
        HKEY k;
        if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr)) {
            RegSetValueExW(k, L"Pencere", 0, REG_BINARY, (const BYTE *)&wpl, sizeof wpl);
            RegCloseKey(k);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(w, m, wp, lp);
}

static HMENU menu_yap() {
    HMENU dosya = CreatePopupMenu(), arac = CreatePopupMenu(), yardim = CreatePopupMenu(), ana = CreateMenu();
    AppendMenuW(dosya, MF_STRING, K_AC, L"&Video aç…\tCtrl+O");
    AppendMenuW(dosya, MF_STRING, K_YENIDEN, L"&Yeniden ara\tF5");
    AppendMenuW(dosya, MF_STRING, K_SITE, L"&Türkçe sitelerde ara\tCtrl+T");
    AppendMenuW(dosya, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(dosya, MF_STRING, K_CIKIS, L"Çı&kış");
    AppendMenuW(arac, MF_STRING, K_AYARLAR, L"&Ayarlar…");
    AppendMenuW(yardim, MF_STRING, K_BILDIR, L"&Sorun bildir…");
    AppendMenuW(yardim, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(yardim, MF_STRING, K_HAKKINDA, L"&Hakkında");
    AppendMenuW(ana, MF_POPUP, (UINT_PTR)dosya, L"&Dosya");
    AppendMenuW(ana, MF_POPUP, (UINT_PTR)arac, L"&Araçlar");
    AppendMenuW(ana, MF_POPUP, (UINT_PTR)yardim, L"&Yardım");
    return ana;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int goster) {
    g_inst = inst;
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2 && wcscmp(argv[1], L"--kaldir") == 0) return paketli() ? 0 : kaldir();
    if (argc >= 2 && wcscmp(argv[1], L"--sagtik") == 0) { if (!paketli()) sagtik_ayarla(true); return 0; }   // kurulum programı çağırır
    if (HWND acik = FindWindowW(L"TAB_Ana", nullptr)) {   // zaten açıksa videoyu ona gönder, ikinci pencere açma
        if (argc >= 2) {
            COPYDATASTRUCT cd{KOPYA_VIDEO, DWORD((wcslen(argv[1]) + 1) * sizeof(wchar_t)), argv[1]};
            SendMessageW(acik, WM_COPYDATA, 0, (LPARAM)&cd);
        }
        if (IsIconic(acik)) ShowWindow(acik, SW_RESTORE);
        SetForegroundWindow(acik);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, GetDpiForSystem());
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    g_kalin = CreateFontIndirectW(&ncm.lfMessageFont);
    g_ayar = ayar_oku();
    if (!paketli()) {   // güncellemeden kalan eski kopyalar (kurulum kilitli dosyaları ".eski…" adıyla kenara çeker)
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wstring d = exe;
        d = d.substr(0, d.find_last_of(L'\\'));
        for (const wchar_t *alt : {L"", L"\\mpc-hc", L"\\mpc-hc\\LAVFilters64"}) {
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileW((d + alt + L"\\*.eski*").c_str(), &fd);
            for (BOOL ok = f != INVALID_HANDLE_VALUE; ok; ok = FindNextFileW(f, &fd)) DeleteFileW((d + alt + L"\\" + fd.cFileName).c_str());
            if (f != INVALID_HANDLE_VALUE) FindClose(f);
        }
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW;   // boyut değişince eski düğme izleri kalmasın
    wc.lpfnWndProc = pencere_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"TAB_Ana";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = ayar_proc;
    wc.lpszClassName = L"TAB_Ayar";
    RegisterClassExW(&wc);

    HWND w = CreateWindowExW(0, L"TAB_Ana", UYG, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 620, nullptr, menu_yap(), inst, nullptr);
    SetWindowPos(w, nullptr, 0, 0, dpi(1000), dpi(620), SWP_NOMOVE | SWP_NOZORDER);
    WINDOWPLACEMENT wpl{};
    DWORD n = sizeof wpl;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\TurkceAltyaziBulucu", L"Pencere", RRF_RT_REG_BINARY, nullptr, &wpl, &n) == ERROR_SUCCESS &&
        n == sizeof wpl) {
        if (wpl.showCmd == SW_SHOWMINIMIZED) wpl.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(w, &wpl);
        ekrana_sigdir(w);   // ölçek ya da ekran değiştiyse kayıtlı boyut taşabilir
    } else {   // ilk açılış: ortala; %150 ölçekte küçük ekrana varsayılan boyut sığmayabilir
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY), &mi);
        RECT a = mi.rcWork, r;
        GetWindowRect(w, &r);
        int en = std::min<int>(r.right - r.left, a.right - a.left), boy = std::min<int>(r.bottom - r.top, a.bottom - a.top);
        SetWindowPos(w, nullptr, a.left + (a.right - a.left - en) / 2, a.top + (a.bottom - a.top - boy) / 2, en, boy, SWP_NOZORDER);
        ShowWindow(w, goster);
    }

    ACCEL kisayol[] = {{FCONTROL | FVIRTKEY, 'O', K_AC}, {FVIRTKEY, VK_F5, K_YENIDEN}, {FCONTROL | FVIRTKEY, 'T', K_SITE},
                       {FCONTROL | FVIRTKEY, 'S', K_KAYDET}};
    HACCEL ac = CreateAcceleratorTableW(kisayol, 4);

    if (argc >= 2) video_yukle(argv[1]);   // sağ tık menüsünden ya da exe/kısayol üzerine sürüklenince
    else durum(paketli() ? L"Videoyu pencereye sürükleyin ya da Gezgin'de sağ tıklayıp \"Türkçe altyazı bul\" seçin."
                         : L"Videoyu pencereye ya da program simgesinin üzerine sürükleyin.");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        HWND aktif = GetActiveWindow();
        if (TranslateAcceleratorW(w, ac, &msg)) continue;
        if (aktif && aktif != w && IsDialogMessageW(aktif, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
