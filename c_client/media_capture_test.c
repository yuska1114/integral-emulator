/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "n64_runtime_media_stream.c"
#include <assert.h>

static void test_remote_resize(void)
{
    for (unsigned scale = 0; scale <= 2; scale += 2) {
        IntegralN64RuntimeMediaStream *s = integral_n64_runtime_media_stream_create(NULL, NULL);
        assert(s);
        integral_n64_runtime_media_stream_set_window_scale(s, scale);
        s->video_renderer_driver_requested[0] = '\0';
        s->video_vsync_enabled = false;
        const unsigned sizes[][2] = {{640,480},{800,600},{320,240},{640,480}};
        int original_w = 0, original_h = 0;
        char error[160];
        for (unsigned i = 0; i < 4; ++i) {
            memset(s->rgb, 96, sizes[i][0] * sizes[i][1] * 3u);
            s->decoded_width = sizes[i][0]; s->decoded_height = sizes[i][1];
            s->decoded_frame_sequence++;
            assert(sync_remote_video_frame(s, error, sizeof(error)) == 1);
            int w, h;
            SDL_GetWindowSize(s->video_window, &w, &h);
            if (!i) {
                SDL_Rect bounds;
                assert(SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(s->video_window), &bounds) == 0);
                unsigned resolved = integral_display_scale_resolve(scale, bounds.w, bounds.h, 640, 480);
                assert(w == (int)(640 * resolved) && h == (int)(480 * resolved));
                SDL_SetWindowSize(s->video_window, 733, 517); /* Remote user's resize. */
                SDL_GetWindowSize(s->video_window, &original_w, &original_h);
            } else assert(w == original_w && h == original_h);
            assert(integral_n64_runtime_media_stream_render(s, NULL, NULL));
        }
        integral_n64_runtime_media_stream_destroy(s);
    }
    puts("Remote AUTO/manual initial scale and user size survive texture changes PASS");
}

static void test_host_resize_encoder(void)
{
    IntegralN64RuntimeMediaStream *s = integral_n64_runtime_media_stream_create(NULL, NULL);
    assert(s);
    uint8_t *frame = malloc(INTEGRAL_N64_RUNTIME_STREAM_BYTES);
    assert(frame);
    IntegralH264Encoder *encoder = NULL;
    IntegralH264Decoder *decoder = NULL;
    uint8_t config[INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES];
    uint32_t config_size = 0;
    unsigned decoded = 0;
    const unsigned sizes[][2] = {{640,480},{1280,960},{960,540},{301,601},{641,479},{1920,1080}};
    char error[160] = {0};
    for (unsigned i = 0; i < 18; ++i) {
        unsigned w = sizes[i % 6][0], h = sizes[i % 6][1];
        uint8_t *raw = malloc((size_t)w*h*3);
        assert(raw);
        memset(raw, 128, (size_t)w*h*3);
        assert(integral_n64_runtime_scale_stream_frame(raw,w,h,w*3,frame) == 0);
        free(raw);
        assert(integral_n64_runtime_media_stream_submit_host_rgb24(s, frame, 640, 480,
            false, (i+1)*33333u, error, sizeof(error)) == 1);
        SDL_LockMutex(s->host_encode_mutex);
        Uint64 deadline = SDL_GetTicks64()+5000;
        while (s->host_encode_metrics.encode_samples <= i && !s->host_encode_failed && SDL_GetTicks64()<deadline)
            SDL_CondWaitTimeout(s->host_encode_condition,s->host_encode_mutex,100);
        assert(!s->host_encode_failed && s->host_encode_metrics.encode_samples == i+1);
        assert(s->encoder_width == 640 && s->encoder_height == 480);
        if (!encoder) encoder = s->encoder;
        assert(s->encoder == encoder);
        if (s->host_output_ready) {
            if (s->host_output_config_size) {
                if (!decoder) {
                    config_size = s->host_output_config_size;
                    memcpy(config,s->host_output_config,config_size);
                    decoder = integral_h264_decoder_create(config,config_size,error,sizeof(error));
                    assert(decoder);
                } else assert(config_size == s->host_output_config_size &&
                              !memcmp(config,s->host_output_config,config_size));
            }
            assert(decoder);
            uint16_t dw=0, dh=0;
            int got = integral_h264_decoder_decode_avcc(decoder,s->host_encoded+8,
                s->host_output_frame_size-8,(i+1)*33333u,s->decode_rgb,RGB_CAPACITY,
                &dw,&dh,error,sizeof(error));
            assert(got >= 0);
            if (got) { assert(dw==640 && dh==480); decoded++; }
            s->host_output_ready = false;
            s->host_output_config_size = 0;
        }
        SDL_CondSignal(s->host_encode_condition);
        SDL_UnlockMutex(s->host_encode_mutex);
    }
    assert(decoded > 0);
    integral_h264_decoder_destroy(decoder);
    integral_n64_runtime_media_stream_destroy(s);
    free(frame);
    puts("Repeated Host resize: fixed resolution, one encoder/decoder, stable H264 config PASS");
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IntegralN64RuntimeMediaStream stream = {0};
    assert(!integral_n64_runtime_media_stream_save_screenshot(&stream, "n64_room"));
    assert(!strcmp(stream.screenshot_notice, "SCREENSHOT FAILED"));
    stream.decode_mutex = SDL_CreateMutex();
    assert(stream.decode_mutex);
    uint8_t rgb[160 * 144 * 3];
    memset(rgb, 90, sizeof(rgb));
    stream.rgb = rgb; stream.decoded_frame_sequence = 1;
    stream.decoded_width = 160; stream.decoded_height = 144;
    assert(integral_n64_runtime_media_stream_save_screenshot(&stream, "n64_room"));
    assert(integral_n64_runtime_media_stream_save_screenshot(&stream, "link_cable"));
    assert(!strcmp(stream.screenshot_notice, "SCREENSHOT SAVED"));
    SDL_DestroyMutex(stream.decode_mutex);
    /* Transport recovery keeps the existing presentation objects and size. */
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    test_remote_resize();
    test_host_resize_encoder();
    IntegralN64RuntimeMediaStream *recovering = integral_n64_runtime_media_stream_create(NULL, NULL);
    assert(recovering);
    recovering->video_window = SDL_CreateWindow("reconnect test", 0, 0, 360, 360, SDL_WINDOW_HIDDEN);
    assert(recovering->video_window);
    recovering->video_window_id = SDL_GetWindowID(recovering->video_window);
    recovering->video_renderer = SDL_CreateRenderer(recovering->video_window, -1, SDL_RENDERER_SOFTWARE);
    assert(recovering->video_renderer);
    recovering->texture = SDL_CreateTexture(recovering->video_renderer,
        SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 160, 144);
    assert(recovering->texture);
    SDL_Window *window = recovering->video_window;
    SDL_Texture *texture = recovering->texture;
    recovering->remote_control_ready = true;
    recovering->decode_failed = true;
    recovering->h264_config_size = 10;
    integral_n64_runtime_media_stream_transport_lost(recovering);
    assert(recovering->video_window == window && recovering->texture == texture);
    assert(recovering->video_window_id == SDL_GetWindowID(window));
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    assert(width == 360 && height == 360);
    assert(!recovering->remote_control_ready && !recovering->decode_failed);
    assert(!recovering->h264_config_size && !recovering->encoder && !recovering->decoder);
    integral_n64_runtime_media_stream_destroy(recovering);
    SDL_Quit();
    puts("Both Remote received-frame capture routes PASS");
    return 0;
}
