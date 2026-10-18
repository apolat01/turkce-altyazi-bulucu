# Microsoft Store'a yükleme rehberi

## 1. Geliştirici hesabı (bir kez, Ali)
1. https://storedeveloper.microsoft.com → "Get started" → **Individual developer** (bireysel geliştirici, ücretsiz).
2. Microsoft hesabıyla giriş yap, kimlik doğrulamasını tamamla (kimlik fotoğrafı ve selfie).
3. Onaylanınca Partner Center'a (https://partner.microsoft.com/dashboard) girilir.

## 2. Ad ayırma
Partner Center → Apps and games → **New product** → **MSIX or PWA app** → ad: `Türkçe Altyazı Bulucu`.

## 3. Paket kimliği (Claude'a ver)
Ürün → **Product management → Product identity** sayfasındaki üç değer:
- `Package/Identity/Name`
- `Package/Identity/Publisher` (CN=… ile başlar)
- `Package/Properties/PublisherDisplayName`

Bunlar `paket/kimlik.txt` dosyasına yazılır, sonra paket yeniden üretilir:
```
AD=<Package/Identity/Name>
YAYINCI=<Package/Identity/Publisher>
YAYINCI_AD=<PublisherDisplayName>
SURUM=1.0.0.0
```
Ardından `./derle.sh && ./paketle.sh` komutları çalıştırılır (deneme makinesi açık olmalı). Çıktı `yapi/TurkceAltyaziBulucu.msix` olur, imzasızdır; Store kendisi imzalar.
Store sürüm numarasının son hanesi 0 olmalı (1.0.0.0, 1.0.1.0 …) ve her yüklemede bir öncekinden büyük olmalı.

## 4. Gönderim (Submission)
- **Pricing and availability:** Free; pazarlar: tümü (ya da yalnız Türkiye).
- **Properties:** Kategori Photo & video. "Does this product access, collect or transmit personal information?" sorusuna **No** cevabı verilir (gerekçe için gizlilik.html'e bak). Destek iletişimi alanına bir e-posta ya da URL yazılır.
- **Age ratings:** IARC anketi. Şiddet, cinsellik, kumar ve kullanıcılar arası iletişim yok. İnternetten içerik (altyazı metni) indiriyor; kullanıcı içerik paylaşmıyor. Beklenen sonuç: 3+ / PEGI 3.
- **Packages:** `TurkceAltyaziBulucu.msix` yüklenir.
- **Store listings → Türkçe (tr-TR):** metinler `listeleme.md` dosyasında, ekran görüntüleri `ekran-goruntuleri/` klasöründe.
- **Submission options → Restricted capabilities:** runFullTrust gerekçesi `listeleme.md` dosyasında. "Notes for certification" alanına da aynı dosyadaki notlar yapıştırılır.

## 5. Sonra
- Sertifikasyon genelde 1–3 iş günü sürer. Reddedilirse gerekçe e-postayla gelir.
- Güncelleme için: `paket/kimlik.txt` içinde SURUM artırılır, `./derle.sh && ./paketle.sh` çalıştırılır, Partner Center'da yeni bir gönderim (submission) açılıp paket yüklenir.

## Deneme (Store'suz)
`./paketle.sh --test` komutu, deneme sertifikasıyla imzalı bir paket üretir.
Bu paketi kurmak için `yapi/TurkceAltyaziBulucu-test.cer` sertifikası önce yönetici olarak **Yerel Makine → Güvenilen Kişiler** deposuna eklenir, sonra .msix dosyasına çift tıklanır.
Deneme sertifikası yalnız bu amaç içindir; dağıtılmaz.
