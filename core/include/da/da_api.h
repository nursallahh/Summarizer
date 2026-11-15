/*
 * Ders Arşivi — C++ çekirdeğin dışarıya açık C arayüzü (C ABI).
 *
 * Bu başlık, çekirdek ile onu kullanan her şey arasındaki tek sözleşmedir:
 * Unity (C#, P/Invoke), komut satırı aracı ve testler yalnızca bu fonksiyonları çağırır.
 *
 * SÖZLEŞME KURALLARI
 * 1. Metinler: Tüm metinler UTF-8'dir (dosya yolları dahil).
 * 2. Hata: Bir iş yapan her fonksiyon da_status döner. DA_OK dışındaki her değer hatadır.
 * 3. Bellek sahipliği:
 *    - "const char*" dönen fonksiyonlar (ör. da_version) çekirdeğe ait sabit bir metin döner.
 *      Çağıran bunu SERBEST BIRAKMAZ.
 *    - "char** out_..." parametresiyle metin veren fonksiyonlar yeni bellek ayırır.
 *      Çağıran işi bitince bu metni da_free_string ile SERBEST BIRAKIR.
 * 4. Uyumluluk: Bu dosyada geriye dönük uyumsuz bir değişiklik yapılırsa DA_ABI_VERSION artırılır.
 *    Unity tarafı açılışta da_abi_version() değerini kendi beklediği değerle karşılaştırır.
 */
#ifndef DA_API_H
#define DA_API_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(DA_CORE_BUILDING)
#    define DA_API __declspec(dllexport)
#  else
#    define DA_API __declspec(dllimport)
#  endif
#else
#  define DA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* C arayüzünün sürümü. Yalnızca uyumsuz değişikliklerde artar. */
#define DA_ABI_VERSION 1

typedef enum da_status {
    DA_OK = 0,
    DA_ERR_INVALID_ARGUMENT = 1, /* Geçersiz parametre (ör. NULL işaretçi) */
    DA_ERR_OUT_OF_MEMORY = 2,    /* Bellek ayrılamadı */
    DA_ERR_INTERNAL = 3          /* Beklenmeyen iç hata */
} da_status;

/* Çekirdeğin sürümü, ör. "0.0.1". Çekirdeğe aittir; serbest bırakılmaz. */
DA_API const char* da_version(void);

/* Çekirdeğin derlendiği C arayüzü sürümü (DA_ABI_VERSION). */
DA_API int32_t da_abi_version(void);

/* Hata kodunun Türkçe açıklaması. Çekirdeğe aittir; serbest bırakılmaz. */
DA_API const char* da_status_message(da_status status);

/*
 * Derleme bilgisini JSON olarak verir:
 *   {"version":"0.0.1","abi":1,"compiler":"...","build_type":"..."}
 * *out_json yeni ayrılmış bir metindir; çağıran da_free_string ile serbest bırakır.
 */
DA_API da_status da_build_info(char** out_json);

/* Çekirdeğin ayırdığı bir metni serbest bırakır. NULL güvenlidir. */
DA_API void da_free_string(char* str);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DA_API_H */
