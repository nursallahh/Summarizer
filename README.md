# Ders Arşivi

**Dağınık ders dosyalarını, düzenli ve çalışılabilir bir ders arşivine dönüştür.**

Ders Arşivi; tahta ve defter fotoğraflarını, slaytları, PDF'leri ve ekran görüntülerini derslere göre düzenleyen, içerikte arama yapmayı sağlayan ve bu dosyalardan **kaynağı gösterilen** özet ve çalışma soruları üreten ücretsiz, açık kaynak bir Windows uygulamasıdır.

> **Durum: Erken geliştirme (Faz 0 — temeller).**
> Henüz kullanılabilir bir sürüm yoktur. İlk indirilebilir sürüm (v0.1), dosya düzenleme akışı tamamlandığında yayınlanacaktır.

---

## Ne yapacak?

- **Ders listesi:** Dersleri elle ekleme veya ders programı belgesinden çıkarıp onaylama.
- **Toplu düzenleme:** Seçilen dosyaları analiz edip ders önerir; tüm sonuçlar tek bir önizleme ekranında onaylanır ya da düzeltilir.
- **Dürüst belirsizlik:** Emin olunamayan dosyalar zorla bir derse atanmaz; "İnceleme gerekli", "Birden fazla ders", "Ders dışı" ve "Okunamadı" durumları vardır.
- **Arama:** Dosya adına değil içeriğe göre arama. Türkçe karakter kullanmadan yazılan sorgular da çalışır.
- **Kaynaklı çalışma (sonraki aşama):** Özet ve sorulardaki her bilgi, geldiği dosyanın ilgili sayfasına bağlanır.
- **Güvenli düzen:** Dosyalar uygulama içindeki ders koleksiyonlarında düzenlenir. Uygulama hiçbir dosyayı silmez ve üzerine yazmaz; her toplu işlem geri alınabilir.

## Mimari

```
┌─────────────────────────┐      ┌──────────────────────────┐
│ Unity ön yüzü (C#)      │      │ Komut satırı aracı (C++) │
│ Görsellik ve sadelik    │      │ Testler ve toplu işler   │
└───────────┬─────────────┘      └────────────┬─────────────┘
            │  C arayüzü (P/Invoke)           │
┌───────────▼─────────────────────────────────▼─────────────┐
│ C++ çekirdek kütüphane (backend)                           │
│ İçe aktarma · OCR · Sınıflandırma · Arama · Üretim         │
│ İş kuyruğu · İşleme yönlendirici · Yerel yedek modeller    │
└───────────┬──────────────────────────────────┬────────────┘
            │                                  │ yalnız zor işler
┌───────────▼─────────────┐      ┌─────────────▼────────────┐
│ Yerel veri (SQLite)     │      │ Bulut yapay zekâ API'si  │
│ Bilgisayarda kalır      │      │ Kullanıcının kendi       │
└─────────────────────────┘      │ anahtarıyla              │
                                 └──────────────────────────┘
```

- **C++ çekirdek** tüm iş mantığını taşır; **Unity** yalnızca arayüzü çizer.
- **Ucuz işler bilgisayarda** yapılır: PDF metni, basılı metin OCR'ı, arama, kolay sınıflandırmalar.
- **Zor işler bulutta** yapılır: el yazısı ve tahta fotoğrafları, belirsiz sınıflandırmalar, özet ve soru üretimi.
- **Kota dolmak üzereyse veya internet yoksa iş durmaz:** uygulama bilgisayardaki yedek modelle devam eder.

## Gizlilik

- Dosyalarınız, veritabanı ve arama dizini **bilgisayarınızda kalır**. Sunucu ve hesap yoktur.
- Buluta yalnızca zor işlerin **küçültülmüş** içeriği gider; dosya adı, klasör yolu ve konum bilgisi (EXIF) gönderilmez.
- Her ders için **"yalnız yerel"** seçeneği vardır; bu derslerden hiçbir veri buluta çıkmaz.
- Uygulama kullanım verisi (telemetri) toplamaz.

> ⚠️ **Ücretsiz API katmanları hakkında:** Bazı sağlayıcılar, ücretsiz katmanda gönderilen içeriği kendi ürünlerini geliştirmek için kullanabilir ve bu içerik insanlar tarafından okunabilir (ör. Gemini API ücretsiz kotası). Hassas bilgi içeren dersler için "yalnız yerel" seçeneğini veya ücretli bir katmanı kullanın. Ayrıntılar: [Gemini API şartları](https://ai.google.dev/terms).

## Geliştirme ortamı

| Araç | Not |
| --- | --- |
| Windows 10/11 (64 bit) | İlk hedef platform |
| Visual Studio 2022 | "C++ ile masaüstü geliştirme" iş yükü |
| CMake | 3.25 veya üstü |
| vcpkg | `VCPKG_ROOT` ortam değişkeni tanımlı olmalı |
| Git | |
| Unity Hub + Unity LTS | Ön yüz için |
| NVIDIA GPU (isteğe bağlı) | Yerel yedek modelleri hızlandırır |

## Derleme

> Aşağıdaki komutlar, depo iskeleti (CMake ön ayarları) eklendikten sonra geçerlidir.

```powershell
git clone https://github.com/<kullanici-adi>/ders-arsivi.git
cd ders-arsivi

# C++ çekirdek ve komut satırı aracı
cmake --preset windows-release
cmake --build --preset windows-release

# Çalıştığını doğrula
.\build\windows-release\cli\Release\ders-arsivi.exe --version
```

**Unity ön yüzü:** Unity Hub'da `unity/` klasörünü açın. Derleme sırasında çekirdek DLL'i otomatik olarak `unity/Assets/Plugins/x86_64/` altına kopyalanır.

## Klasör yapısı

```
ders-arsivi/
├─ core/       C++ çekirdek kütüphane (DLL) ve testleri
├─ cli/        Komut satırı aracı
├─ unity/      Unity ön yüz projesi
├─ eval/       Değerlendirme betikleri ve raporlar (veri içermez)
├─ docs/       Mimari ve karar kayıtları
└─ README.md
```

## Katkı kuralları

1. **Kişisel veri asla depoya girmez.** Gerçek ders dosyaları, etiket dosyaları, ekran görüntüleri ve çıkarılmış metinler depo dışında tutulur (ör. `C:\DersArsivi-veri\`). `.gitignore` ve pre-commit kontrolü bunu engellemek için vardır; ikisini de devre dışı bırakmayın.
2. **API anahtarları asla koda, dosyaya veya loglara yazılmaz.**
3. **İş mantığı Unity'de değil, C++ çekirdekte yazılır.** Unity yalnızca çekirdeği çağırır ve sonucu gösterir.
4. **Test dosyaları sentetik olmalıdır** (`core/tests/fixtures/`): gerçek ders materyali veya gerçek kişilere ait içerik kullanılmaz.

## Doğruluk hakkında

Hedef, desteklenen dosya türlerinde dosyaların **en az %90'ını doğru derse** eşleştirmektir. Bu bir hedeftir, garanti değildir. Her sürümle birlikte, hangi dosya türlerinde ve kaç dosyayla ölçüldüğünü gösteren bir doğruluk raporu `eval/reports/` altında yayınlanır.

## Yol haritası

| Faz | İçerik | Zaman (tahmini) |
| --- | --- | --- |
| Faz 0 | Temeller: depo iskeleti, Unity köprüsü, OCR ve model karşılaştırması | Kasım – Aralık 2026 |
| Faz 1 | Düzenleme akışı (MVP-A) → v0.1 | Ocak – Şubat 2027 |
| Faz 2 | Pilot kullanım ve iyileştirme | Mart – Nisan 2027 |
| Faz 3 | Kaynaklı çalışma asistanı (MVP-B) → v1.0 | Nisan – Haziran 2027 |

## Lisans

Lisans henüz seçilmedi (MIT veya Apache-2.0 değerlendiriliyor). Seçildiğinde `LICENSE` dosyası eklenecektir. Üçüncü taraf kütüphanelerin lisansları `THIRD_PARTY_NOTICES` dosyasında listelenecektir.
