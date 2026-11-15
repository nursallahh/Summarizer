#include "files/image_prep.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <objbase.h>
#  include <wincodec.h>
#endif

#include <algorithm>

namespace smz {

#if defined(_WIN32)

namespace {

template <typename T>
struct Com {
    T* p = nullptr;
    ~Com() {
        if (p != nullptr) {
            p->Release();
        }
    }
    T** put() { return &p; }
    T* operator->() const { return p; }
};

// Initializes COM for this thread if it is not initialized yet, and undoes it at the end.
struct ComScope {
    bool owned = false;
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owned = SUCCEEDED(hr);  // S_OK or S_FALSE; RPC_E_CHANGED_MODE means already initialized differently
    }
    ~ComScope() {
        if (owned) {
            CoUninitialize();
        }
    }
};

// EXIF orientation 1..8 to a WIC transform.
WICBitmapTransformOptions transform_for(int orientation) {
    switch (orientation) {
        case 2: return WICBitmapTransformFlipHorizontal;
        case 3: return WICBitmapTransformRotate180;
        case 4: return WICBitmapTransformFlipVertical;
        case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 6: return WICBitmapTransformRotate90;
        case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
        case 8: return WICBitmapTransformRotate270;
        default: return WICBitmapTransformRotate0;
    }
}

int read_orientation(IWICBitmapFrameDecode* frame) {
    Com<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(reader.put()))) {
        return 1;
    }
    for (const wchar_t* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(query, &value))) {
            int orientation = 1;
            if (value.vt == VT_UI2) {
                orientation = value.uiVal;
            }
            PropVariantClear(&value);
            return orientation >= 1 && orientation <= 8 ? orientation : 1;
        }
    }
    return 1;
}

}  // namespace

std::optional<PreparedImage> prepare_image(const std::filesystem::path& path, int max_side, int jpeg_quality) {
    ComScope com;
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())))) {
        return std::nullopt;
    }
    Com<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  decoder.put()))) {
        return std::nullopt;  // e.g. HEIC without the HEIF extension installed
    }
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.put()))) {
        return std::nullopt;
    }
    UINT width = 0;
    UINT height = 0;
    frame->GetSize(&width, &height);
    if (width == 0 || height == 0) {
        return std::nullopt;
    }

    const double scale = std::min(1.0, static_cast<double>(max_side) / std::max(width, height));
    const UINT scaled_w = std::max(1u, static_cast<UINT>(width * scale + 0.5));
    const UINT scaled_h = std::max(1u, static_cast<UINT>(height * scale + 0.5));

    Com<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(frame.p, scaled_w, scaled_h, WICBitmapInterpolationModeHighQualityCubic))) {
        return std::nullopt;
    }
    Com<IWICBitmapFlipRotator> rotator;
    if (FAILED(factory->CreateBitmapFlipRotator(rotator.put())) ||
        FAILED(rotator->Initialize(scaler.p, transform_for(read_orientation(frame.p))))) {
        return std::nullopt;
    }
    Com<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(rotator.p, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return std::nullopt;
    }
    UINT out_w = 0;
    UINT out_h = 0;
    converter->GetSize(&out_w, &out_h);

    Com<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()))) {
        return std::nullopt;
    }
    Com<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache))) {
        return std::nullopt;
    }
    Com<IWICBitmapFrameEncode> out_frame;
    Com<IPropertyBag2> options;
    if (FAILED(encoder->CreateNewFrame(out_frame.put(), options.put()))) {
        return std::nullopt;
    }
    if (options.p != nullptr) {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = static_cast<float>(std::clamp(jpeg_quality, 1, 100)) / 100.0f;
        options->Write(1, &option, &value);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(out_frame->Initialize(options.p)) || FAILED(out_frame->SetSize(out_w, out_h)) ||
        FAILED(out_frame->SetPixelFormat(&format)) || FAILED(out_frame->WriteSource(converter.p, nullptr)) ||
        FAILED(out_frame->Commit()) || FAILED(encoder->Commit())) {
        return std::nullopt;
    }

    HGLOBAL memory = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.p, &memory))) {
        return std::nullopt;
    }
    STATSTG stat{};
    stream->Stat(&stat, STATFLAG_NONAME);
    const auto size = static_cast<std::size_t>(stat.cbSize.QuadPart);
    const void* data = GlobalLock(memory);
    if (data == nullptr) {
        return std::nullopt;
    }
    PreparedImage image;
    image.jpeg.assign(static_cast<const char*>(data), size);
    image.width = static_cast<int>(out_w);
    image.height = static_cast<int>(out_h);
    GlobalUnlock(memory);
    return image;
}

#else

std::optional<PreparedImage> prepare_image(const std::filesystem::path&, int, int) {
    return std::nullopt;
}

#endif

}  // namespace smz
