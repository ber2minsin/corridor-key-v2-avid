#ifndef __CorridorKeyEffect_h__
#define __CorridorKeyEffect_h__

#include "avx2api.h"
#include "CACFUnknown.h"
#include "ACFPtr.h"
#include "IpcClient.h"

#include <memory>
#include <vector>

class CCorridorKey_Effect :
    public IAVXEffect,
    public CACFUnknown
{
protected:
    CCorridorKey_Effect(IACFUnknown*);
    CCorridorKey_Effect() {}
    CCorridorKey_Effect(const CCorridorKey_Effect&) {}
    virtual ~CCorridorKey_Effect();

    ACFMETHODIMP InitializeInstance(IACFUnknown* pUnkHost);
    ACFMETHODIMP InternalQueryInterface(const acfIID& riid, void** ppv);

public:
    ACFMETHODIMP GetEffectAttribute(
        const acfUID& id,
        IACFValue* value,
        IAVXEffectContext* effectContext = NULL, void* effectUserData = NULL,
        void* frameRangeUserData = NULL,
        IAVXFrameContext* frameContext = NULL, void* frameUserData = NULL);

    ACFMETHODIMP InitializeEffect(IAVXEffectContext* effectContext);
    ACFMETHODIMP BeginEffect(IAVXEffectContext* effectContext, void** effectUserData);
    ACFMETHODIMP BeginFrameRange(IAVXEffectContext* effectContext, void* effectUserData,
                                 void** frameRangeUserData);
    ACFMETHODIMP BeginFrame(IAVXEffectContext* effectContext, void* effectUserData,
                            void* frameRangeUserData,
                            IAVXFrameContext* frameContext, void** frameUserData);
    ACFMETHODIMP Render(IAVXEffectContext* effectContext, void* effectUserData,
                        void* frameRangeUserData,
                        IAVXFrameContext* frameContext, void* frameUserData,
                        IAVXRenderContext* renderContext,
                        IAVXOutput* output);
    ACFMETHODIMP EndFrame(IAVXEffectContext* effectContext, void* effectUserData,
                          void* frameRangeUserData,
                          IAVXFrameContext* frameContext, void* frameUserData);
    ACFMETHODIMP EndFrameRange(IAVXEffectContext* effectContext, void* effectUserData,
                               void* frameRangeUserData);
    ACFMETHODIMP EndEffect(IAVXEffectContext* effectContext, void* effectUserData);

private:
    bool ReadParams(IAVXEffectContext* effectContext, acfDouble time,
                    corridor_key_avx::ProcessParams& out);
    bool ConvertInputToFloatRGBA(IAVXImage* image, IAVXImageFormat* format,
                                 std::vector<float>& out);
    bool ConvertHintToFloatRGBA(IAVXImage* image, IAVXImageFormat* format,
                                std::vector<float>& out);
    bool WriteFloatRGBAToOutput(IAVXImage* image, IAVXImageFormat* format,
                                const float* rgba);
    void PassThrough(IAVXImage* input, IAVXImage* output);

    ACFPtr<IACFUnknown> m_host;
    std::unique_ptr<corridor_key_avx::IpcClient> m_ipc;
    bool m_modelLoaded = false;
    bool m_passthroughOnError = true;
    int m_emptyInputRetries = 0;

public:
    ACF_DECLARE_CONCRETE();
};

#endif
