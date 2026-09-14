/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_h264.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *out, size_t size, const char *message)
{
    if (out && size) snprintf(out, size, "%s", message ? message : "H264 error");
}

#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>

struct IntegralH264Encoder {
    IMFTransform *transform;
    uint16_t width;
    uint16_t height;
    LONGLONG next_time;
    bool config_sent;
};

struct IntegralH264Decoder {
    IMFTransform *transform;
    uint16_t width;
    uint16_t height;
    uint16_t surface_width;
    uint16_t surface_height;
    bool handling_stream_change;
};

static LONG startup_users;
static bool com_owned;

static void error_hr(char *out, size_t size, const char *where, HRESULT result)
{
    if (out && size) snprintf(out, size, "%s 0x%08lx", where, (unsigned long)result);
}

static int startup(char *error_out, size_t error_out_size)
{
    if (InterlockedIncrement(&startup_users) != 1) return 0;
    HRESULT result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    com_owned = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        InterlockedDecrement(&startup_users);
        error_hr(error_out, error_out_size, "COM startup failed", result);
        return -1;
    }
    result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(result)) {
        if (com_owned) CoUninitialize();
        InterlockedDecrement(&startup_users);
        error_hr(error_out, error_out_size, "Media Foundation startup failed", result);
        return -1;
    }
    return 0;
}

static void shutdown(void)
{
    if (InterlockedDecrement(&startup_users) != 0) return;
    MFShutdown();
    if (com_owned) CoUninitialize();
    com_owned = false;
}

static IMFTransform *activate_transform(const GUID *category,
                                        const GUID *input_subtype,
                                        const GUID *output_subtype,
                                        char *error_out,
                                        size_t error_out_size)
{
    MFT_REGISTER_TYPE_INFO input = {MFMediaType_Video, *input_subtype};
    MFT_REGISTER_TYPE_INFO output = {MFMediaType_Video, *output_subtype};
    IMFActivate **activates = NULL;
    UINT32 count = 0;
    IMFTransform *transform = NULL;
    HRESULT result = MFTEnumEx(*category,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                               &input,
                               &output,
                               &activates,
                               &count);
    if (SUCCEEDED(result) && count) {
        result = IMFActivate_ActivateObject(activates[0], &IID_IMFTransform,
                                            (void **)&transform);
    }
    for (UINT32 index = 0; index < count; index++) IMFActivate_Release(activates[index]);
    CoTaskMemFree(activates);
    if (FAILED(result) || !transform) {
        error_hr(error_out, error_out_size, "Media Foundation H264 transform unavailable", result);
        return NULL;
    }
    return transform;
}

static void request_low_latency(IMFTransform *transform)
{
    IMFAttributes *attributes = NULL;
    if (!transform ||
        FAILED(IMFTransform_GetAttributes(transform, &attributes))) return;
    /* Best effort: some software transforms ignore this hint, but the
     * Windows H.264 decoder otherwise buffers real-time GB frames and emits
     * them in visible bursts. */
    (void)IMFAttributes_SetUINT32(attributes, &MF_LOW_LATENCY, TRUE);
    IMFAttributes_Release(attributes);
}

static IMFMediaType *video_type(const GUID *subtype,
                                uint16_t width,
                                uint16_t height,
                                UINT32 bitrate)
{
    IMFMediaType *type = NULL;
    if (FAILED(MFCreateMediaType(&type))) return NULL;
    if (FAILED(IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video)) ||
        FAILED(IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype)) ||
        FAILED(IMFMediaType_SetUINT64(type, &MF_MT_FRAME_SIZE,
                                      ((UINT64)width << 32) | height)) ||
        FAILED(IMFMediaType_SetUINT64(type, &MF_MT_FRAME_RATE,
                                      ((UINT64)30 << 32) | 1u)) ||
        FAILED(IMFMediaType_SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO,
                                      ((UINT64)1 << 32) | 1u)) ||
        FAILED(IMFMediaType_SetUINT32(type, &MF_MT_INTERLACE_MODE,
                                     MFVideoInterlace_Progressive))) {
        IMFMediaType_Release(type);
        return NULL;
    }
    if (bitrate) (void)IMFMediaType_SetUINT32(type, &MF_MT_AVG_BITRATE, bitrate);
    return type;
}

static IMFSample *make_sample(const uint8_t *data,
                              size_t size,
                              LONGLONG timestamp,
                              LONGLONG duration)
{
    IMFMediaBuffer *buffer = NULL;
    IMFSample *sample = NULL;
    BYTE *destination = NULL;
    if (size > UINT32_MAX || FAILED(MFCreateMemoryBuffer((DWORD)size, &buffer)) ||
        FAILED(IMFMediaBuffer_Lock(buffer, &destination, NULL, NULL))) goto fail;
    memcpy(destination, data, size);
    IMFMediaBuffer_Unlock(buffer);
    destination = NULL;
    if (FAILED(IMFMediaBuffer_SetCurrentLength(buffer, (DWORD)size)) ||
        FAILED(MFCreateSample(&sample)) ||
        FAILED(IMFSample_AddBuffer(sample, buffer))) goto fail;
    (void)IMFSample_SetSampleTime(sample, timestamp);
    (void)IMFSample_SetSampleDuration(sample, duration);
    IMFMediaBuffer_Release(buffer);
    return sample;
fail:
    if (destination) IMFMediaBuffer_Unlock(buffer);
    if (sample) IMFSample_Release(sample);
    if (buffer) IMFMediaBuffer_Release(buffer);
    return NULL;
}

static int sample_bytes(IMFSample *sample,
                        uint8_t *out,
                        size_t capacity,
                        uint32_t *size_out)
{
    IMFMediaBuffer *buffer = NULL;
    BYTE *data = NULL;
    DWORD length = 0;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)) ||
        FAILED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length)) ||
        length > capacity) {
        if (data) IMFMediaBuffer_Unlock(buffer);
        if (buffer) IMFMediaBuffer_Release(buffer);
        return -1;
    }
    memcpy(out, data, length);
    *size_out = length;
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    return 0;
}

static int sample_nv12_bytes(IMFSample *sample,
                             uint8_t *out,
                             size_t capacity,
                             uint32_t *size_out)
{
    IMFMediaBuffer *buffer = NULL;
    IMF2DBuffer *buffer_2d = NULL;
    DWORD length = 0;
    if (FAILED(IMFSample_GetBufferByIndex(sample, 0, &buffer))) return -1;
    HRESULT result = IMFMediaBuffer_QueryInterface(
        buffer, &IID_IMF2DBuffer, (void **)&buffer_2d);
    if (SUCCEEDED(result) && buffer_2d) {
        result = IMF2DBuffer_GetContiguousLength(buffer_2d, &length);
        if (SUCCEEDED(result) && length <= capacity) {
            result = IMF2DBuffer_ContiguousCopyTo(buffer_2d, out, length);
        }
        else if (SUCCEEDED(result)) {
            result = MF_E_BUFFERTOOSMALL;
        }
        IMF2DBuffer_Release(buffer_2d);
        IMFMediaBuffer_Release(buffer);
        if (FAILED(result)) return -1;
        *size_out = length;
        return 0;
    }
    IMFMediaBuffer_Release(buffer);
    return sample_bytes(sample, out, capacity, size_out);
}

static size_t start_code_size(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
        data[offset + 2] == 1) return 3;
    if (offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
        data[offset + 2] == 0 && data[offset + 3] == 1) return 4;
    return 0;
}

static size_t next_start_code(const uint8_t *data, size_t size, size_t offset)
{
    while (offset < size && !start_code_size(data, size, offset)) offset++;
    return offset;
}

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

static uint32_t get_be32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 | data[3];
}

static int annexb_to_avcc(const uint8_t *input,
                          size_t input_size,
                          uint8_t *output,
                          size_t capacity,
                          uint32_t *output_size)
{
    size_t start = next_start_code(input, input_size, 0);
    if (start == input_size) {
        if (input_size > capacity) return -1;
        memcpy(output, input, input_size);
        *output_size = (uint32_t)input_size;
        return 0;
    }
    size_t written = 0;
    while (start < input_size) {
        size_t code = start_code_size(input, input_size, start);
        size_t nal_start = start + code;
        size_t next = next_start_code(input, input_size, nal_start);
        size_t nal_size = next - nal_start;
        if (!nal_size) { start = next; continue; }
        if (nal_size > UINT32_MAX || written + 4 + nal_size > capacity) return -1;
        put_be32(output + written, (uint32_t)nal_size);
        memcpy(output + written + 4, input + nal_start, nal_size);
        written += 4 + nal_size;
        start = next;
    }
    *output_size = (uint32_t)written;
    return written ? 0 : -1;
}

static bool avcc_has_idr(const uint8_t *data, size_t size)
{
    size_t offset = 0;
    while (offset + 4u <= size) {
        uint32_t nal_size = get_be32(data + offset);
        offset += 4u;
        if (!nal_size || offset + nal_size > size) return false;
        if ((data[offset] & 0x1fu) == 5u) return true;
        offset += nal_size;
    }
    return false;
}

static int find_parameter_sets(const uint8_t *data,
                               size_t size,
                               const uint8_t **sps,
                               size_t *sps_size,
                               const uint8_t **pps,
                               size_t *pps_size)
{
    size_t start = next_start_code(data, size, 0);
    while (start < size) {
        size_t nal_start = start + start_code_size(data, size, start);
        size_t next = next_start_code(data, size, nal_start);
        size_t nal_size = next - nal_start;
        if (nal_size) {
            uint8_t type = data[nal_start] & 0x1f;
            if (type == 7 && !*sps) { *sps = data + nal_start; *sps_size = nal_size; }
            if (type == 8 && !*pps) { *pps = data + nal_start; *pps_size = nal_size; }
        }
        start = next;
    }
    return *sps && *pps ? 0 : -1;
}

static int make_config(IMFTransform *transform,
                       uint16_t width,
                       uint16_t height,
                       const uint8_t *fallback,
                       size_t fallback_size,
                       uint8_t *out,
                       size_t capacity,
                       uint32_t *size_out)
{
    IMFMediaType *type = NULL;
    UINT32 blob_size = 0;
    uint8_t *blob = NULL;
    const uint8_t *sps = NULL, *pps = NULL;
    size_t sps_size = 0, pps_size = 0;
    if (SUCCEEDED(IMFTransform_GetOutputCurrentType(transform, 0, &type))) {
        (void)IMFMediaType_GetBlobSize(type, &MF_MT_MPEG_SEQUENCE_HEADER, &blob_size);
        if (blob_size) {
            blob = malloc(blob_size);
            if (blob) {
                UINT32 copied = 0;
                if (FAILED(IMFMediaType_GetBlob(type, &MF_MT_MPEG_SEQUENCE_HEADER,
                                                blob, blob_size, &copied))) {
                    free(blob); blob = NULL; blob_size = 0;
                }
            }
        }
        IMFMediaType_Release(type);
    }
    if (blob) (void)find_parameter_sets(blob, blob_size, &sps, &sps_size, &pps, &pps_size);
    if (!sps || !pps) {
        sps = pps = NULL; sps_size = pps_size = 0;
        (void)find_parameter_sets(fallback, fallback_size, &sps, &sps_size, &pps, &pps_size);
    }
    if (!sps || !pps || sps_size > UINT16_MAX || pps_size > UINT16_MAX ||
        8 + sps_size + pps_size > capacity) {
        free(blob);
        return -1;
    }
    put_be16(out, width); put_be16(out + 2, height);
    put_be16(out + 4, (uint16_t)sps_size); put_be16(out + 6, (uint16_t)pps_size);
    memcpy(out + 8, sps, sps_size); memcpy(out + 8 + sps_size, pps, pps_size);
    *size_out = (uint32_t)(8 + sps_size + pps_size);
    free(blob);
    return 0;
}

static uint8_t clamp_byte(int value)
{
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static void rgb24_to_nv12(const uint8_t *rgb,
                          bool bottom_up,
                          uint8_t *nv12,
                          uint16_t width,
                          uint16_t height)
{
    uint8_t *y_plane = nv12;
    uint8_t *uv_plane = nv12 + (size_t)width * height;
    for (unsigned y = 0; y < height; y++) {
        unsigned source_y = bottom_up ? height - 1u - y : y;
        const uint8_t *row = rgb + (size_t)source_y * width * 3u;
        for (unsigned x = 0; x < width; x++) {
            int r = row[x * 3u], g = row[x * 3u + 1u], b = row[x * 3u + 2u];
            y_plane[(size_t)y * width + x] = clamp_byte(((66*r + 129*g + 25*b + 128) >> 8) + 16);
        }
    }
    for (unsigned y = 0; y < height; y += 2) {
        unsigned source_y = bottom_up ? height - 1u - y : y;
        const uint8_t *row = rgb + (size_t)source_y * width * 3u;
        for (unsigned x = 0; x < width; x += 2) {
            int r = row[x * 3u], g = row[x * 3u + 1u], b = row[x * 3u + 2u];
            uv_plane[(size_t)(y / 2u) * width + x] = clamp_byte(((-38*r - 74*g + 112*b + 128) >> 8) + 128);
            uv_plane[(size_t)(y / 2u) * width + x + 1u] = clamp_byte(((112*r - 94*g - 18*b + 128) >> 8) + 128);
        }
    }
}

IntegralH264Encoder *integral_h264_encoder_create(uint16_t width,
                                        uint16_t height,
                                        char *error_out,
                                        size_t error_out_size)
{
    if (!width || !height || (width & 1) || (height & 1) || startup(error_out, error_out_size)) return NULL;
    IntegralH264Encoder *encoder = calloc(1, sizeof(*encoder));
    if (!encoder) { shutdown(); set_error(error_out, error_out_size, "H264 encoder allocation failed"); return NULL; }
    encoder->width = width; encoder->height = height;
    encoder->transform = activate_transform(&MFT_CATEGORY_VIDEO_ENCODER,
                                            &MFVideoFormat_NV12,
                                            &MFVideoFormat_H264,
                                            error_out,
                                            error_out_size);
    request_low_latency(encoder->transform);
    IMFMediaType *output = video_type(&MFVideoFormat_H264, width, height, 4000000);
    IMFMediaType *input = video_type(&MFVideoFormat_NV12, width, height, 0);
    HRESULT result = encoder->transform && output && input
                         ? IMFTransform_SetOutputType(encoder->transform, 0, output, 0)
                         : E_FAIL;
    if (SUCCEEDED(result)) result = IMFTransform_SetInputType(encoder->transform, 0, input, 0);
    if (SUCCEEDED(result)) result = IMFTransform_ProcessMessage(encoder->transform, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (SUCCEEDED(result)) result = IMFTransform_ProcessMessage(encoder->transform, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (output) IMFMediaType_Release(output);
    if (input) IMFMediaType_Release(input);
    if (FAILED(result)) {
        error_hr(error_out, error_out_size, "H264 encoder configuration failed", result);
        integral_h264_encoder_destroy(encoder); return NULL;
    }
    return encoder;
}

void integral_h264_encoder_destroy(IntegralH264Encoder *encoder)
{
    if (!encoder) return;
    if (encoder->transform) IMFTransform_Release(encoder->transform);
    free(encoder); shutdown();
}

int integral_h264_encoder_encode_rgb24(IntegralH264Encoder *encoder,
                                  const uint8_t *rgb,
                                  bool bottom_up,
                                  uint64_t timestamp_us,
                                  uint8_t *config_out,
                                  size_t config_capacity,
                                  uint32_t *config_size_out,
                                  uint8_t *frame_out,
                                  size_t frame_capacity,
                                  uint32_t *frame_size_out,
                                  bool *keyframe_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (!encoder || !rgb || !config_size_out || !frame_size_out || !keyframe_out) return -1;
    *config_size_out = 0; *frame_size_out = 0; *keyframe_out = false;
    size_t nv12_size = (size_t)encoder->width * encoder->height * 3u / 2u;
    uint8_t *nv12 = malloc(nv12_size);
    uint8_t *raw = malloc(frame_capacity);
    if (!nv12 || !raw) { free(nv12); free(raw); set_error(error_out, error_out_size, "H264 frame allocation failed"); return -1; }
    rgb24_to_nv12(rgb, bottom_up, nv12, encoder->width, encoder->height);
    LONGLONG time = (LONGLONG)(timestamp_us * 10u);
    IMFSample *input = make_sample(nv12, nv12_size, time, 333333);
    free(nv12);
    HRESULT result = input ? IMFTransform_ProcessInput(encoder->transform, 0, input, 0) : E_OUTOFMEMORY;
    if (input) IMFSample_Release(input);
    if (FAILED(result)) { free(raw); error_hr(error_out, error_out_size, "H264 encoder input failed", result); return -1; }
    MFT_OUTPUT_STREAM_INFO info;
    memset(&info, 0, sizeof(info));
    result = IMFTransform_GetOutputStreamInfo(encoder->transform, 0, &info);
    if (FAILED(result)) { free(raw); error_hr(error_out, error_out_size, "H264 encoder stream info failed", result); return -1; }
    IMFSample *output_sample = NULL;
    IMFMediaBuffer *output_buffer = NULL;
    if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        DWORD buffer_size = info.cbSize ? info.cbSize : (DWORD)frame_capacity;
        if (FAILED(MFCreateSample(&output_sample)) ||
            FAILED(MFCreateMemoryBuffer(buffer_size, &output_buffer)) ||
            FAILED(IMFSample_AddBuffer(output_sample, output_buffer))) result = E_OUTOFMEMORY;
    }
    MFT_OUTPUT_DATA_BUFFER output = {0};
    output.dwStreamID = 0; output.pSample = output_sample;
    DWORD status = 0;
    if (SUCCEEDED(result)) result = IMFTransform_ProcessOutput(encoder->transform, 0, 1, &output, &status);
    IMFSample *produced = output.pSample;
    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        if (output.pEvents) IMFCollection_Release(output.pEvents);
        if (output_sample) IMFSample_Release(output_sample);
        if (output_buffer) IMFMediaBuffer_Release(output_buffer);
        free(raw); return 0;
    }
    if (FAILED(result) || !produced) {
        if (output.pEvents) IMFCollection_Release(output.pEvents);
        if (output_sample) IMFSample_Release(output_sample);
        if (output_buffer) IMFMediaBuffer_Release(output_buffer);
        free(raw); error_hr(error_out, error_out_size, "H264 encoder output failed", result); return -1;
    }
    uint32_t raw_size = 0;
    if (sample_bytes(produced, raw, frame_capacity, &raw_size) ||
        annexb_to_avcc(raw, raw_size, frame_out, frame_capacity, frame_size_out)) {
        result = E_FAIL;
    }
    UINT32 clean = 0;
    if (SUCCEEDED(IMFSample_GetUINT32(produced, &MFSampleExtension_CleanPoint, &clean))) *keyframe_out = clean != 0;
    if (!*keyframe_out) *keyframe_out = avcc_has_idr(frame_out, *frame_size_out);
    if (SUCCEEDED(result) && !encoder->config_sent &&
        make_config(encoder->transform, encoder->width, encoder->height,
                    raw, raw_size, config_out, config_capacity, config_size_out) == 0) {
        encoder->config_sent = true;
    }
    if (output.pEvents) IMFCollection_Release(output.pEvents);
    if (output_sample) IMFSample_Release(output_sample);
    if (output_buffer) IMFMediaBuffer_Release(output_buffer);
    free(raw);
    if (FAILED(result)) { set_error(error_out, error_out_size, "H264 encoder produced invalid access unit"); return -1; }
    return *frame_size_out ? 1 : 0;
}

static int avcc_to_annexb(const uint8_t *input,
                          size_t input_size,
                          uint8_t *output,
                          size_t capacity,
                          uint32_t *output_size)
{
    size_t read = 0, written = 0;
    while (read + 4 <= input_size) {
        uint32_t nal_size = get_be32(input + read); read += 4;
        if (!nal_size || read + nal_size > input_size || written + 4u + nal_size > capacity) return -1;
        output[written++] = 0; output[written++] = 0; output[written++] = 0; output[written++] = 1;
        memcpy(output + written, input + read, nal_size);
        written += nal_size; read += nal_size;
    }
    if (read != input_size || !written) return -1;
    *output_size = (uint32_t)written;
    return 0;
}

IntegralH264Decoder *integral_h264_decoder_create(const uint8_t *config,
                                        size_t config_size,
                                        char *error_out,
                                        size_t error_out_size)
{
    if (!config || config_size < 10 || startup(error_out, error_out_size)) return NULL;
    uint16_t width = get_be16(config), height = get_be16(config + 2);
    uint16_t sps_size = get_be16(config + 4), pps_size = get_be16(config + 6);
    if (!width || !height || 8u + sps_size + pps_size != config_size || !sps_size || !pps_size) {
        shutdown(); set_error(error_out, error_out_size, "H264 configuration payload invalid"); return NULL;
    }
    IntegralH264Decoder *decoder = calloc(1, sizeof(*decoder));
    if (!decoder) { shutdown(); return NULL; }
    decoder->width = width; decoder->height = height;
    decoder->surface_width = width; decoder->surface_height = height;
    decoder->transform = activate_transform(&MFT_CATEGORY_VIDEO_DECODER,
                                            &MFVideoFormat_H264,
                                            &MFVideoFormat_NV12,
                                            error_out,
                                            error_out_size);
    request_low_latency(decoder->transform);
    IMFMediaType *input = video_type(&MFVideoFormat_H264, width, height, 0);
    IMFMediaType *output = video_type(&MFVideoFormat_NV12, width, height, 0);
    uint8_t *sequence = malloc(8u + sps_size + pps_size);
    HRESULT result = E_OUTOFMEMORY;
    if (sequence) {
        uint8_t *cursor = sequence;
        for (int index = 0; index < 2; index++) {
            uint16_t length = index ? pps_size : sps_size;
            const uint8_t *source = config + 8u + (index ? sps_size : 0u);
            *cursor++ = 0; *cursor++ = 0; *cursor++ = 0; *cursor++ = 1;
            memcpy(cursor, source, length); cursor += length;
        }
        if (input) (void)IMFMediaType_SetBlob(input, &MF_MT_MPEG_SEQUENCE_HEADER,
                                              sequence, (UINT32)(cursor - sequence));
        free(sequence);
        result = decoder->transform && input && output
                     ? IMFTransform_SetInputType(decoder->transform, 0, input, 0)
                     : E_FAIL;
        if (SUCCEEDED(result)) result = IMFTransform_SetOutputType(decoder->transform, 0, output, 0);
        if (SUCCEEDED(result)) result = IMFTransform_ProcessMessage(decoder->transform, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        if (SUCCEEDED(result)) result = IMFTransform_ProcessMessage(decoder->transform, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    }
    if (input) IMFMediaType_Release(input);
    if (output) IMFMediaType_Release(output);
    if (FAILED(result)) { error_hr(error_out, error_out_size, "H264 decoder configuration failed", result); integral_h264_decoder_destroy(decoder); return NULL; }
    return decoder;
}

void integral_h264_decoder_destroy(IntegralH264Decoder *decoder)
{
    if (!decoder) return;
    if (decoder->transform) IMFTransform_Release(decoder->transform);
    free(decoder); shutdown();
}

static void nv12_to_rgb24(const uint8_t *nv12,
                          uint8_t *rgb,
                          uint16_t surface_width,
                          uint16_t surface_height,
                          uint16_t width,
                          uint16_t height)
{
    const uint8_t *uv = nv12 + (size_t)surface_width * surface_height;
    for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < width; x++) {
        int yy = (int)nv12[(size_t)y * surface_width + x] - 16;
        int u = (int)uv[(size_t)(y / 2u) * surface_width + (x & ~1u)] - 128;
        int v = (int)uv[(size_t)(y / 2u) * surface_width + (x & ~1u) + 1u] - 128;
        int c = yy < 0 ? 0 : 298 * yy;
        rgb[((size_t)y * width + x) * 3u] = clamp_byte((c + 409*v + 128) >> 8);
        rgb[((size_t)y * width + x) * 3u + 1u] = clamp_byte((c - 100*u - 208*v + 128) >> 8);
        rgb[((size_t)y * width + x) * 3u + 2u] = clamp_byte((c + 516*u + 128) >> 8);
    }
}

static int decoder_process_output(IntegralH264Decoder *decoder,
                                  uint8_t *rgb_out,
                                  size_t rgb_capacity,
                                  uint16_t *width_out,
                                  uint16_t *height_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    MFT_OUTPUT_STREAM_INFO info = {0};
    HRESULT result = IMFTransform_GetOutputStreamInfo(decoder->transform, 0, &info);
    if (FAILED(result)) {
        error_hr(error_out, error_out_size, "H264 decoder stream info failed", result);
        return -1;
    }
    IMFSample *output_sample = NULL;
    IMFMediaBuffer *output_buffer = NULL;
    if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        DWORD size = info.cbSize ? info.cbSize
                                 : (DWORD)((size_t)decoder->surface_width *
                                           decoder->surface_height * 3u / 2u);
        if (FAILED(MFCreateSample(&output_sample)) ||
            FAILED(MFCreateMemoryBuffer(size, &output_buffer)) ||
            FAILED(IMFSample_AddBuffer(output_sample, output_buffer))) {
            result = E_OUTOFMEMORY;
        }
    }
    MFT_OUTPUT_DATA_BUFFER output_data = {0};
    output_data.pSample = output_sample;
    DWORD status = 0;
    if (SUCCEEDED(result)) {
        result = IMFTransform_ProcessOutput(decoder->transform, 0, 1, &output_data, &status);
    }
    if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
        if (output_data.pEvents) IMFCollection_Release(output_data.pEvents);
        if (output_data.pSample && output_data.pSample != output_sample) {
            IMFSample_Release(output_data.pSample);
        }
        if (output_sample) IMFSample_Release(output_sample);
        if (output_buffer) IMFMediaBuffer_Release(output_buffer);
        HRESULT type_result = MF_E_INVALIDMEDIATYPE;
        for (DWORD type_index = 0;; type_index++) {
            IMFMediaType *renegotiated = NULL;
            HRESULT available = IMFTransform_GetOutputAvailableType(
                decoder->transform, 0, type_index, &renegotiated);
            if (FAILED(available)) {
                type_result = available;
                break;
            }
            GUID subtype = {0};
            UINT64 frame_size = 0;
            bool usable =
                SUCCEEDED(IMFMediaType_GetGUID(
                    renegotiated, &MF_MT_SUBTYPE, &subtype)) &&
                IsEqualGUID(&subtype, &MFVideoFormat_NV12) &&
                SUCCEEDED(IMFMediaType_GetUINT64(
                    renegotiated, &MF_MT_FRAME_SIZE, &frame_size));
            UINT32 width = (UINT32)(frame_size >> 32);
            UINT32 height = (UINT32)frame_size;
            usable = usable &&
                width > 0 && height > 0 && width <= UINT16_MAX &&
                height <= UINT16_MAX && width >= decoder->width &&
                height >= decoder->height && !(width & 1u) && !(height & 1u);
            if (usable) {
                type_result = IMFTransform_SetOutputType(
                    decoder->transform, 0, renegotiated, 0);
                if (SUCCEEDED(type_result)) {
                    decoder->surface_width = (uint16_t)width;
                    decoder->surface_height = (uint16_t)height;
                }
            }
            IMFMediaType_Release(renegotiated);
            if (usable && SUCCEEDED(type_result)) break;
        }
        if (FAILED(type_result)) {
            error_hr(error_out, error_out_size,
                     "H264 decoder stream change failed", type_result);
            return -1;
        }
        if (decoder->handling_stream_change) {
            set_error(error_out, error_out_size,
                      "H264 decoder stream change did not settle");
            return -1;
        }
        decoder->handling_stream_change = true;
        int retry = decoder_process_output(decoder, rgb_out, rgb_capacity,
                                           width_out, height_out,
                                           error_out, error_out_size);
        decoder->handling_stream_change = false;
        return retry;
    }
    int return_value = 0;
    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        return_value = 0;
    }
    else if (FAILED(result) || !output_data.pSample) {
        error_hr(error_out, error_out_size, "H264 decoder output failed", result);
        return_value = -1;
    }
    else {
        size_t nv12_size = (size_t)decoder->surface_width *
                           decoder->surface_height * 3u / 2u;
        size_t rgb_size = (size_t)decoder->width * decoder->height * 3u;
        uint8_t *nv12 = malloc(nv12_size);
        uint32_t bytes = 0;
        if (!nv12 || rgb_size > rgb_capacity ||
            sample_nv12_bytes(output_data.pSample, nv12, nv12_size, &bytes) ||
            bytes < nv12_size) {
            free(nv12);
            set_error(error_out, error_out_size, "H264 decoded frame invalid");
            return_value = -1;
        }
        else {
            nv12_to_rgb24(nv12, rgb_out,
                          decoder->surface_width, decoder->surface_height,
                          decoder->width, decoder->height);
            free(nv12);
            *width_out = decoder->width;
            *height_out = decoder->height;
            return_value = 1;
        }
    }
    if (output_data.pEvents) IMFCollection_Release(output_data.pEvents);
    if (output_data.pSample && output_data.pSample != output_sample) {
        IMFSample_Release(output_data.pSample);
    }
    if (output_sample) IMFSample_Release(output_sample);
    if (output_buffer) IMFMediaBuffer_Release(output_buffer);
    return return_value;
}

int integral_h264_decoder_decode_avcc(IntegralH264Decoder *decoder,
                                 const uint8_t *frame,
                                 size_t frame_size,
                                 uint64_t timestamp_us,
                                 uint8_t *rgb_out,
                                 size_t rgb_capacity,
                                 uint16_t *width_out,
                                 uint16_t *height_out,
                                 char *error_out,
                                 size_t error_out_size)
{
    if (!decoder || !frame || !rgb_out) return -1;
    size_t annex_capacity = frame_size + 64u;
    uint8_t *annex = malloc(annex_capacity);
    uint32_t annex_size = 0;
    if (!annex || avcc_to_annexb(frame, frame_size, annex, annex_capacity, &annex_size)) {
        free(annex); set_error(error_out, error_out_size, "H264 AVCC sample invalid"); return -1;
    }
    IMFSample *input = make_sample(annex, annex_size, (LONGLONG)(timestamp_us * 10u), 333333);
    free(annex);
    HRESULT result = input ? IMFTransform_ProcessInput(decoder->transform, 0, input, 0) : E_OUTOFMEMORY;
    int decoded = 0;
    for (unsigned retry = 0; result == MF_E_NOTACCEPTING && retry < 4u; retry++) {
        /* Preserve one presentation candidate per compressed input call.
         * Draining every available MFT output here collapses a WAN burst into
         * one shared RGB buffer and makes several valid frames invisible. */
        int output = decoder_process_output(decoder,
                                            rgb_out,
                                            rgb_capacity,
                                            width_out,
                                            height_out,
                                            error_out,
                                            error_out_size);
        if (output < 0) {
            if (input) IMFSample_Release(input);
            return -1;
        }
        if (output > 0) decoded = 1;
        result = input ? IMFTransform_ProcessInput(decoder->transform, 0, input, 0)
                       : E_OUTOFMEMORY;
        if (decoded && SUCCEEDED(result)) break;
    }
    if (input) IMFSample_Release(input);
    if (FAILED(result)) { error_hr(error_out, error_out_size, "H264 decoder input failed", result); return -1; }
    if (!decoded) {
        int output = decoder_process_output(decoder,
                                            rgb_out,
                                            rgb_capacity,
                                            width_out,
                                            height_out,
                                            error_out,
                                            error_out_size);
        if (output < 0) return -1;
        if (output > 0) decoded = 1;
    }
    return decoded;
}

#elif defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

struct IntegralH264Encoder {
    VTCompressionSessionRef session;
    uint16_t width;
    uint16_t height;
    bool config_sent;
    uint8_t *config_out;
    size_t config_capacity;
    uint32_t *config_size_out;
    uint8_t *frame_out;
    size_t frame_capacity;
    uint32_t *frame_size_out;
    bool *keyframe_out;
    OSStatus callback_status;
    uint8_t *parameter_sets;
    size_t parameter_sets_size;
};

struct IntegralH264Decoder {
    VTDecompressionSessionRef session;
    CMVideoFormatDescriptionRef format;
    uint16_t width;
    uint16_t height;
    CVPixelBufferRef output;
    OSStatus callback_status;
};

static void apple_error(char *out, size_t size, const char *where, OSStatus status)
{
    if (out && size) snprintf(out, size, "%s %d", where, (int)status);
}

static void apple_put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void apple_put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t apple_get_be16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

static uint8_t apple_clamp_byte(int value)
{
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static OSStatus apple_set_s32_property(VTSessionRef session, CFStringRef key, int32_t value)
{
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    if (!number) return memFullErr;
    OSStatus status = VTSessionSetProperty(session, key, number);
    CFRelease(number);
    return status;
}

static void apple_encoder_callback(void *output_callback_refcon,
                                   void *source_frame_refcon,
                                   OSStatus status,
                                   VTEncodeInfoFlags info_flags,
                                   CMSampleBufferRef sample)
{
    (void)source_frame_refcon;
    (void)info_flags;
    IntegralH264Encoder *encoder = output_callback_refcon;
    if (!encoder || encoder->callback_status != noErr) return;
    if (status != noErr) {
        encoder->callback_status = status;
        return;
    }
    if (!sample || !CMSampleBufferDataIsReady(sample)) return;

    if (!encoder->config_sent) {
        CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
        const uint8_t *sps = NULL, *pps = NULL;
        size_t sps_size = 0, pps_size = 0, count = 0;
        int nal_length = 0;
        OSStatus config_status = format
            ? CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,
                                                                 0,
                                                                 &sps,
                                                                 &sps_size,
                                                                 &count,
                                                                 &nal_length)
            : paramErr;
        if (config_status == noErr) {
            config_status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,
                                                                                1,
                                                                                &pps,
                                                                                &pps_size,
                                                                                NULL,
                                                                                NULL);
        }
        size_t total = 8u + sps_size + pps_size;
        if (config_status != noErr || count < 2 || nal_length != 4 ||
            !sps_size || !pps_size || sps_size > UINT16_MAX || pps_size > UINT16_MAX ||
            total > encoder->config_capacity || !encoder->config_out ||
            !encoder->config_size_out) {
            encoder->callback_status = config_status != noErr ? config_status : paramErr;
            return;
        }
        apple_put_be16(encoder->config_out, encoder->width);
        apple_put_be16(encoder->config_out + 2, encoder->height);
        apple_put_be16(encoder->config_out + 4, (uint16_t)sps_size);
        apple_put_be16(encoder->config_out + 6, (uint16_t)pps_size);
        memcpy(encoder->config_out + 8, sps, sps_size);
        memcpy(encoder->config_out + 8 + sps_size, pps, pps_size);
        *encoder->config_size_out = (uint32_t)total;
        size_t inband_size = 8u + sps_size + pps_size;
        encoder->parameter_sets = malloc(inband_size);
        if (!encoder->parameter_sets) {
            encoder->callback_status = memFullErr;
            return;
        }
        apple_put_be32(encoder->parameter_sets, (uint32_t)sps_size);
        memcpy(encoder->parameter_sets + 4, sps, sps_size);
        apple_put_be32(encoder->parameter_sets + 4u + sps_size, (uint32_t)pps_size);
        memcpy(encoder->parameter_sets + 8u + sps_size, pps, pps_size);
        encoder->parameter_sets_size = inband_size;
        encoder->config_sent = true;
    }

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
    size_t frame_size = block ? CMBlockBufferGetDataLength(block) : 0;
    bool keyframe = true;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        CFDictionaryRef attachment = CFArrayGetValueAtIndex(attachments, 0);
        keyframe = !CFDictionaryContainsKey(attachment, kCMSampleAttachmentKey_NotSync);
    }
    size_t prefix_size = keyframe ? encoder->parameter_sets_size : 0;
    if (!frame_size || prefix_size > encoder->frame_capacity ||
        frame_size > encoder->frame_capacity - prefix_size || !encoder->frame_out ||
        !encoder->frame_size_out) {
        encoder->callback_status = paramErr;
        return;
    }
    if (prefix_size) memcpy(encoder->frame_out, encoder->parameter_sets, prefix_size);
    status = CMBlockBufferCopyDataBytes(block,
                                        0,
                                        frame_size,
                                        encoder->frame_out + prefix_size);
    if (status != noErr) {
        encoder->callback_status = status;
        return;
    }
    *encoder->frame_size_out = (uint32_t)(prefix_size + frame_size);
    if (encoder->keyframe_out) *encoder->keyframe_out = keyframe;
}

IntegralH264Encoder *integral_h264_encoder_create(uint16_t width,
                                        uint16_t height,
                                        char *error_out,
                                        size_t error_out_size)
{
    if (!width || !height || (width & 1u) || (height & 1u)) {
        set_error(error_out, error_out_size, "H264 encoder dimensions invalid");
        return NULL;
    }
    IntegralH264Encoder *encoder = calloc(1, sizeof(*encoder));
    if (!encoder) {
        set_error(error_out, error_out_size, "H264 encoder allocation failed");
        return NULL;
    }
    encoder->width = width;
    encoder->height = height;
    OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault,
                                                  width,
                                                  height,
                                                  kCMVideoCodecType_H264,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  apple_encoder_callback,
                                                  encoder,
                                                  &encoder->session);
    if (status == noErr) {
        status = VTSessionSetProperty(encoder->session,
                                      kVTCompressionPropertyKey_RealTime,
                                      kCFBooleanTrue);
    }
    if (status == noErr) {
        status = VTSessionSetProperty(encoder->session,
                                      kVTCompressionPropertyKey_AllowFrameReordering,
                                      kCFBooleanFalse);
    }
    if (status == noErr) {
        status = VTSessionSetProperty(encoder->session,
                                      kVTCompressionPropertyKey_ProfileLevel,
                                      kVTProfileLevel_H264_Baseline_AutoLevel);
    }
    if (status == noErr) {
        status = apple_set_s32_property(encoder->session,
                                        kVTCompressionPropertyKey_ExpectedFrameRate,
                                        30);
    }
    if (status == noErr) {
        status = apple_set_s32_property(encoder->session,
                                        kVTCompressionPropertyKey_AverageBitRate,
                                        4000000);
    }
    if (status == noErr) {
        status = apple_set_s32_property(encoder->session,
                                        kVTCompressionPropertyKey_MaxKeyFrameInterval,
                                        60);
    }
    if (status == noErr) status = VTCompressionSessionPrepareToEncodeFrames(encoder->session);
    if (status != noErr) {
        apple_error(error_out, error_out_size, "VideoToolbox H264 encoder configuration failed", status);
        integral_h264_encoder_destroy(encoder);
        return NULL;
    }
    return encoder;
}

void integral_h264_encoder_destroy(IntegralH264Encoder *encoder)
{
    if (!encoder) return;
    if (encoder->session) {
        (void)VTCompressionSessionCompleteFrames(encoder->session, kCMTimeInvalid);
        VTCompressionSessionInvalidate(encoder->session);
        CFRelease(encoder->session);
    }
    free(encoder->parameter_sets);
    free(encoder);
}

static void apple_fill_nv12(CVPixelBufferRef pixel,
                            const uint8_t *rgb,
                            bool bottom_up,
                            uint16_t width,
                            uint16_t height)
{
    uint8_t *y_plane = CVPixelBufferGetBaseAddressOfPlane(pixel, 0);
    uint8_t *uv_plane = CVPixelBufferGetBaseAddressOfPlane(pixel, 1);
    size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel, 0);
    size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel, 1);
    for (unsigned y = 0; y < height; y++) {
        unsigned source_y = bottom_up ? height - 1u - y : y;
        const uint8_t *row = rgb + (size_t)source_y * width * 3u;
        uint8_t *destination = y_plane + (size_t)y * y_stride;
        for (unsigned x = 0; x < width; x++) {
            int r = row[x * 3u], g = row[x * 3u + 1u], b = row[x * 3u + 2u];
            destination[x] = apple_clamp_byte(((66*r + 129*g + 25*b + 128) >> 8) + 16);
        }
    }
    for (unsigned y = 0; y < height; y += 2) {
        unsigned source_y = bottom_up ? height - 1u - y : y;
        const uint8_t *row = rgb + (size_t)source_y * width * 3u;
        uint8_t *destination = uv_plane + (size_t)(y / 2u) * uv_stride;
        for (unsigned x = 0; x < width; x += 2) {
            int r = row[x * 3u], g = row[x * 3u + 1u], b = row[x * 3u + 2u];
            destination[x] = apple_clamp_byte(((-38*r - 74*g + 112*b + 128) >> 8) + 128);
            destination[x + 1u] = apple_clamp_byte(((112*r - 94*g - 18*b + 128) >> 8) + 128);
        }
    }
}

int integral_h264_encoder_encode_rgb24(IntegralH264Encoder *encoder,
                                  const uint8_t *rgb,
                                  bool bottom_up,
                                  uint64_t timestamp_us,
                                  uint8_t *config_out,
                                  size_t config_capacity,
                                  uint32_t *config_size_out,
                                  uint8_t *frame_out,
                                  size_t frame_capacity,
                                  uint32_t *frame_size_out,
                                  bool *keyframe_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (!encoder || !rgb || !config_size_out || !frame_size_out || !keyframe_out) return -1;
    *config_size_out = 0;
    *frame_size_out = 0;
    *keyframe_out = false;
    CVPixelBufferRef pixel = NULL;
    CVReturn result = CVPixelBufferCreate(kCFAllocatorDefault,
                                          encoder->width,
                                          encoder->height,
                                          kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                                          NULL,
                                          &pixel);
    if (result != kCVReturnSuccess || !pixel) {
        apple_error(error_out, error_out_size, "VideoToolbox H264 pixel buffer failed", result);
        return -1;
    }
    result = CVPixelBufferLockBaseAddress(pixel, 0);
    if (result != kCVReturnSuccess || !CVPixelBufferIsPlanar(pixel) ||
        CVPixelBufferGetPlaneCount(pixel) < 2) {
        if (result == kCVReturnSuccess) CVPixelBufferUnlockBaseAddress(pixel, 0);
        CVPixelBufferRelease(pixel);
        apple_error(error_out, error_out_size, "VideoToolbox H264 pixel buffer layout failed", result);
        return -1;
    }
    apple_fill_nv12(pixel, rgb, bottom_up, encoder->width, encoder->height);
    CVPixelBufferUnlockBaseAddress(pixel, 0);

    encoder->config_out = config_out;
    encoder->config_capacity = config_capacity;
    encoder->config_size_out = config_size_out;
    encoder->frame_out = frame_out;
    encoder->frame_capacity = frame_capacity;
    encoder->frame_size_out = frame_size_out;
    encoder->keyframe_out = keyframe_out;
    encoder->callback_status = noErr;
    CMTime timestamp = CMTimeMake((int64_t)timestamp_us, 1000000);
    CMTime duration = CMTimeMake(1, 30);
    VTEncodeInfoFlags flags = 0;
    OSStatus status = VTCompressionSessionEncodeFrame(encoder->session,
                                                       pixel,
                                                       timestamp,
                                                       duration,
                                                       NULL,
                                                       NULL,
                                                       &flags);
    CVPixelBufferRelease(pixel);
    if (status == noErr) {
        status = VTCompressionSessionCompleteFrames(encoder->session, kCMTimeInvalid);
    }
    if (status == noErr) status = encoder->callback_status;
    encoder->config_out = NULL;
    encoder->config_size_out = NULL;
    encoder->frame_out = NULL;
    encoder->frame_size_out = NULL;
    encoder->keyframe_out = NULL;
    if (status != noErr) {
        apple_error(error_out, error_out_size, "VideoToolbox H264 encode failed", status);
        return -1;
    }
    return *frame_size_out ? 1 : 0;
}

static void apple_decoder_callback(void *decompression_output_refcon,
                                   void *source_frame_refcon,
                                   OSStatus status,
                                   VTDecodeInfoFlags info_flags,
                                   CVImageBufferRef image,
                                   CMTime presentation_timestamp,
                                   CMTime presentation_duration)
{
    (void)source_frame_refcon;
    (void)info_flags;
    (void)presentation_timestamp;
    (void)presentation_duration;
    IntegralH264Decoder *decoder = decompression_output_refcon;
    if (!decoder) return;
    if (status != noErr) {
        decoder->callback_status = status;
        return;
    }
    if (!image) return;
    if (decoder->output) CVPixelBufferRelease(decoder->output);
    decoder->output = CVPixelBufferRetain(image);
}

IntegralH264Decoder *integral_h264_decoder_create(const uint8_t *config,
                                        size_t config_size,
                                        char *error_out,
                                        size_t error_out_size)
{
    if (!config || config_size < 10) {
        set_error(error_out, error_out_size, "H264 configuration payload invalid");
        return NULL;
    }
    uint16_t width = apple_get_be16(config), height = apple_get_be16(config + 2);
    uint16_t sps_size = apple_get_be16(config + 4), pps_size = apple_get_be16(config + 6);
    if (!width || !height || !sps_size || !pps_size ||
        8u + (size_t)sps_size + pps_size != config_size) {
        set_error(error_out, error_out_size, "H264 configuration payload invalid");
        return NULL;
    }
    IntegralH264Decoder *decoder = calloc(1, sizeof(*decoder));
    if (!decoder) {
        set_error(error_out, error_out_size, "H264 decoder allocation failed");
        return NULL;
    }
    decoder->width = width;
    decoder->height = height;
    const uint8_t *sets[] = {config + 8, config + 8u + sps_size};
    size_t sizes[] = {sps_size, pps_size};
    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
                                                                          2,
                                                                          sets,
                                                                          sizes,
                                                                          4,
                                                                          &decoder->format);
    CFMutableDictionaryRef attributes = NULL;
    if (status == noErr) {
        attributes = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                3,
                                                &kCFTypeDictionaryKeyCallBacks,
                                                &kCFTypeDictionaryValueCallBacks);
        if (!attributes) status = memFullErr;
    }
    int32_t pixel_format = (int32_t)kCVPixelFormatType_32BGRA;
    int32_t pixel_width = width, pixel_height = height;
    CFNumberRef format_number = attributes
        ? CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixel_format) : NULL;
    CFNumberRef width_number = attributes
        ? CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixel_width) : NULL;
    CFNumberRef height_number = attributes
        ? CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixel_height) : NULL;
    if (attributes && (!format_number || !width_number || !height_number)) status = memFullErr;
    if (status == noErr) {
        CFDictionarySetValue(attributes, kCVPixelBufferPixelFormatTypeKey, format_number);
        CFDictionarySetValue(attributes, kCVPixelBufferWidthKey, width_number);
        CFDictionarySetValue(attributes, kCVPixelBufferHeightKey, height_number);
        VTDecompressionOutputCallbackRecord callback = {apple_decoder_callback, decoder};
        status = VTDecompressionSessionCreate(kCFAllocatorDefault,
                                               decoder->format,
                                               NULL,
                                               attributes,
                                               &callback,
                                               &decoder->session);
    }
    if (format_number) CFRelease(format_number);
    if (width_number) CFRelease(width_number);
    if (height_number) CFRelease(height_number);
    if (attributes) CFRelease(attributes);
    if (status != noErr) {
        apple_error(error_out, error_out_size, "VideoToolbox H264 decoder configuration failed", status);
        integral_h264_decoder_destroy(decoder);
        return NULL;
    }
    return decoder;
}

void integral_h264_decoder_destroy(IntegralH264Decoder *decoder)
{
    if (!decoder) return;
    if (decoder->session) {
        VTDecompressionSessionWaitForAsynchronousFrames(decoder->session);
        VTDecompressionSessionInvalidate(decoder->session);
        CFRelease(decoder->session);
    }
    if (decoder->output) CVPixelBufferRelease(decoder->output);
    if (decoder->format) CFRelease(decoder->format);
    free(decoder);
}

int integral_h264_decoder_decode_avcc(IntegralH264Decoder *decoder,
                                 const uint8_t *frame,
                                 size_t frame_size,
                                 uint64_t timestamp_us,
                                 uint8_t *rgb_out,
                                 size_t rgb_capacity,
                                 uint16_t *width_out,
                                 uint16_t *height_out,
                                 char *error_out,
                                 size_t error_out_size)
{
    if (!decoder || !frame || !frame_size || !rgb_out || !width_out || !height_out) return -1;
    if ((size_t)decoder->width * decoder->height * 3u > rgb_capacity) {
        set_error(error_out, error_out_size, "H264 decoded frame buffer too small");
        return -1;
    }
    CMBlockBufferRef block = NULL;
    CMSampleBufferRef sample = NULL;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,
                                                         NULL,
                                                         frame_size,
                                                         kCFAllocatorDefault,
                                                         NULL,
                                                         0,
                                                         frame_size,
                                                         0,
                                                         &block);
    if (status == noErr) status = CMBlockBufferReplaceDataBytes(frame, block, 0, frame_size);
    CMTime timing_timestamp = CMTimeMake((int64_t)timestamp_us, 1000000);
    CMSampleTimingInfo timing = {CMTimeMake(1, 30), timing_timestamp, kCMTimeInvalid};
    if (status == noErr) {
        status = CMSampleBufferCreateReady(kCFAllocatorDefault,
                                           block,
                                           decoder->format,
                                           1,
                                           1,
                                           &timing,
                                           1,
                                           &frame_size,
                                           &sample);
    }
    if (decoder->output) {
        CVPixelBufferRelease(decoder->output);
        decoder->output = NULL;
    }
    decoder->callback_status = noErr;
    VTDecodeInfoFlags flags = 0;
    if (status == noErr) {
        status = VTDecompressionSessionDecodeFrame(decoder->session,
                                                    sample,
                                                    kVTDecodeFrame_EnableAsynchronousDecompression,
                                                    NULL,
                                                    &flags);
    }
    if (status == noErr) status = VTDecompressionSessionWaitForAsynchronousFrames(decoder->session);
    if (status == noErr) status = decoder->callback_status;
    if (sample) CFRelease(sample);
    if (block) CFRelease(block);
    if (status != noErr) {
        apple_error(error_out, error_out_size, "VideoToolbox H264 decode failed", status);
        return -1;
    }
    if (!decoder->output) return 0;
    CVReturn lock_status = CVPixelBufferLockBaseAddress(decoder->output, kCVPixelBufferLock_ReadOnly);
    if (lock_status != kCVReturnSuccess || CVPixelBufferGetPixelFormatType(decoder->output) !=
                                               kCVPixelFormatType_32BGRA) {
        if (lock_status == kCVReturnSuccess) {
            CVPixelBufferUnlockBaseAddress(decoder->output, kCVPixelBufferLock_ReadOnly);
        }
        apple_error(error_out, error_out_size, "VideoToolbox H264 output layout failed", lock_status);
        return -1;
    }
    size_t output_width = CVPixelBufferGetWidth(decoder->output);
    size_t output_height = CVPixelBufferGetHeight(decoder->output);
    if (output_width != decoder->width || output_height != decoder->height) {
        CVPixelBufferUnlockBaseAddress(decoder->output, kCVPixelBufferLock_ReadOnly);
        set_error(error_out, error_out_size, "VideoToolbox H264 output dimensions changed");
        return -1;
    }
    const uint8_t *base = CVPixelBufferGetBaseAddress(decoder->output);
    size_t stride = CVPixelBufferGetBytesPerRow(decoder->output);
    for (size_t y = 0; y < output_height; y++) {
        const uint8_t *source = base + y * stride;
        uint8_t *destination = rgb_out + y * output_width * 3u;
        for (size_t x = 0; x < output_width; x++) {
            destination[x * 3u] = source[x * 4u + 2u];
            destination[x * 3u + 1u] = source[x * 4u + 1u];
            destination[x * 3u + 2u] = source[x * 4u];
        }
    }
    CVPixelBufferUnlockBaseAddress(decoder->output, kCVPixelBufferLock_ReadOnly);
    *width_out = decoder->width;
    *height_out = decoder->height;
    return 1;
}

#elif defined(__linux__) && defined(INTEGRAL_USE_OPENH264)

#include <limits.h>
#include <wels/codec_api.h>

struct IntegralH264Encoder {
    ISVCEncoder *codec;
    uint16_t width;
    uint16_t height;
    uint8_t *i420;
    uint8_t *parameter_sets_avcc;
    size_t parameter_sets_avcc_size;
    uint8_t *sps;
    size_t sps_size;
    uint8_t *pps;
    size_t pps_size;
    bool config_sent;
};

struct IntegralH264Decoder {
    ISVCDecoder *codec;
    uint16_t width;
    uint16_t height;
    uint8_t *parameter_sets_annexb;
    size_t parameter_sets_annexb_size;
};

static void linux_put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void linux_put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t linux_get_be16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

static uint32_t linux_get_be32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 | data[3];
}

static uint8_t linux_clamp_byte(int value)
{
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static size_t linux_start_code_size(const uint8_t *data, size_t size)
{
    if (size >= 4u && data[0] == 0u && data[1] == 0u &&
        data[2] == 0u && data[3] == 1u) return 4u;
    if (size >= 3u && data[0] == 0u && data[1] == 0u && data[2] == 1u) return 3u;
    return 0u;
}

static int linux_append_openh264_layers_avcc(const SFrameBSInfo *info,
                                              uint8_t *output,
                                              size_t capacity,
                                              uint32_t *output_size,
                                              bool *has_sps,
                                              bool *has_pps,
                                              bool *has_idr)
{
    size_t written = 0u;
    if (!info || !output || !output_size) return -1;
    for (int layer_index = 0; layer_index < info->iLayerNum; layer_index++) {
        const SLayerBSInfo *layer = &info->sLayerInfo[layer_index];
        const uint8_t *cursor = layer->pBsBuf;
        if (!cursor || layer->iNalCount < 0 || !layer->pNalLengthInByte) return -1;
        for (int nal_index = 0; nal_index < layer->iNalCount; nal_index++) {
            int encoded_length = layer->pNalLengthInByte[nal_index];
            if (encoded_length <= 0) return -1;
            size_t encoded_size = (size_t)encoded_length;
            size_t start_size = linux_start_code_size(cursor, encoded_size);
            if (!start_size || encoded_size <= start_size) return -1;
            const uint8_t *nal = cursor + start_size;
            size_t nal_size = encoded_size - start_size;
            if (nal_size > UINT32_MAX || written + 4u + nal_size > capacity) return -1;
            linux_put_be32(output + written, (uint32_t)nal_size);
            memcpy(output + written + 4u, nal, nal_size);
            written += 4u + nal_size;
            uint8_t nal_type = nal[0] & 0x1fu;
            if (has_sps && nal_type == 7u) *has_sps = true;
            if (has_pps && nal_type == 8u) *has_pps = true;
            if (has_idr && nal_type == 5u) *has_idr = true;
            cursor += encoded_size;
        }
    }
    if (!written || written > UINT32_MAX) return -1;
    *output_size = (uint32_t)written;
    return 0;
}

static int linux_copy_parameter_sets(IntegralH264Encoder *encoder,
                                     const SFrameBSInfo *info)
{
    size_t capacity = 65536u;
    uint8_t *avcc = malloc(capacity);
    uint32_t avcc_size = 0u;
    bool has_sps = false, has_pps = false;
    if (!avcc || linux_append_openh264_layers_avcc(info, avcc, capacity,
                                                   &avcc_size, &has_sps,
                                                   &has_pps, NULL) != 0 ||
        !has_sps || !has_pps) {
        free(avcc);
        return -1;
    }
    size_t offset = 0u;
    while (offset + 4u <= avcc_size) {
        uint32_t nal_size = linux_get_be32(avcc + offset);
        offset += 4u;
        if (!nal_size || offset + nal_size > avcc_size) {
            free(avcc);
            return -1;
        }
        uint8_t type = avcc[offset] & 0x1fu;
        if (type == 7u && !encoder->sps) {
            encoder->sps = malloc(nal_size);
            if (!encoder->sps) {
                free(avcc);
                return -1;
            }
            memcpy(encoder->sps, avcc + offset, nal_size);
            encoder->sps_size = nal_size;
        }
        else if (type == 8u && !encoder->pps) {
            encoder->pps = malloc(nal_size);
            if (!encoder->pps) {
                free(avcc);
                return -1;
            }
            memcpy(encoder->pps, avcc + offset, nal_size);
            encoder->pps_size = nal_size;
        }
        offset += nal_size;
    }
    if (offset != avcc_size || !encoder->sps || !encoder->pps ||
        encoder->sps_size > UINT16_MAX || encoder->pps_size > UINT16_MAX) {
        free(avcc);
        return -1;
    }
    encoder->parameter_sets_avcc = avcc;
    encoder->parameter_sets_avcc_size = avcc_size;
    return 0;
}

static void linux_rgb24_to_i420(const uint8_t *rgb,
                                bool bottom_up,
                                uint8_t *i420,
                                uint16_t width,
                                uint16_t height)
{
    uint8_t *y_plane = i420;
    uint8_t *u_plane = y_plane + (size_t)width * height;
    uint8_t *v_plane = u_plane + (size_t)width * height / 4u;
    for (unsigned y = 0; y < height; y++) {
        unsigned source_y = bottom_up ? height - 1u - y : y;
        const uint8_t *row = rgb + (size_t)source_y * width * 3u;
        for (unsigned x = 0; x < width; x++) {
            int r = row[x * 3u];
            int g = row[x * 3u + 1u];
            int b = row[x * 3u + 2u];
            y_plane[(size_t)y * width + x] =
                linux_clamp_byte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }
    for (unsigned y = 0; y < height; y += 2u) {
        for (unsigned x = 0; x < width; x += 2u) {
            int r = 0, g = 0, b = 0;
            for (unsigned dy = 0; dy < 2u; dy++) {
                unsigned output_y = y + dy;
                unsigned source_y = bottom_up ? height - 1u - output_y : output_y;
                const uint8_t *row = rgb + (size_t)source_y * width * 3u;
                for (unsigned dx = 0; dx < 2u; dx++) {
                    r += row[(x + dx) * 3u];
                    g += row[(x + dx) * 3u + 1u];
                    b += row[(x + dx) * 3u + 2u];
                }
            }
            r /= 4; g /= 4; b /= 4;
            size_t chroma = (size_t)(y / 2u) * (width / 2u) + x / 2u;
            u_plane[chroma] =
                linux_clamp_byte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            v_plane[chroma] =
                linux_clamp_byte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}

IntegralH264Encoder *integral_h264_encoder_create(uint16_t width,
                                        uint16_t height,
                                        char *error_out,
                                        size_t error_out_size)
{
    if (!width || !height || (width & 1u) || (height & 1u)) {
        set_error(error_out, error_out_size, "OpenH264 encoder dimensions invalid");
        return NULL;
    }
    IntegralH264Encoder *encoder = calloc(1, sizeof(*encoder));
    if (!encoder) {
        set_error(error_out, error_out_size, "OpenH264 encoder allocation failed");
        return NULL;
    }
    encoder->width = width;
    encoder->height = height;
    encoder->i420 = malloc((size_t)width * height * 3u / 2u);
    if (!encoder->i420 || WelsCreateSVCEncoder(&encoder->codec) != 0 || !encoder->codec) {
        set_error(error_out, error_out_size, "OpenH264 encoder creation failed");
        integral_h264_encoder_destroy(encoder);
        return NULL;
    }
    SEncParamBase parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.iUsageType = CAMERA_VIDEO_REAL_TIME;
    parameters.iPicWidth = width;
    parameters.iPicHeight = height;
    parameters.iTargetBitrate = 4000000;
    parameters.iRCMode = RC_BITRATE_MODE;
    parameters.fMaxFrameRate = 30.0f;
    int trace_level = WELS_LOG_ERROR;
    (void)(*encoder->codec)->SetOption(encoder->codec,
                                      ENCODER_OPTION_TRACE_LEVEL,
                                      &trace_level);
    if ((*encoder->codec)->Initialize(encoder->codec, &parameters) != cmResultSuccess) {
        set_error(error_out, error_out_size, "OpenH264 encoder configuration failed");
        integral_h264_encoder_destroy(encoder);
        return NULL;
    }
    int format = videoFormatI420;
    int idr_interval = 60;
    if ((*encoder->codec)->SetOption(encoder->codec, ENCODER_OPTION_DATAFORMAT, &format) != cmResultSuccess ||
        (*encoder->codec)->SetOption(encoder->codec, ENCODER_OPTION_IDR_INTERVAL, &idr_interval) != cmResultSuccess ||
        (*encoder->codec)->SetOption(encoder->codec, ENCODER_OPTION_TRACE_LEVEL, &trace_level) != cmResultSuccess) {
        set_error(error_out, error_out_size, "OpenH264 encoder option failed");
        integral_h264_encoder_destroy(encoder);
        return NULL;
    }
    SFrameBSInfo parameter_sets;
    memset(&parameter_sets, 0, sizeof(parameter_sets));
    if ((*encoder->codec)->EncodeParameterSets(encoder->codec, &parameter_sets) != cmResultSuccess ||
        linux_copy_parameter_sets(encoder, &parameter_sets) != 0) {
        set_error(error_out, error_out_size, "OpenH264 parameter sets unavailable");
        integral_h264_encoder_destroy(encoder);
        return NULL;
    }
    return encoder;
}

void integral_h264_encoder_destroy(IntegralH264Encoder *encoder)
{
    if (!encoder) return;
    if (encoder->codec) {
        (void)(*encoder->codec)->Uninitialize(encoder->codec);
        WelsDestroySVCEncoder(encoder->codec);
    }
    free(encoder->i420);
    free(encoder->parameter_sets_avcc);
    free(encoder->sps);
    free(encoder->pps);
    free(encoder);
}

int integral_h264_encoder_encode_rgb24(IntegralH264Encoder *encoder,
                                  const uint8_t *rgb,
                                  bool bottom_up,
                                  uint64_t timestamp_us,
                                  uint8_t *config_out,
                                  size_t config_capacity,
                                  uint32_t *config_size_out,
                                  uint8_t *frame_out,
                                  size_t frame_capacity,
                                  uint32_t *frame_size_out,
                                  bool *keyframe_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (!encoder || !rgb || !config_size_out || !frame_out ||
        !frame_size_out || !keyframe_out) return -1;
    *config_size_out = 0u;
    *frame_size_out = 0u;
    *keyframe_out = false;
    linux_rgb24_to_i420(rgb, bottom_up, encoder->i420,
                        encoder->width, encoder->height);
    SSourcePicture picture;
    memset(&picture, 0, sizeof(picture));
    picture.iColorFormat = videoFormatI420;
    picture.iStride[0] = encoder->width;
    picture.iStride[1] = encoder->width / 2u;
    picture.iStride[2] = encoder->width / 2u;
    picture.pData[0] = encoder->i420;
    picture.pData[1] = encoder->i420 + (size_t)encoder->width * encoder->height;
    picture.pData[2] = picture.pData[1] + (size_t)encoder->width * encoder->height / 4u;
    picture.iPicWidth = encoder->width;
    picture.iPicHeight = encoder->height;
    picture.uiTimeStamp = (long long)(timestamp_us / 1000u);
    SFrameBSInfo info;
    memset(&info, 0, sizeof(info));
    if ((*encoder->codec)->EncodeFrame(encoder->codec, &picture, &info) != cmResultSuccess) {
        set_error(error_out, error_out_size, "OpenH264 frame encode failed");
        return -1;
    }
    if (info.eFrameType == videoFrameTypeSkip || info.iFrameSizeInBytes <= 0) return 0;
    bool has_sps = false, has_pps = false, has_idr = false;
    uint32_t encoded_size = 0u;
    if (linux_append_openh264_layers_avcc(&info, frame_out, frame_capacity,
                                          &encoded_size, &has_sps,
                                          &has_pps, &has_idr) != 0) {
        set_error(error_out, error_out_size, "OpenH264 frame output invalid");
        return -1;
    }
    bool keyframe = info.eFrameType == videoFrameTypeIDR || has_idr;
    if (keyframe && (!has_sps || !has_pps)) {
        if (encoder->parameter_sets_avcc_size > frame_capacity ||
            encoded_size > frame_capacity - encoder->parameter_sets_avcc_size) {
            set_error(error_out, error_out_size, "OpenH264 keyframe output too large");
            return -1;
        }
        memmove(frame_out + encoder->parameter_sets_avcc_size, frame_out, encoded_size);
        memcpy(frame_out, encoder->parameter_sets_avcc,
               encoder->parameter_sets_avcc_size);
        encoded_size += (uint32_t)encoder->parameter_sets_avcc_size;
    }
    if (!encoder->config_sent) {
        size_t config_size = 8u + encoder->sps_size + encoder->pps_size;
        if (!config_out || config_size > config_capacity) {
            set_error(error_out, error_out_size, "OpenH264 configuration buffer too small");
            return -1;
        }
        linux_put_be16(config_out, encoder->width);
        linux_put_be16(config_out + 2u, encoder->height);
        linux_put_be16(config_out + 4u, (uint16_t)encoder->sps_size);
        linux_put_be16(config_out + 6u, (uint16_t)encoder->pps_size);
        memcpy(config_out + 8u, encoder->sps, encoder->sps_size);
        memcpy(config_out + 8u + encoder->sps_size,
               encoder->pps, encoder->pps_size);
        *config_size_out = (uint32_t)config_size;
        encoder->config_sent = true;
    }
    *frame_size_out = encoded_size;
    *keyframe_out = keyframe;
    return 1;
}

IntegralH264Decoder *integral_h264_decoder_create(const uint8_t *config,
                                        size_t config_size,
                                        char *error_out,
                                        size_t error_out_size)
{
    if (!config || config_size < 10u) {
        set_error(error_out, error_out_size, "OpenH264 configuration payload invalid");
        return NULL;
    }
    uint16_t width = linux_get_be16(config);
    uint16_t height = linux_get_be16(config + 2u);
    uint16_t sps_size = linux_get_be16(config + 4u);
    uint16_t pps_size = linux_get_be16(config + 6u);
    if (!width || !height || (width & 1u) || (height & 1u) ||
        !sps_size || !pps_size ||
        8u + (size_t)sps_size + pps_size != config_size) {
        set_error(error_out, error_out_size, "OpenH264 configuration payload invalid");
        return NULL;
    }
    IntegralH264Decoder *decoder = calloc(1, sizeof(*decoder));
    if (!decoder) {
        set_error(error_out, error_out_size, "OpenH264 decoder allocation failed");
        return NULL;
    }
    decoder->width = width;
    decoder->height = height;
    decoder->parameter_sets_annexb_size = 8u + sps_size + pps_size;
    decoder->parameter_sets_annexb = malloc(decoder->parameter_sets_annexb_size);
    if (!decoder->parameter_sets_annexb ||
        WelsCreateDecoder(&decoder->codec) != 0 || !decoder->codec) {
        set_error(error_out, error_out_size, "OpenH264 decoder creation failed");
        integral_h264_decoder_destroy(decoder);
        return NULL;
    }
    uint8_t *cursor = decoder->parameter_sets_annexb;
    memset(cursor, 0, 3u); cursor[3] = 1u; cursor += 4u;
    memcpy(cursor, config + 8u, sps_size); cursor += sps_size;
    memset(cursor, 0, 3u); cursor[3] = 1u; cursor += 4u;
    memcpy(cursor, config + 8u + sps_size, pps_size);
    SDecodingParam parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.uiTargetDqLayer = UCHAR_MAX;
    parameters.eEcActiveIdc = ERROR_CON_DISABLE;
    parameters.sVideoProperty.size = sizeof(parameters.sVideoProperty);
    parameters.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    if ((*decoder->codec)->Initialize(decoder->codec, &parameters) != cmResultSuccess) {
        set_error(error_out, error_out_size, "OpenH264 decoder configuration failed");
        integral_h264_decoder_destroy(decoder);
        return NULL;
    }
    int trace_level = WELS_LOG_ERROR;
    if ((*decoder->codec)->SetOption(decoder->codec,
                                    DECODER_OPTION_TRACE_LEVEL,
                                    &trace_level) != cmResultSuccess) {
        set_error(error_out, error_out_size, "OpenH264 decoder option failed");
        integral_h264_decoder_destroy(decoder);
        return NULL;
    }
    return decoder;
}

void integral_h264_decoder_destroy(IntegralH264Decoder *decoder)
{
    if (!decoder) return;
    if (decoder->codec) {
        (void)(*decoder->codec)->Uninitialize(decoder->codec);
        WelsDestroyDecoder(decoder->codec);
    }
    free(decoder->parameter_sets_annexb);
    free(decoder);
}

static int linux_avcc_to_annexb(const uint8_t *input,
                                size_t input_size,
                                uint8_t *output,
                                size_t capacity,
                                size_t *output_size)
{
    size_t read = 0u, written = 0u;
    while (read + 4u <= input_size) {
        uint32_t nal_size = linux_get_be32(input + read);
        read += 4u;
        if (!nal_size || read + nal_size > input_size ||
            written + 4u + nal_size > capacity) return -1;
        memset(output + written, 0, 3u);
        output[written + 3u] = 1u;
        memcpy(output + written + 4u, input + read, nal_size);
        written += 4u + nal_size;
        read += nal_size;
    }
    if (read != input_size || !written) return -1;
    *output_size = written;
    return 0;
}

static void linux_i420_to_rgb24(uint8_t *const planes[3],
                                const int strides[2],
                                uint8_t *rgb,
                                uint16_t width,
                                uint16_t height)
{
    for (unsigned y = 0; y < height; y++) {
        const uint8_t *y_row = planes[0] + (size_t)y * strides[0];
        const uint8_t *u_row = planes[1] + (size_t)(y / 2u) * strides[1];
        const uint8_t *v_row = planes[2] + (size_t)(y / 2u) * strides[1];
        uint8_t *destination = rgb + (size_t)y * width * 3u;
        for (unsigned x = 0; x < width; x++) {
            int yy = (int)y_row[x] - 16;
            int u = (int)u_row[x / 2u] - 128;
            int v = (int)v_row[x / 2u] - 128;
            int c = yy < 0 ? 0 : 298 * yy;
            destination[x * 3u] = linux_clamp_byte((c + 409 * v + 128) >> 8);
            destination[x * 3u + 1u] =
                linux_clamp_byte((c - 100 * u - 208 * v + 128) >> 8);
            destination[x * 3u + 2u] = linux_clamp_byte((c + 516 * u + 128) >> 8);
        }
    }
}

int integral_h264_decoder_decode_avcc(IntegralH264Decoder *decoder,
                                 const uint8_t *frame,
                                 size_t frame_size,
                                 uint64_t timestamp_us,
                                 uint8_t *rgb_out,
                                 size_t rgb_capacity,
                                 uint16_t *width_out,
                                 uint16_t *height_out,
                                 char *error_out,
                                 size_t error_out_size)
{
    if (!decoder || !frame || !frame_size || !rgb_out ||
        !width_out || !height_out || frame_size > INT_MAX) return -1;
    size_t required_rgb = (size_t)decoder->width * decoder->height * 3u;
    size_t annex_capacity = decoder->parameter_sets_annexb_size + frame_size + 64u;
    if (required_rgb > rgb_capacity || annex_capacity > INT_MAX) {
        set_error(error_out, error_out_size, "OpenH264 decoded frame buffer too small");
        return -1;
    }
    uint8_t *annexb = malloc(annex_capacity);
    if (!annexb) {
        set_error(error_out, error_out_size, "OpenH264 frame allocation failed");
        return -1;
    }
    memcpy(annexb, decoder->parameter_sets_annexb,
           decoder->parameter_sets_annexb_size);
    size_t annexb_size = 0u;
    if (linux_avcc_to_annexb(frame, frame_size,
                             annexb + decoder->parameter_sets_annexb_size,
                             annex_capacity - decoder->parameter_sets_annexb_size,
                             &annexb_size) != 0) {
        free(annexb);
        set_error(error_out, error_out_size, "OpenH264 AVCC sample invalid");
        return -1;
    }
    annexb_size += decoder->parameter_sets_annexb_size;
    uint8_t *planes[3] = {NULL, NULL, NULL};
    SBufferInfo info;
    memset(&info, 0, sizeof(info));
    info.uiInBsTimeStamp = timestamp_us / 1000u;
    DECODING_STATE status = (*decoder->codec)->DecodeFrameNoDelay(
        decoder->codec, annexb, (int)annexb_size, planes, &info);
    free(annexb);
    if (status != dsErrorFree) {
        if (error_out && error_out_size) {
            snprintf(error_out, error_out_size,
                     "OpenH264 frame decode failed 0x%04x", (unsigned)status);
        }
        return -1;
    }
    if (!info.iBufferStatus) return 0;
    int width = info.UsrData.sSystemBuffer.iWidth;
    int height = info.UsrData.sSystemBuffer.iHeight;
    if (!planes[0] || !planes[1] || !planes[2] ||
        width != decoder->width || height != decoder->height ||
        info.UsrData.sSystemBuffer.iStride[0] < width ||
        info.UsrData.sSystemBuffer.iStride[1] < width / 2) {
        set_error(error_out, error_out_size, "OpenH264 decoded frame layout invalid");
        return -1;
    }
    linux_i420_to_rgb24(planes, info.UsrData.sSystemBuffer.iStride,
                        rgb_out, decoder->width, decoder->height);
    *width_out = decoder->width;
    *height_out = decoder->height;
    return 1;
}

#else

struct IntegralH264Encoder { int unused; };
struct IntegralH264Decoder { int unused; };

IntegralH264Encoder *integral_h264_encoder_create(uint16_t width, uint16_t height, char *error_out, size_t error_out_size)
{ (void)width; (void)height; set_error(error_out, error_out_size, "H264 Media Foundation is available on Windows only"); return NULL; }
void integral_h264_encoder_destroy(IntegralH264Encoder *encoder) { (void)encoder; }
int integral_h264_encoder_encode_rgb24(IntegralH264Encoder *encoder, const uint8_t *rgb, bool bottom_up, uint64_t timestamp_us, uint8_t *config_out, size_t config_capacity, uint32_t *config_size_out, uint8_t *frame_out, size_t frame_capacity, uint32_t *frame_size_out, bool *keyframe_out, char *error_out, size_t error_out_size)
{ (void)encoder; (void)rgb; (void)bottom_up; (void)timestamp_us; (void)config_out; (void)config_capacity; (void)config_size_out; (void)frame_out; (void)frame_capacity; (void)frame_size_out; (void)keyframe_out; set_error(error_out, error_out_size, "H264 Media Foundation is available on Windows only"); return -1; }
IntegralH264Decoder *integral_h264_decoder_create(const uint8_t *config, size_t config_size, char *error_out, size_t error_out_size)
{ (void)config; (void)config_size; set_error(error_out, error_out_size, "H264 Media Foundation is available on Windows only"); return NULL; }
void integral_h264_decoder_destroy(IntegralH264Decoder *decoder) { (void)decoder; }
int integral_h264_decoder_decode_avcc(IntegralH264Decoder *decoder, const uint8_t *frame, size_t frame_size, uint64_t timestamp_us, uint8_t *rgb_out, size_t rgb_capacity, uint16_t *width_out, uint16_t *height_out, char *error_out, size_t error_out_size)
{ (void)decoder; (void)frame; (void)frame_size; (void)timestamp_us; (void)rgb_out; (void)rgb_capacity; (void)width_out; (void)height_out; set_error(error_out, error_out_size, "H264 Media Foundation is available on Windows only"); return -1; }

#endif
