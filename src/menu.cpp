// Türkçe Altyazı Bulucu — Windows 11 kısa sağ tık menüsü (yalnız MSIX paketinde)
// Gezgin bu DLL'i paket bildirimindeki CLSID ile yükler (IExplorerCommand); "Türkçe altyazı bul" → uygulamayı videoyla açar.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include <new>
#include <string>

using std::wstring;

// {696CDC41-8DB2-499E-9289-3B9F78BE9A39} — paket/AppxManifest.xml ile aynı olmalı
static const CLSID CLSID_Menu = {0x696cdc41, 0x8db2, 0x499e, {0x92, 0x89, 0x3b, 0x9f, 0x78, 0xbe, 0x9a, 0x39}};
static HMODULE g_mod;
static LONG g_kilit;

static wstring dll_dizini() {
    wchar_t yol[MAX_PATH];
    GetModuleFileNameW(g_mod, yol, MAX_PATH);
    wstring d = yol;
    return d.substr(0, d.find_last_of(L'\\'));
}

// Uygulamayı paket kimliğiyle açmak için önce yürütme takma adı (%LOCALAPPDATA%\Microsoft\WindowsApps), yoksa paketteki exe
static wstring uygulama_yolu() {
    wchar_t *p = nullptr;
    wstring takma;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p)
        takma = wstring(p) + L"\\Microsoft\\WindowsApps\\TurkceAltyaziBulucu.exe";
    CoTaskMemFree(p);
    if (!takma.empty() && GetFileAttributesW(takma.c_str()) != INVALID_FILE_ATTRIBUTES) return takma;
    return dll_dizini() + L"\\TurkceAltyaziBulucu.exe";
}

class Komut : public IExplorerCommand {
    LONG ref_ = 1;

public:
    Komut() { InterlockedIncrement(&g_kilit); }
    virtual ~Komut() { InterlockedDecrement(&g_kilit); }

    IFACEMETHODIMP QueryInterface(REFIID iid, void **p) override {
        if (iid == IID_IUnknown || iid == IID_IExplorerCommand) {
            *p = static_cast<IExplorerCommand *>(this);
            AddRef();
            return S_OK;
        }
        *p = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (!r) delete this;
        return r;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray *, LPWSTR *ad) override { return SHStrDupW(L"Türkçe altyazı bul", ad); }
    IFACEMETHODIMP GetIcon(IShellItemArray *, LPWSTR *simge) override {
        return SHStrDupW((dll_dizini() + L"\\TurkceAltyaziBulucu.exe,0").c_str(), simge);
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray *, LPWSTR *ipucu) override { *ipucu = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID *g) override { *g = CLSID_Menu; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray *secim, BOOL, EXPCMDSTATE *durum) override {
        DWORD n = 0;   // uygulama tek video açar: çoklu seçimde gösterme
        *durum = secim && SUCCEEDED(secim->GetCount(&n)) && n == 1 ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray *secim, IBindCtx *) override {
        if (!secim) return E_INVALIDARG;
        IShellItem *oge = nullptr;
        if (FAILED(secim->GetItemAt(0, &oge))) return E_FAIL;
        wchar_t *yol = nullptr;
        HRESULT hr = oge->GetDisplayName(SIGDN_FILESYSPATH, &yol);
        oge->Release();
        if (FAILED(hr)) return hr;
        wstring exe = uygulama_yolu(), komut = L"\"" + exe + L"\" \"" + yol + L"\"";
        CoTaskMemFree(yol);
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), komut.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            return HRESULT_FROM_WIN32(GetLastError());
        AllowSetForegroundWindow(pi.dwProcessId);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS *f) override { *f = ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand **e) override { *e = nullptr; return E_NOTIMPL; }
};

class Fabrika : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID iid, void **p) override {
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *p = static_cast<IClassFactory *>(this);
            return S_OK;
        }
        *p = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }   // durağan nesne
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }
    IFACEMETHODIMP CreateInstance(IUnknown *dis, REFIID iid, void **p) override {
        *p = nullptr;
        if (dis) return CLASS_E_NOAGGREGATION;
        Komut *k = new (std::nothrow) Komut;
        if (!k) return E_OUTOFMEMORY;
        HRESULT hr = k->QueryInterface(iid, p);
        k->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL kilitle) override {
        kilitle ? InterlockedIncrement(&g_kilit) : InterlockedDecrement(&g_kilit);
        return S_OK;
    }
};
static Fabrika g_fabrika;

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD neden, LPVOID) {
    if (neden == DLL_PROCESS_ATTACH) {
        g_mod = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **p) {
    if (clsid != CLSID_Menu) { *p = nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
    return g_fabrika.QueryInterface(iid, p);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() { return g_kilit ? S_FALSE : S_OK; }
