// Altyazı kaynakları: BSPlayer (parmak izi), OpenSubtitles.com (parmak izi + ad), Subdl (ad). Hepsi aynı anda aranır.
#include "ortak.h"
#include "kaynaklar.h"

#include <algorithm>
#include <mutex>
#include <random>
#include <regex>
#include <set>

#include "../ucuncu/json.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------- BSPlayer (SOAP, s1–s8; biri yanıt vermezse sıradaki)
static string bs_soap(int sunucu, const string &yontem, const string &govde, int &durum) {
    string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" "
               "xmlns:ns1=\"http://api.bsplayer-subtitles.com/v1.php\"><SOAP-ENV:Body><ns1:" + yontem + ">" + govde + "</ns1:" + yontem +
               "></SOAP-ENV:Body></SOAP-ENV:Envelope>";
    string b = "Content-Type: text/xml; charset=utf-8\r\nSOAPAction: \"http://api.bsplayer-subtitles.com/v1.php#" + yontem +
               "\"\r\nUser-Agent: BSPlayer/2.x (1022.12362)\r\n";
    Yanit y = http("POST", "http://s" + std::to_string(sunucu) + ".api.bsplayer-subtitles.com/v1.php", b, x, 8000);
    durum = y.durum;
    return y.govde;
}

static string xml_alan(const string &x, const string &ad) {
    std::smatch m;
    if (std::regex_search(x, m, std::regex("<" + ad + "[^>]*>([^<]*)</" + ad + ">"))) return m[1];
    return {};
}

static string xml_coz(string s) {   // &amp; vb.
    const std::pair<const char *, const char *> t[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    for (auto &[a, b] : t)
        for (size_t p; (p = s.find(a)) != string::npos;) s.replace(p, strlen(a), b);
    return s;
}

std::vector<Aday> bsplayer_ara(const Video &v, const Ayarlar &a, string &durum) {
    std::vector<Aday> out;
    std::vector<int> sunucular{1, 2, 3, 4, 5, 6, 7, 8};
    std::shuffle(sunucular.begin(), sunucular.end(), std::mt19937(GetTickCount()));
    sunucular.resize(3);   // en fazla 3 sunucu: hem hızlı vazgeçer hem servisi yormaz
    for (int s : sunucular) {
        int kod;
        string g = bs_soap(s, "logIn", "<username></username><password></password><AppID>BSPlayer v2.72</AppID>", kod);
        string tok = xml_alan(g, "data");
        if (kod != 200 || xml_alan(g, "result") != "200" || tok.empty()) continue;
        string diller = a.ingilizce ? "tur,eng" : "tur";
        g = bs_soap(s, "searchSubtitles", "<handle>" + tok + "</handle><movieHash>" + v.hash + "</movieHash><movieSize>" +
                    std::to_string(v.boyut) + "</movieSize><languageId>" + diller + "</languageId><imdbId>*</imdbId>", kod);
        bs_soap(s, "logOut", "<handle>" + tok + "</handle>", kod);
        std::regex oge(R"(<item [^>]*>([\s\S]*?)</item>)");
        for (auto it = std::sregex_iterator(g.begin(), g.end(), oge); it != std::sregex_iterator(); ++it) {
            string x = (*it)[1];
            Aday c;
            c.kaynak = "BSPlayer";
            string dil = xml_alan(x, "subLang");
            c.dil = dil == "tur" ? "tr" : dil == "eng" ? "en" : dil;
            c.ad = xml_coz(xml_alan(x, "subName"));
            c.indirme_url = xml_coz(xml_alan(x, "subDownloadLink"));
            c.hash_eslesti = xml_alan(x, "movieHash") == v.hash;
            string p = xml_alan(x, "subRating");
            c.puan = p.empty() ? 0 : atof(p.c_str());
            c.fps = atof(xml_alan(x, "movieFPS").c_str());
            c.ozet = xml_alan(x, "subHash");
            bool latin_disi = false;   // "eng" etiketli ama Çince/Arapça adlı dosyalar
            for (unsigned char ch : c.ad) if (ch >= 0xE3) { latin_disi = true; break; }
            if (latin_disi) continue;
            if (!c.indirme_url.empty() && (c.dil == "tr" || c.dil == "en")) out.push_back(c);
        }
        durum = std::to_string(out.size()) + " sonuç";
        return out;
    }
    durum = "şu an yanıt vermiyor";
    return out;
}

// ---------------------------------------------------------------- OpenSubtitles.com REST (ücretsiz API anahtarı gerekir)
static const string OS_KOK = "https://api.opensubtitles.com/api/v1";

static string os_baslik(const Ayarlar &a, const string &token = "") {
    string b = "Api-Key: " + a.os_anahtar + "\r\nUser-Agent: TurkceAltyaziBulucu v1.0\r\nAccept: application/json\r\n";
    if (!token.empty()) b += "Authorization: Bearer " + token + "\r\n";
    return b;
}

std::vector<Aday> opensubtitles_ara(const Video &v, const Ayarlar &a, string &durum) {
    std::vector<Aday> out;
    if (a.os_anahtar.empty()) { durum = "API anahtarı yok (Ayarlar)"; return out; }
    string diller = a.ingilizce ? "en,tr" : "tr";
    // parametreler alfabetik sırada olmalı (aksi hâlde API yönlendirir)
    std::vector<string> sorgular{"languages=" + diller + "&moviehash=" + v.hash};
    if (!v.baslik.empty()) {
        string q = "languages=" + diller;
        if (v.sezon) q += "&episode_number=" + std::to_string(v.bolum);
        q += "&query=" + url_kodla(kucuk(v.baslik));
        if (v.sezon) q += "&season_number=" + std::to_string(v.sezon);
        if (v.yil) q += "&year=" + std::to_string(v.yil);
        sorgular.push_back(q);
        // "episode_number" alfabetik olarak "languages"tan önce gelir
        if (v.sezon) sorgular.back() = "episode_number=" + std::to_string(v.bolum) + "&languages=" + diller + "&query=" +
                                       url_kodla(kucuk(v.baslik)) + "&season_number=" + std::to_string(v.sezon);
    }
    std::set<string> gorulen;
    int hata = 0;
    for (auto &q : sorgular) {
        Yanit y = http("GET", OS_KOK + "/subtitles?" + q, os_baslik(a));
        if (y.durum == 401 || y.durum == 403) { durum = "API anahtarı geçersiz"; return out; }
        if (y.durum != 200) { hata = y.durum; continue; }
        json j = json::parse(y.govde, nullptr, false);
        if (!j.is_object() || !j.contains("data")) continue;
        for (auto &d : j["data"]) {
            auto &at = d["attributes"];
            if (!at.contains("files") || at["files"].empty()) continue;
            string fid = std::to_string(at["files"][0].value("file_id", 0));
            if (!gorulen.insert(fid).second) continue;
            Aday c;
            c.kaynak = "OpenSubtitles";
            c.dil = at.value("language", "");
            c.ad = at.value("release", "");
            if (c.ad.empty()) c.ad = at["files"][0].value("file_name", "");
            c.os_file_id = fid;
            c.hash_eslesti = at.value("moviehash_match", false);
            c.indirme_sayisi = at.value("download_count", 0);
            c.puan = at.value("ratings", 0.0);
            c.fps = at.value("fps", 0.0);
            if (c.dil == "tr" || c.dil == "en") out.push_back(c);
        }
    }
    durum = out.empty() && hata ? "hata " + std::to_string(hata) : std::to_string(out.size()) + " sonuç";
    return out;
}

// ---------------------------------------------------------------- Subdl (Subscene arşivi; ücretsiz API anahtarı gerekir)
std::vector<Aday> subdl_ara(const Video &v, const Ayarlar &a, string &durum) {
    std::vector<Aday> out;
    if (a.subdl_anahtar.empty()) { durum = "API anahtarı yok (Ayarlar)"; return out; }
    if (v.baslik.empty()) { durum = "film adı çıkarılamadı"; return out; }
    string u = "https://api.subdl.com/api/v1/subtitles?api_key=" + url_kodla(a.subdl_anahtar) + "&film_name=" + url_kodla(v.baslik) +
               "&languages=" + (a.ingilizce ? "TR,EN" : "TR") + "&subs_per_page=30";
    if (v.sezon) u += "&type=tv&season_number=" + std::to_string(v.sezon) + "&episode_number=" + std::to_string(v.bolum);
    else {
        u += "&type=movie";
        if (v.yil) u += "&year=" + std::to_string(v.yil);
    }
    Yanit y = http("GET", u);
    if (y.durum == 401 || y.durum == 403) { durum = "API anahtarı geçersiz"; return out; }
    json j = json::parse(y.govde, nullptr, false);
    if (y.durum != 200 || !j.is_object() || !j.value("status", false)) {
        durum = j.is_object() && j.contains("error") ? "bulunamadı" : "hata " + std::to_string(y.durum);
        return out;
    }
    for (auto &s : j["subtitles"]) {
        Aday c;
        c.kaynak = "Subdl";
        string dil = kucuk(s.value("language", s.value("lang", "")));
        c.dil = (dil == "tr" || dil == "turkish") ? "tr" : (dil == "en" || dil == "english") ? "en" : "";
        c.ad = s.value("release_name", s.value("name", ""));
        string url = s.value("url", "");
        if (url.empty() || c.dil.empty()) continue;
        c.indirme_url = (url.rfind("http", 0) == 0 ? "" : "https://dl.subdl.com") + url;
        out.push_back(c);
    }
    durum = std::to_string(out.size()) + " sonuç";
    return out;
}

// ---------------------------------------------------------------- indirme (UTF-8'e çevrilmiş SRT metni Aday::icerik'e)
static std::mutex os_kilit;
static string os_token;

bool indir(Aday &c, const Ayarlar &a, string &hata) {
    if (c.indirildi) return true;
    string ham;
    if (c.kaynak == "Video içi") {
        ham = mkv_srt_cikar(c.video_yol, c.gomulu);
        if (ham.empty()) { hata = "videodaki altyazı izi okunamadı"; return false; }
    } else if (c.kaynak == "OpenSubtitles") {
        std::lock_guard<std::mutex> k(os_kilit);
        if (os_token.empty() && !a.os_kullanici.empty()) {   // giriş: günlük indirme hakkı hesaba göre artar
            json g = {{"username", a.os_kullanici}, {"password", a.os_sifre}};
            Yanit y = http("POST", OS_KOK + "/login", os_baslik(a) + "Content-Type: application/json\r\n", g.dump());
            json j = json::parse(y.govde, nullptr, false);
            if (y.durum == 200 && j.is_object()) os_token = j.value("token", "");
        }
        json g = {{"file_id", std::stoll(c.os_file_id)}};
        Yanit y = http("POST", OS_KOK + "/download", os_baslik(a, os_token) + "Content-Type: application/json\r\n", g.dump());
        json j = json::parse(y.govde, nullptr, false);
        if (y.durum != 200 || !j.is_object() || !j.contains("link")) {
            hata = j.is_object() && j.contains("message") ? j["message"].get<string>() : "OpenSubtitles indirme hatası " + std::to_string(y.durum);
            return false;
        }
        Yanit d = http("GET", j["link"].get<string>());
        if (d.durum != 200) { hata = "dosya indirilemedi"; return false; }
        ham = d.govde;
    } else {
        Yanit d = http("GET", c.indirme_url, c.kaynak == "BSPlayer" ? "User-Agent: BSPlayer/2.x (1022.12362)\r\n" : "");
        if (d.durum != 200 || d.govde.empty()) { hata = "indirilemedi (" + std::to_string(d.durum) + ")"; return false; }
        ham = d.govde;
        if (ham.size() > 2 && ham[0] == 'P' && ham[1] == 'K') {
            string ad;
            ham = zipten_altyazi(ham, ad);
            if (ham.empty()) { hata = "zip içinde altyazı yok"; return false; }
        } else ham = gzip_ac(ham);
    }
    if (ham.empty()) { hata = "boş dosya"; return false; }
    c.icerik = utf8e_cevir(ham, c.dil);
    c.indirildi = true;
    return true;
}

// ---------------------------------------------------------------- uyum puanı
static std::vector<string> etiketler(const string &s) {
    static const std::regex parca(R"([a-z0-9]+)");
    static const std::set<string> onemli{"bluray", "brrip", "bdrip", "web", "webrip", "webdl", "dl", "hdtv", "dvdrip", "hdrip",
                                         "720p", "1080p", "2160p", "480p", "x264", "x265", "hevc", "h264", "h265", "10bit",
                                         "yify", "yts", "rarbg", "amzn", "nf", "dsnp", "hmax", "atvp", "extended", "remastered",
                                         "directors", "unrated", "proper", "repack", "imax"};
    std::vector<string> out;
    string k = kucuk(s);
    for (auto it = std::sregex_iterator(k.begin(), k.end(), parca); it != std::sregex_iterator(); ++it)
        if (onemli.count(it->str())) out.push_back(it->str());
    // sürüm grubu: son "-" sonrası
    auto t = k.find_last_of('-');
    if (t != string::npos && t + 1 < k.size()) {
        string g = k.substr(t + 1);
        g = g.substr(0, g.find_first_of(". []()"));
        if (g.size() >= 2 && g.size() <= 12) out.push_back("grp:" + g);
    }
    return out;
}

void uyum_hesapla(Aday &c, const Video &v) {
    c.not_.clear();
    if (c.kaynak == "Video içi") {   // aynı dosyanın içinde: senkron sorunu olamaz
        c.uyum = c.gomulu.forced ? 30 : 100;
        c.not_ = c.gomulu.forced ? "videonun içinde · yalnız zorunlu satırlar (tabela, yabancı dil)" : "videonun içinde · tam senkron";
        return;
    }
    if (c.hash_eslesti) {
        c.uyum = 95;
        c.not_ = "bu dosya için yüklenmiş";
    } else {
        auto a = etiketler(v.surum), b = etiketler(c.ad);
        int ortak = 0;
        for (auto &x : a) ortak += std::count(b.begin(), b.end(), x) ? 1 : 0;
        c.uyum = a.empty() ? 50 : 40 + 45 * ortak / (int)a.size();
        if (kucuk(c.ad).find(v.surum) != string::npos || v.surum.find(kucuk(c.ad)) != string::npos) c.uyum = std::max(c.uyum, 88);
    }
    if (c.indirildi && c.dil == "tr" && c.icerik.size() > 5000) {   // ş/ğ/ı hiç yoksa yükleyen dosyayı bozmuş
        int harf = 0;
        for (const char *h : {"ş", "ğ", "ı", "Ş", "Ğ", "İ"})
            for (size_t p = 0; (p = c.icerik.find(h, p)) != string::npos; p++) harf++;
        if (!harf) { c.uyum = std::max(5, c.uyum - 25); c.not_ = "Türkçe harfler eksik (ş, ğ, ı yok)"; }
    }
    if (c.indirildi && v.sure_sn > 600) {
        double fark = son_satir_sn(c.icerik) - v.sure_sn;
        if (fark > 120) { c.uyum = std::max(5, c.uyum - 35); c.not_ = "altyazı filmden uzun: farklı kurgu olabilir"; }
        else if (fark < -900) { c.uyum = std::max(5, c.uyum - 20); c.not_ = "altyazı erken bitiyor"; }
    }
}
