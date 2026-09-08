/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>

#include <stdio.h>

static UINT32 enumerate_transforms(const char *label,
                                   const GUID *category,
                                   const GUID *input_subtype,
                                   const GUID *output_subtype,
                                   UINT32 flags)
{
    MFT_REGISTER_TYPE_INFO input = {MFMediaType_Video, *input_subtype};
    MFT_REGISTER_TYPE_INFO output = {MFMediaType_Video, *output_subtype};
    IMFActivate **activates = NULL;
    UINT32 count = 0;
    HRESULT result = MFTEnumEx(*category,
                               flags | MFT_ENUM_FLAG_SORTANDFILTER,
                               &input,
                               &output,
                               &activates,
                               &count);
    if (FAILED(result)) {
        printf("%s error=0x%08lx\n", label, (unsigned long)result);
        return 0;
    }
    printf("%s count=%u\n", label, (unsigned)count);
    for (UINT32 index = 0; index < count; index++) {
        WCHAR *name = NULL;
        UINT32 name_length = 0;
        if (SUCCEEDED(IMFActivate_GetAllocatedString(activates[index],
                                                      &MFT_FRIENDLY_NAME_Attribute,
                                                      &name,
                                                      &name_length))) {
            printf("  [%u] %ls\n", (unsigned)index, name);
            CoTaskMemFree(name);
        }
        IMFActivate_Release(activates[index]);
    }
    CoTaskMemFree(activates);
    return count;
}

int main(void)
{
    HRESULT result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int should_uninitialize = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        printf("COM startup failed error=0x%08lx\n", (unsigned long)result);
        return 1;
    }
    result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(result)) {
        printf("Media Foundation startup failed error=0x%08lx\n", (unsigned long)result);
        if (should_uninitialize) CoUninitialize();
        return 1;
    }

    UINT32 hardware_encoders = enumerate_transforms(
        "H264_ENCODER_HARDWARE",
        &MFT_CATEGORY_VIDEO_ENCODER,
        &MFVideoFormat_NV12,
        &MFVideoFormat_H264,
        MFT_ENUM_FLAG_HARDWARE);
    UINT32 software_encoders = enumerate_transforms(
        "H264_ENCODER_SOFTWARE",
        &MFT_CATEGORY_VIDEO_ENCODER,
        &MFVideoFormat_NV12,
        &MFVideoFormat_H264,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT);
    UINT32 hardware_decoders = enumerate_transforms(
        "H264_DECODER_HARDWARE",
        &MFT_CATEGORY_VIDEO_DECODER,
        &MFVideoFormat_H264,
        &MFVideoFormat_NV12,
        MFT_ENUM_FLAG_HARDWARE);
    UINT32 software_decoders = enumerate_transforms(
        "H264_DECODER_SOFTWARE",
        &MFT_CATEGORY_VIDEO_DECODER,
        &MFVideoFormat_H264,
        &MFVideoFormat_NV12,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT);

    printf("H264_ENCODER_AVAILABLE=%s\n",
           hardware_encoders || software_encoders ? "yes" : "no");
    printf("H264_DECODER_AVAILABLE=%s\n",
           hardware_decoders || software_decoders ? "yes" : "no");
    MFShutdown();
    if (should_uninitialize) CoUninitialize();
    return (hardware_encoders || software_encoders) &&
                   (hardware_decoders || software_decoders)
               ? 0
               : 2;
}
#else
#include <stdio.h>
int main(void)
{
    fputs("Media Foundation probe is available on Windows only.\n", stderr);
    return 2;
}
#endif
