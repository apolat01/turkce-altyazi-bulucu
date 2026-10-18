// Türkçe Altyazı Bulucu kurulumu — yönetici istemez, kullanıcı klasörüne kurar.
//   %LOCALAPPDATA%\Programs\TurkceAltyaziBulucu\{TurkceAltyaziBulucu.exe, mpc-hc\ (dahili oynatıcı), LISANSLAR.txt}
//   masaüstü + Başlat kısayolu, video dosyalarına sağ tık "Türkçe altyazı bul", Uygulamalar listesinde "Kaldır"
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <string>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "../ucuncu/miniz/miniz.h"

using std::wstring;

static const wchar_t *AD = L"Türkçe Altyazı Bulucu";
static const wchar_t *SURUM = L"1.0.0";
static const wchar_t *KALDIR = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TurkceAltyaziBulucu";

static wstring bilinen(REFKNOWNFOLDERID id) {
    wchar_t *p = nullptr;   // KF_FLAG_CREATE: yeni kullanıcıda Programs klasörü henüz olmayabilir
    wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) d = p;
    CoTaskMemFree(p);
    return d;
}

static bool kaynak(int no, const void *&veri, DWORD &boy) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(no), (LPCWSTR)RT_RCDATA);
    if (!r) return false;
    veri = LockResource(LoadResource(nullptr, r));
    boy = SizeofResource(nullptr, r);
    return veri != nullptr;
}

static wstring g_hata;   // sessiz kurulumda %TEMP%\\TurkceAltyaziBulucu-kurulum.log'a yazılır

static bool yaz(const wstring &yol, const void *veri, size_t boy) {
    HANDLE h = CreateFileW(yol.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION) {
        // Gezgin simgeyi göstermek için exe/dll'i açık tutuyor olabilir: açık dosyanın adı değiştirilebilir,
        // eskisi ".eski" olarak kenara çekilir (program bir sonraki açılışta siler), yenisi yazılır
        wstring kenar = yol + L".eski" + std::to_wstring(GetTickCount());
        if (MoveFileExW(yol.c_str(), kenar.c_str(), MOVEFILE_REPLACE_EXISTING))
            h = CreateFileW(yol.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (h == INVALID_HANDLE_VALUE) {
        g_hata += L"yazılamadı (" + std::to_wstring(GetLastError()) + L"): " + yol + L"\r\n";
        return false;
    }
    DWORD n;
    BOOL ok = WriteFile(h, veri, (DWORD)boy, &n, nullptr) && n == boy;
    CloseHandle(h);
    return ok;
}

static wstring genis(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    wstring w(n ? n - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

// Dahili MPC-HC (GPL, LAV Filters içinde): gömülü zip'i <dizin>\mpc-hc altına aç
static bool mpc_ac(const wstring &hedef) {
    const void *veri;
    DWORD boy;
    if (!kaynak(102, veri, boy)) return false;
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, veri, boy, 0)) return false;
    bool ok = true;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z) && ok; i++) {
        char ad[512];
        mz_zip_reader_get_filename(&z, i, ad, sizeof ad);
        wstring yol = hedef + L"\\" + genis(ad);
        for (auto &c : yol) if (c == L'/') c = L'\\';
        if (mz_zip_reader_is_file_a_directory(&z, i)) {
            SHCreateDirectoryExW(nullptr, yol.c_str(), nullptr);
            continue;
        }
        SHCreateDirectoryExW(nullptr, yol.substr(0, yol.find_last_of(L'\\')).c_str(), nullptr);
        size_t n = 0;
        void *p = mz_zip_reader_extract_to_heap(&z, i, &n, 0);
        ok = p && yaz(yol, p, n);
        mz_free(p);
    }
    mz_zip_reader_end(&z);
    // taşınabilir mod: ayarlarını kendi klasöründe tutsun, kullanıcının kendi MPC-HC'sine dokunmasın
    const wchar_t ini[] = L"\xFEFF[Settings]\r\nUpdaterAutoCheck=0\r\n";   // UTF-16 (MPC-HC'nin biçimi); güncelleme sorusu çıkmasın
    yaz(hedef + L"\\mpc-hc64.ini", ini, sizeof ini - sizeof(wchar_t));
    return ok;
}

// Güncelleme: açık program ve dahili oynatıcı kapanmazsa dosyalar yazılamaz
static void calisani_kapat(const wstring &dizin) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe)) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t yol[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, yol, &n) && _wcsnicmp(yol, dizin.c_str(), dizin.size()) == 0) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 3000);
        }
        CloseHandle(h);
    }
    CloseHandle(s);
}

static void kisayol(const wstring &lnk, const wstring &hedef, const wstring &dizin) {
    IShellLinkW *sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void **)&sl))) return;
    sl->SetPath(hedef.c_str());
    sl->SetWorkingDirectory(dizin.c_str());
    sl->SetDescription(L"Videoyu bu simgenin üzerine sürükleyin: Türkçe altyazı bulunur");
    sl->SetIconLocation(hedef.c_str(), 0);
    IPersistFile *pf;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void **)&pf))) {
        pf->Save(lnk.c_str(), TRUE);
        pf->Release();
    }
    sl->Release();
}

static void reg_sz(HKEY k, const wchar_t *ad, const wstring &v) {
    RegSetValueExW(k, ad, 0, REG_SZ, (const BYTE *)v.c_str(), DWORD((v.size() + 1) * sizeof(wchar_t)));
}

static void calistir(const wstring &exe, const wstring &arg, const wstring &dizin, bool bekle) {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    wstring komut = L"\"" + exe + L"\" " + arg;
    if (CreateProcessW(nullptr, komut.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB, nullptr, dizin.c_str(), &si, &pi) ||
        CreateProcessW(nullptr, komut.data(), nullptr, nullptr, FALSE, 0, nullptr, dizin.c_str(), &si, &pi)) {
        if (bekle) WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static unsigned long long klasor_boyutu(const wstring &d) {
    unsigned long long t = 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((d + L"\\*").c_str(), &fd);
    for (BOOL ok = f != INVALID_HANDLE_VALUE; ok; ok = FindNextFileW(f, &fd)) {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) t += klasor_boyutu(d + L"\\" + fd.cFileName);
        else t += (unsigned long long)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
    }
    if (f != INVALID_HANDLE_VALUE) FindClose(f);
    return t;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR komut, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool sessiz = wcsstr(komut, L"/sessiz") || wcsstr(komut, L"/S");
    if (!sessiz && MessageBoxW(nullptr,
                               L"Türkçe Altyazı Bulucu kurulsun mu?\n\n"
                               L"• Videoyu masaüstündeki simgenin ya da programın üzerine sürükleyin.\n"
                               L"• Video dosyalarına sağ tıklayınca \"Türkçe altyazı bul\" çıkar.\n"
                               L"• Altyazıları denemek için tüm kodekleri açan MPC-HC oynatıcı da kurulur.\n\n"
                               L"Yönetici izni gerekmez.",
                               L"Türkçe Altyazı Bulucu Kurulum", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;

    wstring dizin = bilinen(FOLDERID_UserProgramFiles) + L"\\TurkceAltyaziBulucu";
    SHCreateDirectoryExW(nullptr, dizin.c_str(), nullptr);
    calisani_kapat(dizin);
    wstring exe = dizin + L"\\TurkceAltyaziBulucu.exe";
    const void *veri;
    DWORD boy;
    bool ok = kaynak(101, veri, boy) && yaz(exe, veri, boy);
    if (ok && kaynak(103, veri, boy)) yaz(dizin + L"\\LISANSLAR.txt", veri, boy);
    ok = ok && mpc_ac(dizin + L"\\mpc-hc");
    if (!ok) {
        wchar_t t[MAX_PATH];
        GetTempPathW(MAX_PATH, t);
        HANDLE h = CreateFileW((wstring(t) + L"TurkceAltyaziBulucu-kurulum.log").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD n;
            WriteFile(h, g_hata.c_str(), DWORD(g_hata.size() * sizeof(wchar_t)), &n, nullptr);
            CloseHandle(h);
        }
        if (!sessiz) MessageBoxW(nullptr, (L"Dosyalar yazılamadı:\n" + g_hata).c_str(), L"Kurulum", MB_ICONERROR);
        return 1;
    }
    kisayol(bilinen(FOLDERID_Desktop) + L"\\Türkçe Altyazı Bulucu.lnk", exe, dizin);
    kisayol(bilinen(FOLDERID_Programs) + L"\\Türkçe Altyazı Bulucu.lnk", exe, dizin);
    calistir(exe, L"--sagtik", dizin, true);   // video dosyalarına sağ tık menüsü

    HKEY k;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, KALDIR, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr)) {
        reg_sz(k, L"DisplayName", AD);
        reg_sz(k, L"DisplayVersion", SURUM);
        reg_sz(k, L"Publisher", L"Ali Polat");
        reg_sz(k, L"DisplayIcon", exe);
        reg_sz(k, L"InstallLocation", dizin);
        reg_sz(k, L"UninstallString", L"\"" + exe + L"\" --kaldir");
        DWORD bir = 1;
        RegSetValueExW(k, L"NoModify", 0, REG_DWORD, (const BYTE *)&bir, sizeof bir);
        RegSetValueExW(k, L"NoRepair", 0, REG_DWORD, (const BYTE *)&bir, sizeof bir);
        DWORD kb = (DWORD)(klasor_boyutu(dizin) / 1024);   // Ayarlar → Uygulamalar'da görünen boyut
        RegSetValueExW(k, L"EstimatedSize", 0, REG_DWORD, (const BYTE *)&kb, sizeof kb);
        RegCloseKey(k);
    }
    if (!sessiz)   // önce mesaj, sonra program: tersi olursa mesaj programın arkasında kalıyor
        MessageBoxW(nullptr, L"Kurulum tamam.\n\nMasaüstündeki \"Türkçe Altyazı Bulucu\" simgesinin üzerine bir video sürükleyin.\n"
                             L"Kaldırmak için: Ayarlar → Uygulamalar → Türkçe Altyazı Bulucu.",
                    L"Türkçe Altyazı Bulucu Kurulum", MB_ICONINFORMATION | MB_SETFOREGROUND);
    calistir(exe, L"", dizin, false);
    return 0;
}
