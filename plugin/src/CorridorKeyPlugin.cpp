#define INITACFIDS

#include "avx2api.h"
#include "CACFClassFactory.h"
#include "ACFPtr.h"
#include "acfextras.h"

#include "CorridorKeyEffect.h"
#include "CorridorKeyUIDs.h"

static const acfUInt32 kEffectMajorVersion = 2;
static const acfUInt32 kEffectMinorVersion = 0;

static const acfWChar* sPluginVendor = L"CorridorKey Community";
static const acfWChar* sPluginName = L"CorridorKey.avx";
static const acfWChar* sPluginDescription =
    L"Corridor Key 2.0 neural green/blue screen keyer for Avid Media Composer. "
    L"One-click keying, Time Awareness, Despill/Unmix, Alpha Hint, multi-output.";
static const acfWChar* sComponentName = L"Corridor Key 2.0";
// Image = segment filter (same as FlipFlop/Fade). Key category made MC wire inputs like a keyer.
static const acfWChar* sCategoryName = L"Image";

static const acfUInt32 kTotalPluginComponents = 1;

static void AddUInt32Slider(
    IACFComponentFactory* pFactory,
    IAVXEffectDefinition* pEffectDef,
    const acfUID& paramGuid,
    const acfUID& layoutGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfUInt32 defVal,
    acfUInt32 minVal,
    acfUInt32 maxVal)
{
    ACFPtr<IAVXParameterDefinition> pParam;
    acfcheck(pFactory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                       IID_IAVXParameterDefinition, (void**)&pParam));
    if (!pParam)
        return;
    acfcheck(pParam->InitializeParameterDefinition(
        paramGuid, name, desc, ACFTypeID_UInt32, kAVXKeyFrameBehavior_Static));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_UInt32, &defVal, sizeof(defVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Minimum, ACFTypeID_UInt32, &minVal, sizeof(minVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Maximum, ACFTypeID_UInt32, &maxVal, sizeof(maxVal)));
    acfcheck(pEffectDef->AddParameter(pParam));

    acfUID viewId = ACF_WIDGET_SLIDER;
    ACFPtr<IAVXLayoutDefinition> pLayout;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pLayout));
    if (!pLayout)
        return;
    acfcheck(pLayout->InitializeLayoutDefinition(layoutGuid, name, desc, &viewId, pParam));
    acfcheck(pEffectDef->AddLayoutDefinition(pLayout));
}

static void AddCheckbox(
    IACFComponentFactory* pFactory,
    IAVXEffectDefinition* pEffectDef,
    const acfUID& paramGuid,
    const acfUID& layoutGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfBool defVal)
{
    ACFPtr<IAVXParameterDefinition> pParam;
    acfcheck(pFactory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                       IID_IAVXParameterDefinition, (void**)&pParam));
    if (!pParam)
        return;
    acfcheck(pParam->InitializeParameterDefinition(
        paramGuid, name, desc, ACFTypeID_Boolean, kAVXKeyFrameBehavior_Static));
    acfBool minVal = kACFFalse;
    acfBool maxVal = kACFTrue;
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_Boolean, &defVal, sizeof(defVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Minimum, ACFTypeID_Boolean, &minVal, sizeof(minVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Maximum, ACFTypeID_Boolean, &maxVal, sizeof(maxVal)));
    acfcheck(pEffectDef->AddParameter(pParam));

    acfUID viewId = ACF_WIDGET_CHECKBOX;
    ACFPtr<IAVXLayoutDefinition> pLayout;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pLayout));
    if (!pLayout)
        return;
    acfcheck(pLayout->InitializeLayoutDefinition(layoutGuid, name, desc, &viewId, pParam));
    acfcheck(pEffectDef->AddLayoutDefinition(pLayout));
}

static ACFPtr<IAVXLayoutDefinition> MakeListItem(
    IACFComponentFactory* pFactory,
    const acfUID& itemGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfUInt32 value)
{
    ACFPtr<IAVXLayoutDefinition> pItem;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pItem));
    if (!pItem)
        return pItem;
    acfUID viewId = ACF_WIDGET_LISTITEM;
    acfcheck(pItem->InitializeLayoutDefinition(itemGuid, name, desc, &viewId));
    acfcheck(pItem->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_UInt32, &value, sizeof(value)));
    return pItem;
}

static void AddListBox(
    IACFComponentFactory* pFactory,
    IAVXEffectDefinition* pEffectDef,
    const acfUID& paramGuid,
    const acfUID& layoutGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfUInt32 defVal,
    const acfUID* itemGuids,
    const acfWChar** itemNames,
    const acfUInt32* itemValues,
    acfUInt32 itemCount)
{
    ACFPtr<IAVXParameterDefinition> pParam;
    acfcheck(pFactory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                       IID_IAVXParameterDefinition, (void**)&pParam));
    if (!pParam)
        return;
    acfcheck(pParam->InitializeParameterDefinition(
        paramGuid, name, desc, ACFTypeID_UInt32, kAVXKeyFrameBehavior_Static));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_UInt32, &defVal, sizeof(defVal)));
    acfcheck(pEffectDef->AddParameter(pParam));

    acfUID viewId = ACF_WIDGET_LISTBOX;
    ACFPtr<IAVXLayoutDefinition> pLayout;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pLayout));
    if (!pLayout)
        return;
    acfcheck(pLayout->InitializeLayoutDefinition(layoutGuid, name, desc, &viewId, pParam));

    for (acfUInt32 i = 0; i < itemCount; ++i) {
        ACFPtr<IAVXLayoutDefinition> pItem =
            MakeListItem(pFactory, itemGuids[i], itemNames[i], itemNames[i], itemValues[i]);
        if (pItem)
            acfcheck(pLayout->AddLayoutDefinition(pItem));
    }

    acfcheck(pEffectDef->AddLayoutDefinition(pLayout));
}

extern "C" ACFAPI ACFRegisterPlugin(IACFUnknown* pUnkHost, IACFPluginDefinition** ppPluginDefinition)
{
    BEGIN_ACF_METHOD
    if (!pUnkHost || !ppPluginDefinition)
        return ACF_E_POINTER;
    *ppPluginDefinition = 0;

    ACFPtr<IACFComponentFactory> pFactory;
    acfcheck(pUnkHost->QueryInterface(IID_IACFComponentFactory, (void**)&pFactory));

    ACFPtr<IACFPluginDefinition> pPluginDef;
    acfcheck(pFactory->CreateComponent(ACFCompID_PluginDefinition, 0, IID_IACFPluginDefinition, (void**)&pPluginDef));
    acfcheck(pPluginDef->InitializePlugin(
        MID_CorridorKey_Plugin,
        kEffectMajorVersion,
        kEffectMinorVersion,
        sPluginName,
        AVXVendorID_CorridorKey,
        sPluginVendor,
        kTotalPluginComponents,
        kACFTrue));

    acfcheck(pPluginDef->DefineAttribute(
        ACFATTR_Definition_Description,
        ACFTypeID_UnicodeString,
        sPluginDescription,
        ACF_SIZE_UNK));

    *ppPluginDefinition = pPluginDef.detach();
    END_ACF_METHOD
}

extern "C" ACFAPI ACFRegisterComponent(
    IACFUnknown* pUnkHost,
    acfUInt32 index,
    IACFComponentDefinition** ppComponentDefinition)
{
    BEGIN_ACF_METHOD

    if (!pUnkHost || !ppComponentDefinition)
        return ACF_E_POINTER;
    if (kTotalPluginComponents <= index)
        return ACF_E_INVALIDARG;

    *ppComponentDefinition = 0;

    ACFPtr<IACFComponentFactory> pFactory;
    acfcheck(pUnkHost->QueryInterface(IID_IACFComponentFactory, (void**)&pFactory));

    ACFPtr<IAVXEffectDefinition> pEffectDef;
    acfcheck(pFactory->CreateComponent(ACFCompID_EffectDefinition, 0, IID_IAVXEffectDefinition, (void**)&pEffectDef));
    acfcheck(pEffectDef->InitializeComponent(
        EID_CorridorKey_Effect,
        ACFCompType_ImageEffect,
        kEffectMajorVersion,
        kEffectMinorVersion,
        CLSID_CorridorKey_Effect,
        sComponentName));

    acfcheck(pEffectDef->DefineAttribute(ACFATTR_Effect_Category, ACFTypeID_UnicodeString, sCategoryName, ACF_SIZE_UNK));

    // ONE input only. Empirically: AddOptionalInput / AddInput for a 2nd layer makes
    // Media Composer deliver flat video-black (Y=16) on the primary for single-clip
    // applies — SupportNoInputData does not prevent that. Alpha Hint is Mode=External
    // using the clip's own alpha (nest a matte into alpha, or use an RGBA source).
    acfcheck(pEffectDef->AddInput(ACFLayerID_Input, L"Input", kAVXDataType_Color));
    acfcheck(pEffectDef->AddOutput(ACFLayerID_Output, L"Output", kAVXDataType_Color));

    // Formats Media Composer commonly delivers (RGB + YUV, 8/10/16-bit).
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_RGBA_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGR_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_RGB_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_SHORT_2_14));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_SHORT_2_14));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_USHORT));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_USHORT));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_CbYCrY_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_YCbYCr_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_CbYCrY_NoA, kAVXComponentType_SHORT_2_14));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_CbYCrY_NoA, kAVXComponentType_USHORT_10_6));

    // Mode dropdown
    {
        static const acfUID guids[] = { LISTITEM_Mode_Auto, LISTITEM_Mode_External };
        static const acfWChar* names[] = { L"Auto", L"External Hint (FG alpha)" };
        static const acfUInt32 values[] = { 0, 1 };
        AddListBox(pFactory, pEffectDef, PARAMETER_Mode_GUID, LAYOUT_Mode_GUID,
                   L"Mode",
                   L"Auto = one-click key. External = use this clip's alpha channel as Alpha Hint.",
                   0, guids, names, values, 2);
    }

    // Output dropdown
    {
        static const acfUID guids[] = {
            LISTITEM_Out_Processed, LISTITEM_Out_Matte,
            LISTITEM_Out_Foreground, LISTITEM_Out_Checker
        };
        static const acfWChar* names[] = {
            L"Processed", L"Matte", L"Foreground", L"Checkerboard"
        };
        static const acfUInt32 values[] = { 0, 1, 2, 3 };
        AddListBox(pFactory, pEffectDef, PARAMETER_Output_GUID, LAYOUT_Output_GUID,
                   L"Output", L"Processed / Matte / Foreground / Checkerboard",
                   0, guids, names, values, 4);
    }

    // Despill / Unmix strength (0–100%)
    AddUInt32Slider(pFactory, pEffectDef, PARAMETER_Despill_GUID, LAYOUT_Despill_GUID,
                    L"Despill / Unmix", L"Spill removal / unmix strength (0-100%)",
                    50, 0, 100);

    // Colorspace dropdown
    {
        static const acfUID guids[] = { LISTITEM_CS_sRGB, LISTITEM_CS_Linear };
        static const acfWChar* names[] = { L"sRGB", L"Linear" };
        static const acfUInt32 values[] = { 0, 1 };
        AddListBox(pFactory, pEffectDef, PARAMETER_Colorspace_GUID, LAYOUT_Colorspace_GUID,
                   L"Colorspace", L"Input colorspace for the model",
                   0, guids, names, values, 2);
    }

    // Time Awareness — analyzes the clip for temporal consistency (official CK feature)
    AddCheckbox(pFactory, pEffectDef, PARAMETER_TimeAware_GUID, LAYOUT_TimeAware_GUID,
                L"Time Awareness",
                L"Analyze the clip for temporal consistency (eliminates flicker)",
                kACFFalse);

    // Temporal Window — hann_chunk when Time Awareness is on (1–80 frames)
    AddUInt32Slider(pFactory, pEffectDef, PARAMETER_TemporalWindow_GUID, LAYOUT_TemporalWindow_GUID,
                    L"Temporal Window",
                    L"Frames per temporal window when Time Awareness is on (1-80)",
                    16, 1, 80);

    // Hint Quality (0–100%)
    AddUInt32Slider(pFactory, pEffectDef, PARAMETER_HintQuality_GUID, LAYOUT_HintQuality_GUID,
                    L"Hint Quality", L"How strongly to trust the Alpha Hint (0-100%)",
                    95, 0, 100);

    AddCheckbox(pFactory, pEffectDef, PARAMETER_CarryHint_GUID, LAYOUT_CarryHint_GUID,
                L"Carry Hint Alpha",
                L"Carry hint alpha across temporal windows",
                kACFFalse);

    // Do NOT claim realtime — MC may skip filling media and pass video-black buffers.
    // Effect still runs on scrub/render; Expert Render caches the result.
    acfBool bReal = kACFFalse;
    acfcheck(pEffectDef->DefineAttribute(AVXATTR_Effect_Realtime, ACFTypeID_Boolean, &bReal, sizeof(acfBool)));

    // IPC worker is not re-entrant across host render threads.
    acfBool bSingle = kACFTrue;
    acfcheck(pEffectDef->DefineAttribute(AVXATTR_Effect_RenderSingleThreaded, ACFTypeID_Boolean, &bSingle, sizeof(acfBool)));

    *ppComponentDefinition = pEffectDef.detach();

    END_ACF_METHOD
}

ACFAPI ACFGetClassFactory(
    IACFUnknown* pUnkHost,
    const acfCLSID& clsid,
    const acfIID& iid,
    void** ppOut)
{
    if (!pUnkHost || !ppOut)
        return ACF_E_INVALIDARG;

    ACFRESULT result = ACF_CLASS_E_CLASSNOTAVAILABLE;
    ACFCreateObjectProc createProc = 0;

    if (CLSID_CorridorKey_Effect == clsid) {
        createProc = CCorridorKey_Effect::ACFCreate;
    }

    if (createProc) {
        ACFPtr<IACFUnknown> pFactoryUnknown;
        result = CACFClassFactory::Create(createProc, IID_IACFUnknown, (void**)&pFactoryUnknown);
        if (ACFSUCCEEDED(result) && pFactoryUnknown)
            result = pFactoryUnknown->QueryInterface(iid, ppOut);
    }

    return result;
}

ACFAPI ACFCanUnloadNow(IACFUnknown* pUnkHost)
{
    if (!pUnkHost)
        return ACF_E_INVALIDARG;
    return CACFUnknown::GetActiveObjectCount() == 0 ? ACF_OK : ACF_E_FAIL;
}
