#pragma once
#include "ortak.h"

std::vector<Aday> bsplayer_ara(const Video &v, const Ayarlar &a, string &durum);
std::vector<Aday> opensubtitles_ara(const Video &v, const Ayarlar &a, string &durum);
std::vector<Aday> subdl_ara(const Video &v, const Ayarlar &a, string &durum);
bool indir(Aday &c, const Ayarlar &a, string &hata);
void uyum_hesapla(Aday &c, const Video &v);
