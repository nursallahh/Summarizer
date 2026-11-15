# Ders Arşivi

Dağınık ders dosyalarını düzenli ve çalışılabilir bir ders arşivine dönüştüren ücretsiz, açık kaynak bir Windows uygulaması.

## Amaç

Öğrencilerin ders materyalleri; tahta ve defter fotoğrafları, slaytlar, PDF'ler ve ekran görüntüleri telefon galerisine, WhatsApp'a ve İndirilenler klasörüne dağılmış durumda. Ders Arşivi bu dosyaları derslere göre düzenler, içerikte aramayı sağlar ve dosyalardan kaynağı gösterilen özet ve çalışma soruları üretir.

## Kullanım

1. Derslerinizi ekleyin: elle girin ya da ders programınızın görselini veya PDF'ini yükleyip çıkarılan dersleri onaylayın.
2. Düzenlemek istediğiniz dosyaları seçin ya da uygulama penceresine sürükleyin.
3. Uygulama her dosya için bir ders önerir. Önerileri tek ekranda inceleyin, gerekirse düzeltin ve onaylayın. Emin olunamayan dosyalar "İnceleme gerekli" olarak ayrılır.
4. Arşivinizde dosya adına değil içeriğe göre arayın.
5. Bir ders, konu veya dosya seçip özet ve çalışma soruları üretin. Her bilginin hangi dosyadan geldiği gösterilir.

Uygulama dosyalarınızı silmez veya üzerine yazmaz; her toplu işlem geri alınabilir. Dosyalarınız bilgisayarınızda kalır.

## Gereksinimler

**Kullanım için**
- Windows 10 veya 11 (64 bit)
- İsteğe bağlı: Gemini API anahtarı. Anahtar girilirse el yazısı okuma ve özet üretimi için bulut kullanılır; kota dolduğunda veya internet olmadığında uygulama bilgisayardaki yedek modelle devam eder.
- İsteğe bağlı: NVIDIA ekran kartı (yedek modeli hızlandırır)

> **Not:** Gemini API'nin ücretsiz katmanında gönderilen içerik Google tarafından ürün geliştirmek için kullanılabilir. Hassas bilgi içeren dersler için uygulamadaki "yalnız yerel" seçeneğini kullanın.

**Geliştirme için**
- Visual Studio 2022 ("C++ ile masaüstü geliştirme" iş yükü)
- CMake 3.25 veya üstü
- vcpkg
- Unity (LTS sürümü)

## Lisans

Bu proje Apache License 2.0 ile dağıtılmaktadır. Ayrıntılar için `LICENSE` dosyasına bakın.
