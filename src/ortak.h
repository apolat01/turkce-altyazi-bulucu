// Türkçe Altyazı Bulucu — ortak tanımlar
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

using std::string;
using std::wstring;

// MKV içindeki bir altyazı izi
struct GomuluIz {
    int no = 0;           // TrackNumber
    string dil, ad, codec;
    bool forced = false, varsayilan = false;
    bool metin() const { return codec == "S_TEXT/UTF8" || codec == "S_TEXT/ASS" || codec == "S_TEXT/SSA" || codec == "S_ASS" || codec == "S_SSA"; }
};

// ---------------------------------------------------------------- bulunan altyazı
struct Aday {
    string kaynak;        // "BSPlayer", "OpenSubtitles", "Subdl"
    string dil;           // "tr" / "en"
    string ad;            // sürüm / dosya adı
    string indirme_url;   // BSPlayer: doğrudan gz · Subdl: zip · OpenSubtitles: boş (file_id ile istenir)
    string os_file_id;    // OpenSubtitles file_id
    string ozet;          // kaynağın içerik özeti (BSPlayer subHash) — aynı altyazıyı iki kez göstermemek için
    bool hash_eslesti = false;
    int indirme_sayisi = 0;
    double puan = 0;      // kaynağın kullanıcı puanı (0–10), yoksa 0
    double fps = 0;
    // hesaplananlar
    int uyum = 0;         // 0–100
    string not_;          // kullanıcıya kısa açıklama
    string icerik;        // indirildikten sonra UTF-8 SRT metni
    bool indirildi = false;
    bool indiriliyor = false;   // yalnız arayüz iş parçacığı kullanır
    int bekleyen_eylem = 0;     // indirme sürerken basılan Dene/Kaydet
    // kaynak "Video içi": videonun içindeki altyazı izi (indirme yerine çıkarılır)
    wstring video_yol;
    GomuluIz gomulu;
};

// ---------------------------------------------------------------- video bilgisi
struct Video {
    wstring yol;
    string hash;          // OpenSubtitles/BSPlayer 64 bit parmak izi (16 hex)
    uint64_t boyut = 0;
    string baslik;        // dosya adından çıkarılan film adı
    int yil = 0;
    int sezon = 0, bolum = 0;
    string surum;         // dosya adı (sürüm karşılaştırması için, küçük harf)
    double sure_sn = 0;   // Windows'un bildirdiği süre (bilinmiyorsa 0)
    std::vector<string> gomulu;   // MKV içindeki altyazı izlerinin dilleri ("tr", "en", …)
    std::vector<GomuluIz> izler;  // MKV içindeki altyazı izleri
};



// ---------------------------------------------------------------- ayarlar (HKCU\Software\TurkceAltyaziBulucu)
struct Ayarlar {
    string os_anahtar, os_kullanici, os_sifre;   // OpenSubtitles.com
    string subdl_anahtar;
    bool ingilizce = true;                        // Türkçe yoksa İngilizce de ara
};
Ayarlar ayar_oku();
void ayar_yaz(const Ayarlar &a);

// ---------------------------------------------------------------- yardımcılar (yardimci.cpp)
string utf8(const wstring &w);
wstring genis(const string &s);
string kucuk(string s);

struct Yanit {
    int durum = 0;        // HTTP durum kodu, 0 = bağlantı hatası
    string govde;
};
// url: https://… ya da http://… · basliklar: "Ad: değer\r\n" biçiminde
Yanit http(const string &yontem, const string &url, const string &basliklar = "", const string &govde = "", int zaman_asimi_ms = 15000);
string url_kodla(const string &s);

bool parmak_izi(const wstring &yol, string &hash, uint64_t &boyut);
void video_cozumle(Video &v);                       // ad, yıl, sezon/bölüm, süre, gömülü altyazılar
std::vector<GomuluIz> mkv_izler(const wstring &yol);
string mkv_srt_cikar(const wstring &yol, const GomuluIz &iz);   // izi SRT metni olarak çıkarır (boş = hata)
string gzip_ac(const string &veri);                 // boş = hata
string zipten_altyazi(const string &veri, string &dosya_adi);   // ilk .srt (yoksa .ass/.ssa/.sub)
string utf8e_cevir(const string &ham, const string &dil);       // UTF-8 değilse 1254/1252'den çevir
double son_satir_sn(const string &srt);             // son altyazının bitiş zamanı (sn)
string kaydir(const string &srt, int ms);           // tüm zamanları ms kadar kaydır
long long deneme_ani_ms(const string &srt);         // filmin ~%30'unda konuşma olan bir an
