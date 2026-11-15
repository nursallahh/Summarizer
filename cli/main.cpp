// ders-arsivi: Çekirdeği Unity olmadan kullanan komut satırı aracı.
// Faz 0'da doğruluk testleri ve toplu işler bu araçla yapılır.

#include "da/da_api.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

void print_help() {
    std::printf(
        "Ders Arşivi komut satırı aracı\n"
        "\n"
        "Kullanım:\n"
        "  ders-arsivi --version   Çekirdek sürümünü gösterir\n"
        "  ders-arsivi --info      Derleme bilgisini JSON olarak gösterir\n"
        "  ders-arsivi --help      Bu yardımı gösterir\n");
}

int print_info() {
    char* json = nullptr;
    const da_status status = da_build_info(&json);
    if (status != DA_OK) {
        std::fprintf(stderr, "Hata: %s\n", da_status_message(status));
        return 1;
    }
    std::printf("%s\n", json);
    da_free_string(json);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Türkçe karakterler Windows konsolunda doğru görünsün.
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }
    if (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0) {
        std::printf("ders-arsivi %s\n", da_version());
        return 0;
    }
    if (std::strcmp(argv[1], "--info") == 0) {
        return print_info();
    }

    std::fprintf(stderr, "Bilinmeyen komut: %s\n\n", argv[1]);
    print_help();
    return 2;
}
