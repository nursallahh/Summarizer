#!/bin/sh
# Ders Arşivi — veri koruma kontrolü
#
# Kişisel veri, model dosyası veya API anahtarı depoya girmesin diye dosya listesini denetler.
# İki yerde çalışır:
#   1) pre-commit kancası: commit'e eklenen dosyalar (tools/hooks/pre-commit)
#   2) GitHub Actions: depodaki tüm dosyalar (git ls-files)
#
# Kullanım:  tools/veri-kontrol.sh dosya1 dosya2 ...
# Çıkış kodu: 0 = temiz, 1 = sorun bulundu

MAX_BYTES=5242880   # 5 MB: bundan büyük dosya kod deposuna ait değildir

problems=0

report() {
    echo "  ENGEL: $1  ->  $2"
    problems=$((problems + 1))
}

is_allowed_media_path() {
    case "$1" in
        unity/Assets/*)            return 0 ;;  # arayüz görselleri
        docs/images/*)             return 0 ;;  # belgelerdeki ekran görüntüleri (kişisel bilgi içermemeli)
        core/tests/fixtures/*)     return 0 ;;  # sentetik test dosyaları
    esac
    return 1
}

for file in "$@"; do
    [ -f "$file" ] || continue   # silinmiş dosyalar atlanır

    lower=$(printf '%s' "$file" | tr '[:upper:]' '[:lower:]')

    # 1) Veri klasörleri
    case "$lower" in
        data/*|veri/*|eval/data/*|eval/private/*)
            report "$file" "veri klasörü depoya giremez (veri C:\\DersArsivi-veri\\ altında durur)" ;;
    esac

    # 2) Etiket dosyaları
    case "$lower" in
        *etiket*.csv|*label*.csv)
            report "$file" "etiket dosyası depoya giremez" ;;
    esac

    # 3) Model dosyaları
    case "$lower" in
        *.gguf|*.onnx|*.safetensors|*.traineddata)
            report "$file" "model dosyası depoya giremez (uygulama indirir)" ;;
    esac

    # 4) Fotoğraf, belge ve medya: yalnız izinli klasörlerde
    case "$lower" in
        *.jpg|*.jpeg|*.png|*.heic|*.webp|*.bmp|*.tif|*.tiff|*.pdf|*.doc|*.docx|*.ppt|*.pptx|*.mp3|*.m4a|*.wav|*.ogg|*.mp4|*.mov)
            if ! is_allowed_media_path "$file"; then
                report "$file" "fotoğraf/belge yalnız unity/Assets, docs/images veya core/tests/fixtures altında olabilir"
            fi ;;
    esac

    # 5) Gizli bilgi dosyaları
    case "$lower" in
        .env|.env.*|*/.env|*.key|*.pem|secrets.*)
            report "$file" "gizli bilgi dosyası depoya giremez" ;;
    esac

    # 6) Boyut
    size=$(wc -c < "$file" | tr -d ' ')
    if [ "$size" -gt "$MAX_BYTES" ]; then
        report "$file" "dosya 5 MB'tan büyük ($size bayt)"
    fi

    # 7) İçerikte API anahtarı izi (Google: AIza..., OpenAI: sk-..., Anthropic: sk-ant-...)
    #    Bu betiğin kendisi desenleri içerdiği için atlanır.
    if [ "$file" != "tools/veri-kontrol.sh" ]; then
        if grep -I -q -E 'AIza[0-9A-Za-z_-]{35}|(^|[^A-Za-z0-9])sk-(ant-)?[A-Za-z0-9_-]{20,}' "$file" 2>/dev/null; then
            report "$file" "API anahtarına benzeyen bir metin içeriyor"
        fi
    fi
done

if [ "$problems" -gt 0 ]; then
    echo ""
    echo "Veri kontrolü: $problems sorun bulundu. Bu dosyaları commit'ten çıkarın:"
    echo "  git restore --staged <dosya>"
    exit 1
fi

exit 0
