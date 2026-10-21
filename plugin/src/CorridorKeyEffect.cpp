#include "CorridorKeyEffect.h"
#include "CorridorKeyUIDs.h"

#include "avx2uids.h"
#include "avx2attributes.h"
#include "acfextras.h"
#include "acfassert.h"
#include "acfvalue.h"
#include "ACFPtr.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

ACF_DEFINE_FACTORY(CCorridorKey_Effect)

namespace {

std::string LogPath()
{
    // Program Files is not writable by Media Composer — log under LocalAppData.
    char la[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH);
    std::string root;
    if (n > 0 && n < MAX_PATH)
        root = std::string(la) + "\\CorridorKeyAVX";
    else
        root = "C:\\Temp\\CorridorKeyAVX";
    return root + "\\logs\\effect.log";
}

void EffectLog(const std::string& line)
{
    const std::string path = LogPath();
    const auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        CreateDirectoryA(path.substr(0, pos).c_str(), nullptr);
    }
    std::ofstream f(path, std::ios::app);
    if (!f)
        return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char ts[64];
    sprintf_s(ts, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    f << ts << line << "\n";
}

float Clamp01(float v)
{
    if (v < 0.f) return 0.f;
    if (v > 1.f) return 1.f;
    return v;
}

float Clamp255(float v)
{
    if (v < 0.f) return 0.f;
    if (v > 255.f) return 255.f;
    return v;
}

bool IsYuvColorSpace(avx2ColorSpace cs)
{
    return cs == kAVXColorSpace_YCbCr_601 || cs == kAVXColorSpace_YCbCr_709;
}

void Yuv8ToRgb01(float Y, float Cb, float Cr, float& r, float& g, float& b)
{
    const float y = Y - 16.f;
    const float u = Cb - 128.f;
    const float v = Cr - 128.f;
    r = Clamp01((1.164f * y + 1.596f * v) / 255.f);
    g = Clamp01((1.164f * y - 0.813f * v - 0.391f * u) / 255.f);
    b = Clamp01((1.164f * y + 2.018f * u) / 255.f);
}

void Rgb01ToYuv8(float r, float g, float b, float& Y, float& Cb, float& Cr)
{
    const float R = Clamp01(r) * 255.f;
    const float G = Clamp01(g) * 255.f;
    const float B = Clamp01(b) * 255.f;
    Y = Clamp255(16.f + 0.257f * R + 0.504f * G + 0.098f * B);
    Cb = Clamp255(128.f - 0.148f * R - 0.291f * G + 0.439f * B);
    Cr = Clamp255(128.f + 0.439f * R - 0.368f * G - 0.071f * B);
}

acfUInt32 YuvMacroPixelBytes(IAVXImageFormat* format)
{
    acfUInt32 pixelsz = format->GetPixelSize(0);
    if (pixelsz == 0)
        return 0;
    if (format->GetSubsampling() == kAVXSubsampling_4220)
        pixelsz *= 2;
    return pixelsz;
}

// GetChannel() returns a component index (not a byte offset). Index into the
// correctly typed pixel array; GetPixelSize() is still the byte stride.
// Expand video-range code values (16–235) to full 0–1 for the model.
float ExpandVideo8(float code)
{
    return Clamp01((code - 16.f) / 219.f);
}

float CompressVideo8(float linear)
{
    return Clamp255(16.f + Clamp01(linear) * 219.f);
}

bool IsVideoRange(IAVXImageFormat* format)
{
    return format && format->GetBlackWhiteLevel() == kAVXBlackWhiteLevel_VideoRange;
}

bool WritePixelRGBA(IAVXImageFormat* format, avx2Pixel8* pixel, float r, float g, float b, float a)
{
    const acfSInt32 ri = format->GetChannel(kAVXChannelType_Red);
    const acfSInt32 gi = format->GetChannel(kAVXChannelType_Green);
    const acfSInt32 bi = format->GetChannel(kAVXChannelType_Blue);
    const acfSInt32 ai = format->GetChannel(kAVXChannelType_Alpha);
    if (ri < 0 || gi < 0 || bi < 0)
        return false;

    const avx2ComponentType ct = format->GetComponentType();
    r = Clamp01(r); g = Clamp01(g); b = Clamp01(b); a = Clamp01(a);
    const bool video = IsVideoRange(format);

    if (ct == kAVXComponentType_SHORT_2_14) {
        auto* p = reinterpret_cast<avx2Pixel2_14*>(pixel);
        p[ri] = static_cast<avx2Pixel2_14>(r * 16384.0f + 0.5f);
        p[gi] = static_cast<avx2Pixel2_14>(g * 16384.0f + 0.5f);
        p[bi] = static_cast<avx2Pixel2_14>(b * 16384.0f + 0.5f);
        if (ai >= 0)
            p[ai] = static_cast<avx2Pixel2_14>(16384);
        return true;
    }
    if (ct == kAVXComponentType_USHORT || ct == kAVXComponentType_USHORT_10_6) {
        auto* p = reinterpret_cast<acfUInt16*>(pixel);
        if (video) {
            // Map 0–1 → 16<<8 .. 235<<8 roughly (scale 16–235 into 16-bit)
            auto toVideo16 = [](float v) -> acfUInt16 {
                return static_cast<acfUInt16>(CompressVideo8(v) * (65535.f / 255.f) + 0.5f);
            };
            p[ri] = toVideo16(r);
            p[gi] = toVideo16(g);
            p[bi] = toVideo16(b);
        } else {
            p[ri] = static_cast<acfUInt16>(r * 65535.0f + 0.5f);
            p[gi] = static_cast<acfUInt16>(g * 65535.0f + 0.5f);
            p[bi] = static_cast<acfUInt16>(b * 65535.0f + 0.5f);
        }
        if (ai >= 0)
            p[ai] = 65535;
        return true;
    }
    // Default: 8-bit — restore video levels when the host expects them
    if (video) {
        pixel[ri] = static_cast<avx2Pixel8>(CompressVideo8(r) + 0.5f);
        pixel[gi] = static_cast<avx2Pixel8>(CompressVideo8(g) + 0.5f);
        pixel[bi] = static_cast<avx2Pixel8>(CompressVideo8(b) + 0.5f);
    } else {
        pixel[ri] = static_cast<avx2Pixel8>(r * 255.0f + 0.5f);
        pixel[gi] = static_cast<avx2Pixel8>(g * 255.0f + 0.5f);
        pixel[bi] = static_cast<avx2Pixel8>(b * 255.0f + 0.5f);
    }
    if (ai >= 0)
        pixel[ai] = 255;
    return true;
}

bool ReadPixelRGBA(IAVXImageFormat* format, const avx2Pixel8* pixel, float& r, float& g, float& b, float& a)
{
    const acfSInt32 ri = format->GetChannel(kAVXChannelType_Red);
    const acfSInt32 gi = format->GetChannel(kAVXChannelType_Green);
    const acfSInt32 bi = format->GetChannel(kAVXChannelType_Blue);
    const acfSInt32 ai = format->GetChannel(kAVXChannelType_Alpha);
    if (ri < 0 || gi < 0 || bi < 0)
        return false;

    const avx2ComponentType ct = format->GetComponentType();
    const bool video = IsVideoRange(format);

    if (ct == kAVXComponentType_SHORT_2_14) {
        const auto* p = reinterpret_cast<const avx2Pixel2_14*>(pixel);
        r = Clamp01(static_cast<float>(p[ri]) / 16384.0f);
        g = Clamp01(static_cast<float>(p[gi]) / 16384.0f);
        b = Clamp01(static_cast<float>(p[bi]) / 16384.0f);
        a = (ai >= 0) ? Clamp01(static_cast<float>(p[ai]) / 16384.0f) : 1.f;
        return true;
    }
    if (ct == kAVXComponentType_USHORT || ct == kAVXComponentType_USHORT_10_6) {
        const auto* p = reinterpret_cast<const acfUInt16*>(pixel);
        if (video) {
            auto fromVideo16 = [](acfUInt16 c) -> float {
                return ExpandVideo8(static_cast<float>(c) * (255.f / 65535.f));
            };
            r = fromVideo16(p[ri]);
            g = fromVideo16(p[gi]);
            b = fromVideo16(p[bi]);
        } else {
            r = static_cast<float>(p[ri]) / 65535.0f;
            g = static_cast<float>(p[gi]) / 65535.0f;
            b = static_cast<float>(p[bi]) / 65535.0f;
        }
        a = (ai >= 0) ? static_cast<float>(p[ai]) / 65535.0f : 1.f;
        return true;
    }
    if (video) {
        r = ExpandVideo8(static_cast<float>(pixel[ri]));
        g = ExpandVideo8(static_cast<float>(pixel[gi]));
        b = ExpandVideo8(static_cast<float>(pixel[bi]));
    } else {
        r = pixel[ri] / 255.0f;
        g = pixel[gi] / 255.0f;
        b = pixel[bi] / 255.0f;
    }
    a = (ai >= 0) ? (pixel[ai] / 255.0f) : 1.f;
    return true;
}

bool ConvertYuvInputToFloatRGBA(IAVXImage* image, IAVXImageFormat* format, std::vector<float>& out)
{
    const acfUInt32 w = image->GetWidth();
    const acfUInt32 h = image->GetHeight();
    const acfSInt32 cb = format->GetChannel(kAVXChannelType_Cb);
    const acfSInt32 y1 = format->GetChannel(kAVXChannelType_Y1);
    const acfSInt32 cr = format->GetChannel(kAVXChannelType_Cr);
    const acfSInt32 y2 = format->GetChannel(kAVXChannelType_Y2);
    const acfUInt32 macroBytes = YuvMacroPixelBytes(format);
    if (cb < 0 || y1 < 0 || cr < 0 || y2 < 0 || macroBytes == 0 || (w % 2) != 0)
        return false;

    EffectLog(
        "format YUV ct=" + std::to_string(static_cast<int>(format->GetComponentType())) +
        " macro=" + std::to_string(macroBytes) +
        " cs=" + std::to_string(static_cast<int>(format->GetColorSpace())) +
        " cb=" + std::to_string(cb) + " y1=" + std::to_string(y1) +
        " cr=" + std::to_string(cr) + " y2=" + std::to_string(y2));

    out.resize(static_cast<size_t>(w) * h * 4);
    double sumR = 0, sumG = 0, sumB = 0;
    double sumY = 0, sumCb = 0, sumCr = 0;
    float minC = 1.f, maxC = 0.f;
    int rawYMin = 255, rawYMax = 0;
    for (acfUInt32 y = 0; y < h; ++y) {
        avx2Pixel8* ipix = image->GetLinePtr(y, 0);
        if (!ipix)
            return false;
        for (acfUInt32 x = 0; x < w; x += 2) {
            const int Y1i = ipix[y1];
            const int Y2i = ipix[y2];
            const int Cbi = ipix[cb];
            const int Cri = ipix[cr];
            rawYMin = (std::min)(rawYMin, (std::min)(Y1i, Y2i));
            rawYMax = (std::max)(rawYMax, (std::max)(Y1i, Y2i));
            sumY += Y1i + Y2i;
            sumCb += Cbi * 2;
            sumCr += Cri * 2;
            const float Y1 = static_cast<float>(Y1i);
            const float Y2 = static_cast<float>(Y2i);
            const float Cb = static_cast<float>(Cbi);
            const float Cr = static_cast<float>(Cri);
            float* d0 = &out[(static_cast<size_t>(y) * w + x) * 4];
            float* d1 = &out[(static_cast<size_t>(y) * w + x + 1) * 4];
            Yuv8ToRgb01(Y1, Cb, Cr, d0[0], d0[1], d0[2]);
            d0[3] = 1.f;
            Yuv8ToRgb01(Y2, Cb, Cr, d1[0], d1[1], d1[2]);
            d1[3] = 1.f;
            sumR += d0[0] + d1[0];
            sumG += d0[1] + d1[1];
            sumB += d0[2] + d1[2];
            minC = (std::min)(minC, (std::min)(d0[0], (std::min)(d0[1], d0[2])));
            maxC = (std::max)(maxC, (std::max)(d0[0], (std::max)(d0[1], d0[2])));
            minC = (std::min)(minC, (std::min)(d1[0], (std::min)(d1[1], d1[2])));
            maxC = (std::max)(maxC, (std::max)(d1[0], (std::max)(d1[1], d1[2])));
            ipix += macroBytes;
        }
    }
    const double n = static_cast<double>(w) * h;
    EffectLog(
        "yuv raw Y=" + std::to_string(sumY / n) +
        " Cb=" + std::to_string(sumCb / n) +
        " Cr=" + std::to_string(sumCr / n) +
        " Ymin=" + std::to_string(rawYMin) +
        " Ymax=" + std::to_string(rawYMax));
    EffectLog(
        "rgb mean R=" + std::to_string(sumR / n) +
        " G=" + std::to_string(sumG / n) +
        " B=" + std::to_string(sumB / n) +
        " min=" + std::to_string(minC) +
        " max=" + std::to_string(maxC));
    return true;
}

bool WriteFloatRGBAToYuv(IAVXImage* image, IAVXImageFormat* format, const float* rgba)
{
    const acfUInt32 w = image->GetWidth();
    const acfUInt32 h = image->GetHeight();
    const acfSInt32 cb = format->GetChannel(kAVXChannelType_Cb);
    const acfSInt32 y1 = format->GetChannel(kAVXChannelType_Y1);
    const acfSInt32 cr = format->GetChannel(kAVXChannelType_Cr);
    const acfSInt32 y2 = format->GetChannel(kAVXChannelType_Y2);
    const acfUInt32 macroBytes = YuvMacroPixelBytes(format);
    if (cb < 0 || y1 < 0 || cr < 0 || y2 < 0 || macroBytes == 0 || (w % 2) != 0)
        return false;

    for (acfUInt32 y = 0; y < h; ++y) {
        avx2Pixel8* opix = image->GetLinePtr(y, 0);
        if (!opix)
            return false;
        for (acfUInt32 x = 0; x < w; x += 2) {
            const float* s0 = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            const float* s1 = &rgba[(static_cast<size_t>(y) * w + x + 1) * 4];
            float Y0, Cb0, Cr0, Y1v, Cb1, Cr1;
            Rgb01ToYuv8(s0[0], s0[1], s0[2], Y0, Cb0, Cr0);
            Rgb01ToYuv8(s1[0], s1[1], s1[2], Y1v, Cb1, Cr1);
            opix[y1] = static_cast<avx2Pixel8>(Y0 + 0.5f);
            opix[y2] = static_cast<avx2Pixel8>(Y1v + 0.5f);
            opix[cb] = static_cast<avx2Pixel8>((Cb0 + Cb1) * 0.5f + 0.5f);
            opix[cr] = static_cast<avx2Pixel8>((Cr0 + Cr1) * 0.5f + 0.5f);
            opix += macroBytes;
        }
    }
    return true;
}

void FillDebugColor(IAVXImage* image, IAVXImageFormat* format, float r, float g, float b)
{
    if (!image || !format)
        return;
    const acfUInt32 w = image->GetWidth();
    const acfUInt32 h = image->GetHeight();
    if (IsYuvColorSpace(format->GetColorSpace())) {
        std::vector<float> solid(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < solid.size() / 4; ++i) {
            solid[i * 4 + 0] = r;
            solid[i * 4 + 1] = g;
            solid[i * 4 + 2] = b;
            solid[i * 4 + 3] = 1.f;
        }
        WriteFloatRGBAToYuv(image, format, solid.data());
        return;
    }
    const acfUInt32 pixelsz = format->GetPixelSize(0);
    if (pixelsz == 0)
        return;
    for (acfUInt32 y = 0; y < h; ++y) {
        avx2Pixel8* opix = image->GetLinePtr(y, 0);
        if (!opix)
            continue;
        for (acfUInt32 x = 0; x < w; ++x) {
            WritePixelRGBA(format, opix, r, g, b, 1.f);
            opix += pixelsz;
        }
    }
}

}  // namespace

CCorridorKey_Effect::CCorridorKey_Effect(IACFUnknown* pControllingUnknown)
    : CACFUnknown(pControllingUnknown)
{
}

CCorridorKey_Effect::~CCorridorKey_Effect()
{
    InternalAddRef();
    if (m_ipc) {
        m_ipc->Shutdown();
        m_ipc.reset();
    }
    m_host = 0;
}

ACFMETHODIMP CCorridorKey_Effect::InitializeInstance(IACFUnknown* pUnkHost)
{
    BEGIN_ACF_METHOD
    if (!pUnkHost)
        return ACF_E_INVALIDARG;
    m_host = pUnkHost;
    m_ipc = std::make_unique<corridor_key_avx::IpcClient>();
    EffectLog("InitializeInstance");
    END_ACF_METHOD
}

ACFMETHODIMP CCorridorKey_Effect::InternalQueryInterface(const acfIID& riid, void** ppvObj)
{
    if (!ppvObj)
        return ACF_E_INVALIDARG;
    if (riid == IID_IAVXEffect) {
        *ppvObj = static_cast<IAVXEffect*>(this);
        (static_cast<IACFUnknown*>(*ppvObj))->AddRef();
        return ACF_OK;
    }
    return CACFUnknown::InternalQueryInterface(riid, ppvObj);
}

ACFMETHODIMP CCorridorKey_Effect::GetEffectAttribute(
    const acfUID& id, IACFValue* value, IAVXEffectContext*, void*, void*, IAVXFrameContext*, void*)
{
    // Kept for hosts that honor it; MC still blacks FG if a 2nd input is defined,
    // so 1.11 ships single-input only. See AVXATTR_Effect_SupportNoInputData.
    if (id == AVXATTR_Effect_SupportNoInputData) {
        if (!value)
            return ACF_E_POINTER;
        acfBool allowNull = kACFTrue;
        value->Clear();
        return value->InitializeValue(ACFTypeID_Boolean, &allowNull, sizeof(allowNull));
    }
    return ACF_E_NOTIMPL;
}

ACFMETHODIMP CCorridorKey_Effect::InitializeEffect(IAVXEffectContext*)
{
    EffectLog("InitializeEffect");
    return ACF_OK;
}

ACFMETHODIMP CCorridorKey_Effect::BeginEffect(IAVXEffectContext*, void**)
{
    EffectLog("BeginEffect");
    return ACF_OK;
}

ACFMETHODIMP CCorridorKey_Effect::EndEffect(IAVXEffectContext*, void*)
{
    EffectLog("EndEffect");
    return ACF_OK;
}

ACFMETHODIMP CCorridorKey_Effect::BeginFrameRange(
    IAVXEffectContext* effectContext, void*, void**)
{
    EffectLog("BeginFrameRange");
    // Model load happens in Render. Only kick analyze after a successful load.
    if (effectContext && m_ipc && m_modelLoaded) {
        corridor_key_avx::ProcessParams params;
        ReadParams(effectContext, 0.0, params);
        if (params.time_aware != 0) {
            m_ipc->AnalyzeClipStart(params);
        }
    }
    return ACF_OK;
}

ACFMETHODIMP CCorridorKey_Effect::EndFrameRange(IAVXEffectContext*, void*, void*)
{
    return ACF_OK;
}

ACFMETHODIMP CCorridorKey_Effect::BeginFrame(
    IAVXEffectContext*, void*, void*, IAVXFrameContext*, void**)
{
    return ACF_OK;
}

ACFMETHODIMP CCorridorKey_Effect::EndFrame(
    IAVXEffectContext*, void*, void*, IAVXFrameContext*, void*)
{
    return ACF_OK;
}

bool CCorridorKey_Effect::ReadParams(
    IAVXEffectContext* effectContext, acfDouble time, corridor_key_avx::ProcessParams& out)
{
    auto readUInt32 = [&](const acfUID& id, acfUInt32 defVal) -> acfUInt32 {
        ACFPtr<IAVXParameter> p;
        if (ACFFAILED(effectContext->GetParameter(id, IID_PPV_ARG(IAVXParameter, &p))) || !p)
            return defVal;
        acfUInt32 v = defVal;
        if (ACFFAILED(p->GetValue(time, ACFTypeID_UInt32, &v, sizeof(v))))
            return defVal;
        return v;
    };
    auto readBool = [&](const acfUID& id, bool defVal) -> bool {
        ACFPtr<IAVXParameter> p;
        if (ACFFAILED(effectContext->GetParameter(id, IID_PPV_ARG(IAVXParameter, &p))) || !p)
            return defVal;
        acfBool v = defVal ? kACFTrue : kACFFalse;
        if (ACFFAILED(p->GetValue(time, ACFTypeID_Boolean, &v, sizeof(v))))
            return defVal;
        return v != kACFFalse;
    };

    out.mode = static_cast<int>(readUInt32(PARAMETER_Mode_GUID, 0));
    out.output_mode = static_cast<int>(readUInt32(PARAMETER_Output_GUID, 0));
    // Despill / Hint Quality are 0–100 integer UI → 0..1 for the model
    out.despill_strength = static_cast<float>(readUInt32(PARAMETER_Despill_GUID, 50)) / 100.f;
    out.input_colorspace = static_cast<int>(readUInt32(PARAMETER_Colorspace_GUID, 0));
    out.time_aware = readBool(PARAMETER_TimeAware_GUID, false) ? 1 : 0;
    acfUInt32 tw = readUInt32(PARAMETER_TemporalWindow_GUID, 16);
    if (tw < 1) tw = 1;
    if (tw > 80) tw = 80;
    out.temporal_window = static_cast<int>(tw);
    out.hint_quality = static_cast<float>(readUInt32(PARAMETER_HintQuality_GUID, 95)) / 100.f;
    out.carry_hint = readBool(PARAMETER_CarryHint_GUID, false);
    return true;
}

void CCorridorKey_Effect::PassThrough(IAVXImage* input, IAVXImage* output)
{
    const acfUInt32 bytes = (std::min)(input->GetRawDataSize(), output->GetRawDataSize());
    void* src = input->GetRawDataPtr();
    void* dst = output->GetRawDataPtr();
    if (src && dst && bytes)
        memcpy(dst, src, bytes);
}

bool CCorridorKey_Effect::ConvertInputToFloatRGBA(
    IAVXImage* image, IAVXImageFormat* format, std::vector<float>& out)
{
    if (!image || !format)
        return false;

    const acfUInt32 w = image->GetWidth();
    const acfUInt32 h = image->GetHeight();
    if (w == 0 || h == 0)
        return false;

    if (IsYuvColorSpace(format->GetColorSpace()))
        return ConvertYuvInputToFloatRGBA(image, format, out);

    const acfSInt32 r = format->GetChannel(kAVXChannelType_Red);
    const acfSInt32 g = format->GetChannel(kAVXChannelType_Green);
    const acfSInt32 b = format->GetChannel(kAVXChannelType_Blue);
    const acfSInt32 a = format->GetChannel(kAVXChannelType_Alpha);
    const acfUInt32 pixelsz = format->GetPixelSize(0);
    const avx2ComponentType ct = format->GetComponentType();

    if (r < 0 || g < 0 || b < 0 || pixelsz == 0) {
        EffectLog(
            "Convert fail: channels r=" + std::to_string(r) +
            " g=" + std::to_string(g) + " b=" + std::to_string(b) +
            " a=" + std::to_string(a) + " pixsz=" + std::to_string(pixelsz) +
            " ct=" + std::to_string(static_cast<int>(ct)) +
            " cs=" + std::to_string(static_cast<int>(format->GetColorSpace())) +
            " nch=" + std::to_string(format->GetNumChannels()));
        return false;
    }

    EffectLog(
        "format ct=" + std::to_string(static_cast<int>(ct)) +
        " pixsz=" + std::to_string(pixelsz) +
        " cs=" + std::to_string(static_cast<int>(format->GetColorSpace())) +
        " order=" + std::to_string(static_cast<int>(format->GetComponentOrder())) +
        " chR=" + std::to_string(r) + " G=" + std::to_string(g) +
        " B=" + std::to_string(b) + " A=" + std::to_string(a));

    out.resize(static_cast<size_t>(w) * h * 4);
    double sumR = 0, sumG = 0, sumB = 0;
    float minC = 1.f, maxC = 0.f;
    for (acfUInt32 y = 0; y < h; ++y) {
        avx2Pixel8* ipix = image->GetLinePtr(y, 0);
        if (!ipix) {
            EffectLog("Convert fail: GetLinePtr null y=" + std::to_string(y));
            return false;
        }
        for (acfUInt32 x = 0; x < w; ++x) {
            float* d = &out[(static_cast<size_t>(y) * w + x) * 4];
            if (!ReadPixelRGBA(format, ipix, d[0], d[1], d[2], d[3]))
                return false;
            sumR += d[0]; sumG += d[1]; sumB += d[2];
            minC = (std::min)(minC, (std::min)(d[0], (std::min)(d[1], d[2])));
            maxC = (std::max)(maxC, (std::max)(d[0], (std::max)(d[1], d[2])));
            ipix += pixelsz;
        }
    }
    const double n = static_cast<double>(w) * h;
    EffectLog(
        "rgb mean R=" + std::to_string(sumR / n) +
        " G=" + std::to_string(sumG / n) +
        " B=" + std::to_string(sumB / n) +
        " min=" + std::to_string(minC) +
        " max=" + std::to_string(maxC));
    return true;
}

bool CCorridorKey_Effect::ConvertHintToFloatRGBA(
    IAVXImage* image, IAVXImageFormat* format, std::vector<float>& out)
{
    if (!ConvertInputToFloatRGBA(image, format, out))
        return false;
    const size_t n = out.size() / 4;
    for (size_t i = 0; i < n; ++i) {
        float alpha = out[i * 4 + 3];
        float y = 0.2126f * out[i * 4 + 0] + 0.7152f * out[i * 4 + 1] + 0.0722f * out[i * 4 + 2];
        float hint = (alpha > 0.01f) ? alpha : y;
        out[i * 4 + 0] = hint;
        out[i * 4 + 1] = hint;
        out[i * 4 + 2] = hint;
        out[i * 4 + 3] = 1.0f;
    }
    return true;
}

bool CCorridorKey_Effect::WriteFloatRGBAToOutput(
    IAVXImage* image, IAVXImageFormat* format, const float* rgba)
{
    if (IsYuvColorSpace(format->GetColorSpace()))
        return WriteFloatRGBAToYuv(image, format, rgba);

    const acfUInt32 w = image->GetWidth();
    const acfUInt32 h = image->GetHeight();
    const acfSInt32 r = format->GetChannel(kAVXChannelType_Red);
    const acfSInt32 g = format->GetChannel(kAVXChannelType_Green);
    const acfSInt32 b = format->GetChannel(kAVXChannelType_Blue);
    const acfUInt32 pixelsz = format->GetPixelSize(0);
    if (r < 0 || g < 0 || b < 0 || pixelsz == 0)
        return false;

    // Precomposed RGB + opaque alpha so MC does not composite the key to black.
    for (acfUInt32 y = 0; y < h; ++y) {
        avx2Pixel8* opix = image->GetLinePtr(y, 0);
        if (!opix)
            return false;
        for (acfUInt32 x = 0; x < w; ++x) {
            const float* s = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            if (!WritePixelRGBA(format, opix, s[0], s[1], s[2], 1.f))
                return false;
            opix += pixelsz;
        }
    }
    return true;
}

ACFMETHODIMP CCorridorKey_Effect::Render(
    IAVXEffectContext* effectContext, void*, void*,
    IAVXFrameContext* frameContext, void*,
    IAVXRenderContext* renderContext,
    IAVXOutput* output)
{
    BEGIN_ACF_METHOD

    ACFASSERT(effectContext && frameContext && renderContext && output);
    EffectLog("Render enter");

    ACFPtr<IAVXImage> outputImage;
    acfcheck(output->GetOutput(0, AVXOutputTypeID_Image, IID_PPV_ARG(IAVXImage, &outputImage)));
    avx2FieldMode fieldMode = outputImage->GetFieldMode();

    ACFPtr<IAVXImageFormat> pOutFormat;
    acfcheck(outputImage->GetFormat(&pOutFormat));

    acfSInt32 frame = frameContext->GetFrameNumber();
    avx2Field field = renderContext->GetField();
    acfDouble time = renderContext->GetTime();

    ACFPtr<IAVXImageInput> layer;
    acfcheck(effectContext->GetInput(0, AVXInputTypeID_Image, IID_PPV_ARG(IAVXImageInput, &layer)));

    auto imageLooksEmpty = [](IAVXImage* img, IAVXImageFormat* fmt) -> bool {
        if (!img || !fmt)
            return true;
        // Sample a few pixels: flat video-black (Y/R≈16) or all zeros.
        const acfUInt32 w = img->GetWidth();
        const acfUInt32 h = img->GetHeight();
        if (w == 0 || h == 0)
            return true;
        avx2Pixel8* p = img->GetLinePtr(h / 2, 0);
        if (!p)
            return true;
        if (IsYuvColorSpace(fmt->GetColorSpace())) {
            const acfSInt32 y1 = fmt->GetChannel(kAVXChannelType_Y1);
            if (y1 < 0)
                return false;
            const int y = static_cast<int>(p[y1]);
            return y <= 16;
        }
        const acfSInt32 r = fmt->GetChannel(kAVXChannelType_Red);
        if (r < 0)
            return false;
        return p[r] <= 16;
    };

    ACFPtr<IAVXImage> inputImage;
    {
        ACFRESULT gi = layer->GetImage(static_cast<acfUInt32>(frame), field, fieldMode, &inputImage);
        if (gi == ACF_E_DATANOTAVAILABLE)
            return gi;
        acfcheck(gi);
    }

    // Prefer full-rez only when it actually has media (FullRez can be a black filler).
    {
        ACFPtr<IAVXImageInput_v2> layerV2;
        if (ACFSUCCEEDED(layer->QueryInterface(IID_PPV_ARG(IAVXImageInput_v2, &layerV2))) && layerV2) {
            ACFPtr<IAVXImage> fullImage;
            ACFRESULT gi2 = layerV2->GetImageFullRez(static_cast<acfUInt32>(frame), field, fieldMode, &fullImage);
            if (ACFSUCCEEDED(gi2) && fullImage
                && fullImage->GetWidth() == outputImage->GetWidth()
                && fullImage->GetHeight() == outputImage->GetHeight()) {
                ACFPtr<IAVXImageFormat> fullFmt;
                if (ACFSUCCEEDED(fullImage->GetFormat(&fullFmt)) && fullFmt
                    && !imageLooksEmpty(fullImage, fullFmt)) {
                    EffectLog("using GetImageFullRez");
                    inputImage = fullImage;
                } else {
                    EffectLog("GetImageFullRez empty — keeping GetImage");
                }
            } else if (gi2 == ACF_E_DATANOTAVAILABLE) {
                // Don't abort if we already have usable GetImage data.
                ACFPtr<IAVXImageFormat> tmpFmt;
                if (ACFSUCCEEDED(inputImage->GetFormat(&tmpFmt)) && imageLooksEmpty(inputImage, tmpFmt))
                    return gi2;
                EffectLog("GetImageFullRez DATANOTAVAILABLE — keeping GetImage");
            }
        }
    }

    ACFPtr<IAVXImageFormat> pInFormat;
    acfcheck(inputImage->GetFormat(&pInFormat));

    corridor_key_avx::ProcessParams params;
    ReadParams(effectContext, time, params);
    params.width = outputImage->GetWidth();
    params.height = outputImage->GetHeight();
    params.frame_number = frame;
    params.has_alpha_hint = false;

    EffectLog(
        "frame=" + std::to_string(frame) +
        " size=" + std::to_string(params.width) + "x" + std::to_string(params.height) +
        " mode=" + std::to_string(params.mode) +
        " outMode=" + std::to_string(params.output_mode) +
        " timeAware=" + std::to_string(params.time_aware) +
        " tw=" + std::to_string(params.temporal_window) +
        " despill=" + std::to_string(params.despill_strength) +
        " fieldMode=" + std::to_string(static_cast<int>(fieldMode)) +
        " field=" + std::to_string(static_cast<int>(field)) +
        " inW=" + std::to_string(inputImage->GetWidth()) +
        " inH=" + std::to_string(inputImage->GetHeight()) +
        " rawBytes=" + std::to_string(inputImage->GetRawDataSize()) +
        " bwLevel=" + std::to_string(static_cast<int>(pInFormat->GetBlackWhiteLevel())) +
        " numIn=" + std::to_string(effectContext->GetNumInputs()));

    std::vector<float> inputRgba;
    std::vector<float> hintRgba;
    std::vector<float> outputRgba(static_cast<size_t>(params.width) * params.height * 4);

    bool ok = ConvertInputToFloatRGBA(inputImage, pInFormat, inputRgba);
    if (!ok) {
        EffectLog("ConvertInput failed — passthrough");
        PassThrough(inputImage, outputImage);
        return ACF_OK;
    }

    // Alpha Hint without a 2nd effect input (2nd input = MC black FG). When Mode is
    // External Hint, use this clip's own alpha as the guide matte — only if alpha
    // actually varies (not flat opaque). Otherwise stay Auto and pass no hint.
    params.has_alpha_hint = false;
    if (params.mode == 1) {
        const size_t nPix = inputRgba.size() / 4;
        float aMin = 1.f, aMax = 0.f;
        hintRgba.resize(inputRgba.size());
        for (size_t i = 0; i < nPix; ++i) {
            const float a = inputRgba[i * 4 + 3];
            aMin = (std::min)(aMin, a);
            aMax = (std::max)(aMax, a);
            hintRgba[i * 4 + 0] = a;
            hintRgba[i * 4 + 1] = a;
            hintRgba[i * 4 + 2] = a;
            hintRgba[i * 4 + 3] = 1.f;
        }
        if (aMax - aMin > 0.02f) {
            params.has_alpha_hint = true;
            EffectLog("External Hint from FG alpha range=" +
                      std::to_string(aMin) + ".." + std::to_string(aMax));
        } else {
            EffectLog("External Hint but FG alpha flat — Auto (no hint passed)");
            params.mode = 0;
            hintRgba.clear();
        }
    } else {
        hintRgba.clear();
    }

    // Host sometimes returns a flat video-black placeholder. Ask it to retry.
    {
        float mn = 1.f, mx = 0.f;
        double sum = 0.0;
        const size_t nPix = inputRgba.size() / 4;
        for (size_t i = 0; i < nPix; ++i) {
            const float v0 = inputRgba[i * 4 + 0];
            const float v1 = inputRgba[i * 4 + 1];
            const float v2 = inputRgba[i * 4 + 2];
            sum += v0 + v1 + v2;
            mn = (std::min)(mn, (std::min)(v0, (std::min)(v1, v2)));
            mx = (std::max)(mx, (std::max)(v0, (std::max)(v1, v2)));
        }
        const double mean = nPix ? sum / static_cast<double>(nPix * 3) : 0.0;
        EffectLog("input rgb mean=" + std::to_string(mean) + " min=" + std::to_string(mn) + " max=" + std::to_string(mx));
        const bool flatEmpty = (mx - mn < 1e-4f) && (mean < 0.08);
        if (flatEmpty) {
            if (m_emptyInputRetries < 3) {
                ++m_emptyInputRetries;
                EffectLog("empty/flat placeholder — DATANOTAVAILABLE retry "
                          + std::to_string(m_emptyInputRetries));
                return ACF_E_DATANOTAVAILABLE;
            }
            EffectLog("empty input after retries — passthrough");
            PassThrough(inputImage, outputImage);
            return ACF_OK;
        }
        m_emptyInputRetries = 0;
    }

    if (!m_ipc) {
        EffectLog("m_ipc null — debug RED");
        FillDebugColor(outputImage, pOutFormat, 1.f, 0.f, 0.f);
        return ACF_OK;
    }

    if (!m_modelLoaded) {
        EffectLog("LoadModel starting (may take up to ~60s)...");
        m_modelLoaded = m_ipc->LoadModel();
        EffectLog(std::string("LoadModel=") + (m_modelLoaded ? "ok" : ("FAIL " + m_ipc->LastError())));
    }

    // ProcessFrame expects width*height floats matching the converted buffer.
    const acfUInt32 inW = inputImage->GetWidth();
    const acfUInt32 inH = inputImage->GetHeight();
    if (inW != params.width || inH != params.height) {
        EffectLog(
            "size mismatch in=" + std::to_string(inW) + "x" + std::to_string(inH) +
            " out=" + std::to_string(params.width) + "x" + std::to_string(params.height) +
            " — using input size for inference");
        params.width = inW;
        params.height = inH;
        outputRgba.assign(static_cast<size_t>(inW) * inH * 4, 0.f);
    }

    const float* hintPtr = (params.has_alpha_hint && !hintRgba.empty()) ? hintRgba.data() : nullptr;

    if (m_modelLoaded) {
        if (params.time_aware != 0) {
            if (!m_ipc->GetCachedFrame(frame, params.width, params.height, outputRgba.data())) {
                ok = m_ipc->ProcessFrame(params, inputRgba.data(), hintPtr, outputRgba.data());
            } else {
                ok = true;
            }
        } else {
            ok = m_ipc->ProcessFrame(params, inputRgba.data(), hintPtr, outputRgba.data());
        }
        if (!ok) {
            EffectLog("ProcessFrame FAIL: " + m_ipc->LastError());
        }
    } else {
        ok = false;
    }

    if (ok) {
        {
            double sum = 0.0;
            const size_t nPix = static_cast<size_t>(params.width) * params.height;
            for (size_t i = 0; i < nPix; ++i) {
                sum += outputRgba[i * 4 + 0] + outputRgba[i * 4 + 1] + outputRgba[i * 4 + 2];
            }
            const double meanRgb = nPix ? sum / static_cast<double>(nPix * 3) : 0.0;
            EffectLog("output rgb mean=" + std::to_string(meanRgb));
            // If the model returned an empty/black frame, show the source so we
            // can tell IPC worked vs. the viewer eating transparent pixels.
            if (meanRgb < 0.001) {
                EffectLog("output near-black — showing input passthrough");
                PassThrough(inputImage, outputImage);
                return ACF_OK;
            }
        }
        if (!WriteFloatRGBAToOutput(outputImage, pOutFormat, outputRgba.data())) {
            EffectLog("WriteFloatRGBA failed — debug YELLOW");
            FillDebugColor(outputImage, pOutFormat, 1.f, 1.f, 0.f);
        } else {
            EffectLog("Render OK");
        }
    } else if (m_passthroughOnError) {
        EffectLog("passthrough (error path)");
        PassThrough(inputImage, outputImage);
    }

    END_ACF_METHOD
}
