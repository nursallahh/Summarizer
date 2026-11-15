// Çekirdek C arayüzü testleri.
// Bilinçli olarak test çerçevesi kullanılmıyor: Adım 1'de bağımlılık sıfır kalsın.
// Test sayısı arttığında GoogleTest veya Catch2'ye geçilecek.

#include "da/da_api.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::printf("[GECTI] %s\n", description);
    } else {
        std::printf("[KALDI] %s\n", description);
        ++g_failures;
    }
}

}  // namespace

int main() {
    // Sürüm boş olmamalı ve CMake'teki proje sürümüyle aynı olmalı.
    const char* version = da_version();
    check(version != nullptr && std::strlen(version) > 0, "da_version bos olmayan bir metin doner");
    check(std::string(version) == DA_TEST_EXPECTED_VERSION, "da_version CMake proje surumuyle ayni");

    // Başlıktaki ABI sürümü ile derlenmiş DLL'inki aynı olmalı.
    check(da_abi_version() == DA_ABI_VERSION, "da_abi_version basliktaki DA_ABI_VERSION ile ayni");

    // Hata mesajları her kod için tanımlı olmalı.
    check(std::strlen(da_status_message(DA_OK)) > 0, "DA_OK icin mesaj var");
    check(std::strlen(da_status_message(DA_ERR_INTERNAL)) > 0, "DA_ERR_INTERNAL icin mesaj var");
    check(std::strlen(da_status_message(static_cast<da_status>(999))) > 0, "Bilinmeyen kod icin mesaj var");

    // NULL çıkış parametresi reddedilmeli, çökmemeli.
    check(da_build_info(nullptr) == DA_ERR_INVALID_ARGUMENT, "da_build_info(NULL) gecersiz parametre doner");

    // Derleme bilgisi geçerli bir metin döndürmeli ve serbest bırakılabilmeli.
    char* json = nullptr;
    const da_status status = da_build_info(&json);
    check(status == DA_OK, "da_build_info basarili");
    check(json != nullptr && json[0] == '{', "da_build_info JSON nesnesi doner");
    check(json != nullptr && std::strstr(json, DA_TEST_EXPECTED_VERSION) != nullptr, "JSON surumu iceriyor");
    da_free_string(json);

    // NULL serbest bırakma güvenli olmalı.
    da_free_string(nullptr);
    check(true, "da_free_string(NULL) cokmez");

    if (g_failures > 0) {
        std::printf("\n%d test KALDI\n", g_failures);
        return 1;
    }
    std::printf("\nTum testler gecti\n");
    return 0;
}
