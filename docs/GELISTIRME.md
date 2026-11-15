# Geliştirme

Bu belge projeyi bilgisayarınızda derlemek ve çalıştırmak için gereken adımları anlatır.

## İlk kurulum (bir kez)

1. Visual Studio 2022 veya daha yeni bir sürümü **"C++ ile masaüstü geliştirme"** iş yüküyle kurun.
2. CMake (3.25+) ve Git'i kurun.
3. vcpkg'yi kurun ve yolunu ortam değişkeni olarak tanımlayın:

   ```powershell
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\vcpkg
   ```

   `setx` sonrası açık olan PowerShell pencerelerini kapatıp yeniden açın.

4. Depodaki veri koruma kontrolünü etkinleştirin (depo klasöründe):

   ```powershell
   git config core.hooksPath tools/hooks
   ```

   Bundan sonra her `git commit`, eklenen dosyaları denetler. Fotoğraf, PDF, etiket dosyası, model veya API anahtarı commit'e girmeye çalışırsa commit durdurulur.

## Derleme ve test

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Çalıştığını doğrulayın:

```powershell
.\build\windows-release\bin\Release\ders-arsivi.exe --version
.\build\windows-release\bin\Release\ders-arsivi.exe --info
```

Hata ayıklama için `windows-release` yerine `windows-debug` kullanın.

## Klasör yapısı

| Klasör | İçerik |
| --- | --- |
| `core/` | C++ çekirdek kütüphane (`da_core.dll`). Dışarı açık tek sözleşme: `core/include/da/da_api.h` |
| `core/tests/` | Çekirdek testleri |
| `cli/` | Komut satırı aracı (`ders-arsivi.exe`) |
| `unity/` | Unity ön yüz projesi (Adım 1'in ikinci yarısında eklenecek) |
| `tools/` | Veri koruma kontrolü ve git kancaları |
| `docs/` | Geliştirme belgeleri |

## Kurallar

- **İş mantığı C++ çekirdekte yazılır.** Unity yalnızca `da_api.h` fonksiyonlarını çağırır ve sonucu gösterir.
- **C arayüzünü değiştirirken** `da_api.h` başındaki sözleşme kurallarına uyun. Geriye dönük uyumsuz bir değişiklikte `DA_ABI_VERSION` artırılır.
- **Gerçek ders dosyaları depoya girmez.** Test verisi `C:\DersArsivi-veri\` altında durur. Testlerde yalnızca `core/tests/fixtures/` altındaki sentetik dosyalar kullanılır.
