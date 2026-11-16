#!/bin/sh
# Summarizer — data protection check
#
# Checks a list of files so that personal data, model files or API keys never enter the repository.
# Runs in two places:
#   1) pre-commit hook: files being committed (tools/hooks/pre-commit)
#   2) GitHub Actions: every file in the repository (git ls-files)
#
# Usage:      tools/check-data.sh file1 file2 ...
# Exit code:  0 = clean, 1 = problems found

MAX_BYTES=5242880   # 5 MB: anything larger does not belong in a code repository

problems=0

report() {
    echo "  BLOCKED: $1  ->  $2"
    problems=$((problems + 1))
}

is_allowed_media_path() {
    case "$1" in
        unity/Assets/*)            return 0 ;;  # UI images
        docs/images/*)             return 0 ;;  # screenshots in docs (must not contain personal data)
        core/tests/fixtures/*)     return 0 ;;  # synthetic test files
    esac
    return 1
}

for file in "$@"; do
    [ -f "$file" ] || continue   # deleted files are skipped

    lower=$(printf '%s' "$file" | tr '[:upper:]' '[:lower:]')

    # 1) Data folders
    case "$lower" in
        data/*|veri/*|eval/data/*|eval/private/*)
            report "$file" "data folders are not allowed (real data lives outside the repo)" ;;
    esac

    # 2) Label files
    case "$lower" in
        *etiket*.csv|*label*.csv)
            report "$file" "label files are not allowed" ;;
    esac

    # 3) Model files
    case "$lower" in
        *.gguf|*.onnx|*.safetensors|*.traineddata)
            report "$file" "model files are not allowed (the app downloads them)" ;;
    esac

    # 4) Photos, documents and media: only in allowed folders
    case "$lower" in
        *.jpg|*.jpeg|*.png|*.heic|*.webp|*.bmp|*.tif|*.tiff|*.pdf|*.doc|*.docx|*.ppt|*.pptx|*.mp3|*.m4a|*.wav|*.ogg|*.mp4|*.mov)
            if ! is_allowed_media_path "$file"; then
                report "$file" "photos/documents are only allowed under unity/Assets, docs/images or core/tests/fixtures"
            fi ;;
    esac

    # 5) Secret files
    case "$lower" in
        .env|.env.*|*/.env|*.key|*.pem|secrets.*)
            report "$file" "secret files are not allowed" ;;
    esac

    # 6) Size
    size=$(wc -c < "$file" | tr -d ' ')
    if [ "$size" -gt "$MAX_BYTES" ]; then
        report "$file" "file is larger than 5 MB ($size bytes)"
    fi

    # 7) API key patterns in content (Google "AIza..." keys and common "sk-..." style keys)
    #    This script contains the patterns itself, so it is skipped.
    if [ "$file" != "tools/check-data.sh" ]; then
        if grep -I -q -E 'AIza[0-9A-Za-z_-]{35}|(^|[^A-Za-z0-9])sk-(ant-)?[A-Za-z0-9_-]{20,}' "$file" 2>/dev/null; then
            report "$file" "contains text that looks like an API key"
        fi
    fi
done

if [ "$problems" -gt 0 ]; then
    echo ""
    echo "Data check: $problems problem(s) found. Remove these files from the commit:"
    echo "  git restore --staged <file>"
    exit 1
fi

exit 0
