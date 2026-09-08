/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_remote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "joypad.h"
#include "protocol.h"

#define LAN_REMOTE_REQUEST_MAX 2048u
#define LAN_REMOTE_BMP_HEADER_SIZE 54u
#define LAN_REMOTE_BMP_ROW_SIZE (INTEGRAL_GB_RUNTIME_GB_WIDTH * 3u)
#define LAN_REMOTE_BMP_PIXEL_SIZE (LAN_REMOTE_BMP_ROW_SIZE * INTEGRAL_GB_RUNTIME_GB_HEIGHT)
#define LAN_REMOTE_BMP_SIZE (LAN_REMOTE_BMP_HEADER_SIZE + LAN_REMOTE_BMP_PIXEL_SIZE)
#define LAN_REMOTE_RGB565_SIZE INTEGRAL_GB_RUNTIME_VIDEO_PAYLOAD_SIZE
#define LAN_REMOTE_FRAME_INTERVAL_US 33333u
#define LAN_REMOTE_WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static const char LAN_REMOTE_HTML[] =
"<!doctype html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover\">"
"<title>INTEGRAL EMULATOR GB Runtime Remote</title>"
"<style>"
".wrap{height:100dvh;min-height:100svh;display:grid;grid-template-rows:auto auto minmax(0,1fr);padding:calc(env(safe-area-inset-top,0px) + 8px) clamp(8px,3vw,14px) calc(env(safe-area-inset-bottom,0px) + 8px);gap:clamp(6px,1.6dvh,10px);overflow:hidden}"
"*{box-sizing:border-box;touch-action:none;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;-webkit-tap-highlight-color:transparent}html,body{height:100%;overflow:hidden;-webkit-text-size-adjust:100%}body{margin:0;background:#14181c;color:#eee;font-family:-apple-system,BlinkMacSystemFont,sans-serif;overscroll-behavior:none}"
".title{font-size:clamp(12px,3.7vw,15px);line-height:1;font-weight:800;color:#dfe7ee;letter-spacing:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.screenbox{position:relative;width:min(78vw,clamp(250px,calc((100dvh - 300px)*10/9),304px));margin:0 auto;background:#050608;border:4px solid #2a3138;aspect-ratio:10/9}.screen{display:block;width:100%;height:100%;image-rendering:pixelated}.pause{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);padding:5px 10px;border-radius:6px;background:rgba(20,24,28,.82);color:#fff;font-size:clamp(13px,3.8vw,16px);font-weight:900;letter-spacing:0;opacity:0;pointer-events:none}.screenbox.paused .pause{opacity:1}"
".pad{--d:clamp(48px,14.4vw,64px);--round:clamp(58px,17.4vw,76px);--pillw:clamp(72px,20vw,90px);--pillh:clamp(36px,10.5vw,44px);display:grid;grid-template-columns:calc(var(--d)*3) minmax(calc(var(--pillw)*2 + 10px),1fr);gap:clamp(6px,2vw,12px);align-self:start;align-items:end;justify-content:center;min-height:0;width:100%;max-width:560px;margin:clamp(8px,1.8dvh,14px) auto 0;overflow:hidden}"
".dpad{display:grid;grid-template-columns:var(--d) var(--d) var(--d);grid-template-rows:var(--d) var(--d) var(--d);justify-content:center}.btn{border:0;border-radius:12px;background:#2a483f;color:#fff;font-weight:800;font-size:clamp(18px,5vw,22px);box-shadow:inset 0 -4px 0 #162822}"
".btn:active,.btn.on{background:#56a27e}.up{grid-column:2}.left{grid-column:1;grid-row:2}.right{grid-column:3;grid-row:2}.down{grid-column:2;grid-row:3}"
".actions{display:grid;gap:clamp(14px,3.2dvh,22px);justify-items:center;align-self:end;min-width:0}.play{position:relative;padding-top:clamp(24px,6.4vw,34px);padding-left:clamp(34px,9vw,46px)}.ab{display:flex;justify-content:center;gap:clamp(8px,2.4vw,14px)}.round{width:var(--round);height:var(--round);border-radius:50%;background:#8b3542}.round.on{background:#d65364}.fast{position:absolute;left:0;top:0;width:clamp(34px,9.6vw,42px);height:clamp(30px,8.8vw,38px);border-radius:8px;background:#31404a;font-size:clamp(15px,4vw,18px)}.fast.on{background:#c79a37}"
".sys{display:flex;justify-content:center;gap:clamp(8px,2vw,10px)}.pill{min-width:var(--pillw);height:var(--pillh);border-radius:22px;background:#28313a;font-size:clamp(12px,3.4vw,14px)}"
"</style></head><body><div class=\"wrap\"><div class=\"title\">FAMILY GBC LAN REMOTE CONTROLLER</div>"
"<div id=\"screenbox\" class=\"screenbox\"><canvas id=\"screen\" class=\"screen\" width=\"160\" height=\"144\"></canvas><div class=\"pause\">PAUSE</div></div>"
"<div class=\"pad\"><div class=\"dpad\">"
"<button class=\"btn up\" data-b=\"4\">▲</button><button class=\"btn left\" data-b=\"2\">◀</button><button class=\"btn right\" data-b=\"1\">▶</button><button class=\"btn down\" data-b=\"8\">▼</button>"
"</div><div class=\"actions\"><div class=\"play\"><button class=\"btn fast\" data-fast=\"1\">F</button><div class=\"ab\"><button class=\"btn round\" data-b=\"32\">B</button><button class=\"btn round\" data-b=\"16\">A</button></div></div>"
"<div class=\"sys\"><button class=\"btn pill\" data-b=\"64\">SELECT</button><button class=\"btn pill\" data-b=\"128\">START</button></div></div></div></div>"
"<script>"
"let mask=0,fast=0,paused=0,ws=null;const canvas=document.getElementById('screen'),screenbox=document.getElementById('screenbox'),ctx=canvas.getContext('2d'),img=ctx.createImageData(160,144);"
"let audioCtx=null,audioAt=0;function audioStart(){if(!audioCtx){audioCtx=new (window.AudioContext||window.webkitAudioContext)({sampleRate:48000});audioAt=audioCtx.currentTime+0.08;}if(audioCtx.state==='suspended')audioCtx.resume();}"
"function draw(d){if(d.length!==46080)return;for(let i=0,j=0;i<23040;i++,j+=4){const v=(d[i*2]<<8)|d[i*2+1],r5=(v>>11)&31,g6=(v>>5)&63,b5=v&31;img.data[j]=(r5<<3)|(r5>>2);img.data[j+1]=(g6<<2)|(g6>>4);img.data[j+2]=(b5<<3)|(b5>>2);img.data[j+3]=255;}ctx.putImageData(img,0,0);}"
"function play(d){if(!audioCtx||d.length<4)return;const frames=(d[1]<<8)|d[2],need=3+frames*4;if(d.length!==need)return;const b=audioCtx.createBuffer(2,frames,48000),l=b.getChannelData(0),r=b.getChannelData(1);for(let i=0,p=3;i<frames;i++,p+=4){let lv=d[p]|(d[p+1]<<8),rv=d[p+2]|(d[p+3]<<8);if(lv&32768)lv-=65536;if(rv&32768)rv-=65536;l[i]=lv/32768;r[i]=rv/32768;}const s=audioCtx.createBufferSource();s.buffer=b;s.connect(audioCtx.destination);const now=audioCtx.currentTime;if(audioAt<now+0.03||audioAt>now+0.25)audioAt=now+0.08;s.start(audioAt);audioAt+=frames/48000;}"
"function route(d){if(d.length<1)return;if(d[0]===86)draw(d.subarray(1));else if(d[0]===65)play(d);}"
"function openWs(){ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');ws.binaryType='arraybuffer';ws.onmessage=e=>route(new Uint8Array(e.data));ws.onclose=()=>setTimeout(openWs,500);ws.onerror=()=>{try{ws.close()}catch(e){}};}openWs();"
"function send(){if(ws&&ws.readyState===1){ws.send(new Uint8Array([mask,fast,paused]));}else{fetch('/input?buttons='+mask+'&fast='+fast+'&pause='+paused,{method:'POST',cache:'no-store'}).catch(()=>{});}}"
"function setBtn(el,on){let b=Number(el.dataset.b);if(on){mask|=b;el.classList.add('on')}else{mask&=~b;el.classList.remove('on')}send()}"
"document.querySelectorAll('[data-b]').forEach(el=>{el.addEventListener('pointerdown',e=>{e.preventDefault();audioStart();el.setPointerCapture(e.pointerId);setBtn(el,true)});el.addEventListener('pointerup',e=>{e.preventDefault();setBtn(el,false)});el.addEventListener('pointercancel',e=>{setBtn(el,false)});el.addEventListener('pointerleave',e=>{if(e.buttons===0)setBtn(el,false)});});"
"document.querySelectorAll('[data-fast]').forEach(el=>{el.addEventListener('pointerdown',e=>{e.preventDefault();audioStart();fast=fast?0:1;el.classList.toggle('on',!!fast);send();});});"
"screenbox.addEventListener('pointerdown',e=>{e.preventDefault();audioStart();paused=paused?0:1;screenbox.classList.toggle('paused',!!paused);mask=0;document.querySelectorAll('[data-b].on').forEach(el=>el.classList.remove('on'));send();});"
"['contextmenu','selectstart','dragstart','gesturestart','touchstart','touchmove','touchend','touchcancel','dblclick'].forEach(t=>document.addEventListener(t,e=>e.preventDefault(),{passive:false}));"
"window.addEventListener('blur',()=>{mask=0;fast=0;document.querySelectorAll('.on').forEach(e=>e.classList.remove('on'));send();});"
"</script></body></html>";

typedef struct Sha1State {
    uint32_t h[5];
    uint64_t length;
    uint8_t block[64];
    size_t block_size;
} Sha1State;

static uint32_t rol32(uint32_t value, unsigned bits)
{
    return (value << bits) | (value >> (32u - bits));
}

static uint32_t load_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void store_be32(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value >> 24);
    dest[1] = (uint8_t)(value >> 16);
    dest[2] = (uint8_t)(value >> 8);
    dest[3] = (uint8_t)value;
}

static void store_be64(uint8_t *dest, uint64_t value)
{
    for (unsigned i = 0; i < 8; i++) {
        dest[7u - i] = (uint8_t)(value >> (i * 8u));
    }
}

static void sha1_transform(Sha1State *state, const uint8_t block[64])
{
    uint32_t w[80];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = load_be32(block + i * 4u);
    }
    for (unsigned i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1);
    }

    uint32_t a = state->h[0];
    uint32_t b = state->h[1];
    uint32_t c = state->h[2];
    uint32_t d = state->h[3];
    uint32_t e = state->h[4];
    for (unsigned i = 0; i < 80; i++) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        }
        else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t temp = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }
    state->h[0] += a;
    state->h[1] += b;
    state->h[2] += c;
    state->h[3] += d;
    state->h[4] += e;
}

static void sha1_init(Sha1State *state)
{
    state->h[0] = 0x67452301u;
    state->h[1] = 0xEFCDAB89u;
    state->h[2] = 0x98BADCFEu;
    state->h[3] = 0x10325476u;
    state->h[4] = 0xC3D2E1F0u;
    state->length = 0;
    state->block_size = 0;
}

static void sha1_update(Sha1State *state, const uint8_t *data, size_t size)
{
    state->length += (uint64_t)size * 8u;
    while (size > 0) {
        size_t available = sizeof(state->block) - state->block_size;
        size_t chunk = size < available ? size : available;
        memcpy(state->block + state->block_size, data, chunk);
        state->block_size += chunk;
        data += chunk;
        size -= chunk;
        if (state->block_size == sizeof(state->block)) {
            sha1_transform(state, state->block);
            state->block_size = 0;
        }
    }
}

static void sha1_finish(Sha1State *state, uint8_t digest[20])
{
    uint64_t bit_length = state->length;
    state->block[state->block_size++] = 0x80u;
    if (state->block_size > 56u) {
        while (state->block_size < sizeof(state->block)) {
            state->block[state->block_size++] = 0;
        }
        sha1_transform(state, state->block);
        state->block_size = 0;
    }
    while (state->block_size < 56u) {
        state->block[state->block_size++] = 0;
    }
    store_be64(state->block + 56, bit_length);
    sha1_transform(state, state->block);
    for (unsigned i = 0; i < 5; i++) {
        store_be32(digest + i * 4u, state->h[i]);
    }
}

static bool base64_encode(const uint8_t *src, size_t size, char *dest, size_t dest_size)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t required = ((size + 2u) / 3u) * 4u + 1u;
    if (dest_size < required) {
        return false;
    }
    size_t out = 0;
    for (size_t i = 0; i < size; i += 3u) {
        uint32_t value = (uint32_t)src[i] << 16;
        bool have_second = i + 1u < size;
        bool have_third = i + 2u < size;
        if (have_second) {
            value |= (uint32_t)src[i + 1u] << 8;
        }
        if (have_third) {
            value |= (uint32_t)src[i + 2u];
        }
        dest[out++] = alphabet[(value >> 18) & 0x3Fu];
        dest[out++] = alphabet[(value >> 12) & 0x3Fu];
        dest[out++] = have_second ? alphabet[(value >> 6) & 0x3Fu] : '=';
        dest[out++] = have_third ? alphabet[value & 0x3Fu] : '=';
    }
    dest[out] = '\0';
    return true;
}

static void write_response(integral_gb_runtime_socket_t fd,
                           const char *status,
                           const char *content_type,
                           const char *body)
{
    char header[256];
    size_t body_len = strlen(body);
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %lu\r\n"
                              "Connection: close\r\n"
                              "Cache-Control: no-store\r\n"
                              "\r\n",
                              status,
                              content_type,
                              (unsigned long)body_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        return;
    }
    (void)integral_gb_runtime_socket_write(fd, header, (size_t)header_len);
    (void)integral_gb_runtime_socket_write(fd, body, body_len);
}

static void write_binary_response(integral_gb_runtime_socket_t fd,
                                  const char *status,
                                  const char *content_type,
                                  const uint8_t *body,
                                  size_t body_len)
{
    char header[256];
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %lu\r\n"
                              "Connection: close\r\n"
                              "Cache-Control: no-store\r\n"
                              "\r\n",
                              status,
                              content_type,
                              (unsigned long)body_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        return;
    }
    (void)integral_gb_runtime_socket_write(fd, header, (size_t)header_len);
    (void)integral_gb_runtime_socket_write(fd, body, body_len);
}

static void close_websocket(IntegralGBRuntimeLanRemote *remote)
{
    if (!remote || !INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(remote->websocket)) {
        return;
    }
    (void)integral_gb_runtime_socket_close(remote->websocket);
    remote->websocket = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    remote->websocket_out_size = 0;
    remote->websocket_out_sent = 0;
    remote->websocket_sent_frame_seq = remote->frame_seq;
    remote->buttons = 0;
    remote->fast_enabled = false;
    remote->paused = false;
}

static bool request_header_value(const char *request,
                                 const char *name,
                                 char *dest,
                                 size_t dest_size)
{
    const char *cursor = request;
    size_t name_len = strlen(name);
    while (*cursor != '\0') {
        const char *line_end = strstr(cursor, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        if (line_len > name_len + 1u &&
            strncasecmp(cursor, name, name_len) == 0 &&
            cursor[name_len] == ':') {
            const char *value = cursor + name_len + 1u;
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            size_t value_len = line_len - (size_t)(value - cursor);
            while (value_len > 0 &&
                   (value[value_len - 1u] == ' ' || value[value_len - 1u] == '\t')) {
                value_len--;
            }
            if (value_len >= dest_size) {
                return false;
            }
            memcpy(dest, value, value_len);
            dest[value_len] = '\0';
            return true;
        }
        if (!line_end) {
            break;
        }
        cursor = line_end + 2u;
    }
    return false;
}

static bool build_websocket_accept(const char *key, char *dest, size_t dest_size)
{
    char source[128];
    size_t key_len = strlen(key);
    size_t guid_len = strlen(LAN_REMOTE_WEBSOCKET_GUID);
    if (key_len + guid_len >= sizeof(source)) {
        return false;
    }
    memcpy(source, key, key_len);
    memcpy(source + key_len, LAN_REMOTE_WEBSOCKET_GUID, guid_len + 1u);

    Sha1State sha1;
    uint8_t digest[20];
    sha1_init(&sha1);
    sha1_update(&sha1, (const uint8_t *)source, key_len + guid_len);
    sha1_finish(&sha1, digest);
    return base64_encode(digest, sizeof(digest), dest, dest_size);
}

static bool websocket_handshake(IntegralGBRuntimeLanRemote *remote, integral_gb_runtime_socket_t fd, const char *request)
{
    char key[96];
    char accept[64];
    char response[256];
    if (!request_header_value(request, "Sec-WebSocket-Key", key, sizeof(key)) ||
        !build_websocket_accept(key, accept, sizeof(accept))) {
        write_response(fd, "400 Bad Request", "text/plain", "bad websocket\n");
        return false;
    }
    int response_len = snprintf(response,
                                sizeof(response),
                                "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: %s\r\n"
                                "\r\n",
                                accept);
    if (response_len < 0 || (size_t)response_len >= sizeof(response)) {
        return false;
    }
    (void)integral_gb_runtime_socket_write(fd, response, (size_t)response_len);
    close_websocket(remote);
    remote->websocket = fd;
    remote->websocket_sent_frame_seq = remote->frame_seq;
    return true;
}

static void put_u16_le(uint8_t *dest, uint16_t value)
{
    dest[0] = (uint8_t)(value & 0xFFu);
    dest[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value & 0xFFu);
    dest[1] = (uint8_t)((value >> 8) & 0xFFu);
    dest[2] = (uint8_t)((value >> 16) & 0xFFu);
    dest[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void build_frame_bmp(const IntegralGBRuntimeSlot *slot, uint8_t *bmp)
{
    memset(bmp, 0, LAN_REMOTE_BMP_SIZE);
    bmp[0] = 'B';
    bmp[1] = 'M';
    put_u32_le(bmp + 2, LAN_REMOTE_BMP_SIZE);
    put_u32_le(bmp + 10, LAN_REMOTE_BMP_HEADER_SIZE);
    put_u32_le(bmp + 14, 40u);
    put_u32_le(bmp + 18, INTEGRAL_GB_RUNTIME_GB_WIDTH);
    put_u32_le(bmp + 22, INTEGRAL_GB_RUNTIME_GB_HEIGHT);
    put_u16_le(bmp + 26, 1u);
    put_u16_le(bmp + 28, 24u);
    put_u32_le(bmp + 34, LAN_REMOTE_BMP_PIXEL_SIZE);

    uint8_t *pixels = bmp + LAN_REMOTE_BMP_HEADER_SIZE;
    for (unsigned y = 0; y < INTEGRAL_GB_RUNTIME_GB_HEIGHT; y++) {
        unsigned src_y = INTEGRAL_GB_RUNTIME_GB_HEIGHT - 1u - y;
        for (unsigned x = 0; x < INTEGRAL_GB_RUNTIME_GB_WIDTH; x++) {
            uint32_t argb = slot->pixels[src_y * INTEGRAL_GB_RUNTIME_GB_WIDTH + x];
            uint8_t *dest = pixels + y * LAN_REMOTE_BMP_ROW_SIZE + x * 3u;
            dest[0] = (uint8_t)(argb & 0xFFu);
            dest[1] = (uint8_t)((argb >> 8) & 0xFFu);
            dest[2] = (uint8_t)((argb >> 16) & 0xFFu);
        }
    }
}

static void build_frame_rgb565(const IntegralGBRuntimeSlot *slot, uint8_t *payload)
{
    for (unsigned i = 0; i < INTEGRAL_GB_RUNTIME_GB_WIDTH * INTEGRAL_GB_RUNTIME_GB_HEIGHT; i++) {
        uint16_t pixel = integral_gb_runtime_rgb888_to_rgb565(slot->pixels[i]);
        payload[i * 2u] = (uint8_t)(pixel >> 8);
        payload[i * 2u + 1u] = (uint8_t)pixel;
    }
}

static bool frame_rgb565_ready(const IntegralGBRuntimeLanRemote *remote)
{
    return remote && remote->open && remote->frame_ready;
}

static bool parse_buttons_query(const char *request, uint8_t *buttons)
{
    const char *needle = "buttons=";
    const char *start = strstr(request, needle);
    if (!start) {
        return false;
    }
    start += strlen(needle);
    char *end = NULL;
    unsigned long value = strtoul(start, &end, 10);
    if (end == start || value > 255ul) {
        return false;
    }
    *buttons = (uint8_t)value;
    return true;
}

static bool parse_bool_query(const char *request, const char *name)
{
    const char *start = strstr(request, name);
    if (!start) {
        return false;
    }
    start += strlen(name);
    char *end = NULL;
    unsigned long value = strtoul(start, &end, 10);
    return end != start && value != 0ul;
}

static bool handle_request(IntegralGBRuntimeLanRemote *remote, integral_gb_runtime_socket_t fd, const IntegralGBRuntimeSlot *slot)
{
    char request[LAN_REMOTE_REQUEST_MAX];
    ssize_t n = -1;
    uint64_t started_us = integral_gb_runtime_now_us();
    for (;;) {
        n = integral_gb_runtime_socket_read(fd, request, sizeof(request) - 1u);
        if (n > 0) {
            break;
        }
        if (n == 0) {
            return false;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (!integral_gb_runtime_socket_error_would_block(error_code) ||
            (integral_gb_runtime_now_us() - started_us) > 100000u) {
            return false;
        }
        integral_gb_runtime_sleep_ms(1);
    }
    request[n] = '\0';
    if (strncmp(request, "GET /ws ", 8) == 0) {
        return websocket_handshake(remote, fd, request);
    }
    if (strncmp(request, "POST /input?", 12) == 0) {
        uint8_t buttons = 0;
        if (parse_buttons_query(request, &buttons)) {
            remote->buttons = buttons;
            remote->fast_enabled = parse_bool_query(request, "fast=");
            remote->paused = parse_bool_query(request, "pause=");
            write_response(fd, "204 No Content", "text/plain", "");
            return false;
        }
        write_response(fd, "400 Bad Request", "text/plain", "bad buttons\n");
        return false;
    }
    if (strncmp(request, "GET / ", 6) == 0 ||
        strncmp(request, "GET /?", 6) == 0 ||
        strncmp(request, "GET /index.html", 15) == 0) {
        write_response(fd, "200 OK", "text/html; charset=utf-8", LAN_REMOTE_HTML);
        return false;
    }
    if (strncmp(request, "GET /frame.bmp", 14) == 0) {
        if (!slot || !slot->initialized) {
            write_response(fd, "503 Service Unavailable", "text/plain", "frame unavailable\n");
            return false;
        }
        uint8_t bmp[LAN_REMOTE_BMP_SIZE];
        build_frame_bmp(slot, bmp);
        write_binary_response(fd, "200 OK", "image/bmp", bmp, sizeof(bmp));
        return false;
    }
    if (strncmp(request, "GET /frame.rgb565", 17) == 0) {
        if (!frame_rgb565_ready(remote)) {
            write_response(fd, "503 Service Unavailable", "text/plain", "frame unavailable\n");
            return false;
        }
        (void)slot;
        write_binary_response(fd,
                              "200 OK",
                              "application/octet-stream",
                              remote->frame_rgb565,
                              sizeof(remote->frame_rgb565));
        return false;
    }
    write_response(fd, "404 Not Found", "text/plain", "not found\n");
    return false;
}

static void set_key(IntegralGBRuntimeSlot *slot, GB_key_t key, uint8_t buttons, uint8_t mask)
{
    if (slot && slot->initialized) {
        GB_set_key_state(slot->gb, key, (buttons & mask) != 0);
    }
}

int integral_gb_runtime_lan_remote_open(IntegralGBRuntimeLanRemote *remote, unsigned port)
{
    if (!remote) {
        return -1;
    }
    memset(remote, 0, sizeof(*remote));
    remote->listener = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    remote->websocket = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    remote->port = port;
    if (integral_gb_runtime_net_init() != 0) {
        return -1;
    }
    remote->listener = integral_gb_runtime_tcp_listen_ipv4(port, 8, true);
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(remote->listener)) {
        integral_gb_runtime_net_shutdown();
        return -1;
    }
    if (!integral_gb_runtime_detect_local_ipv4(remote->host, sizeof(remote->host))) {
        (void)snprintf(remote->host, sizeof(remote->host), "127.0.0.1");
    }
    remote->open = true;
    return 0;
}

void integral_gb_runtime_lan_remote_update_frame(IntegralGBRuntimeLanRemote *remote, const IntegralGBRuntimeSlot *slot)
{
    if (!remote || !remote->open || !slot || !slot->initialized) {
        return;
    }
    uint64_t now_us = integral_gb_runtime_now_us();
    if (remote->frame_ready && now_us - remote->last_frame_us < LAN_REMOTE_FRAME_INTERVAL_US) {
        return;
    }
    build_frame_rgb565(slot, remote->frame_rgb565);
    remote->last_frame_us = now_us;
    remote->frame_ready = true;
    remote->frame_seq++;
}

static void websocket_prepare_frame(IntegralGBRuntimeLanRemote *remote)
{
    size_t payload_size = 1u + LAN_REMOTE_RGB565_SIZE;
    remote->websocket_out[0] = 0x82u;
    remote->websocket_out[1] = 126u;
    remote->websocket_out[2] = (uint8_t)(payload_size >> 8);
    remote->websocket_out[3] = (uint8_t)payload_size;
    remote->websocket_out[4] = INTEGRAL_GB_RUNTIME_PACKET_VIDEO;
    memcpy(remote->websocket_out + 5u, remote->frame_rgb565, LAN_REMOTE_RGB565_SIZE);
    remote->websocket_out_size = 4u + payload_size;
    remote->websocket_out_sent = 0;
    remote->websocket_sent_frame_seq = remote->frame_seq;
}

static bool websocket_prepare_audio(IntegralGBRuntimeLanRemote *remote, const int16_t *samples, unsigned frames)
{
    if (!remote || !INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(remote->websocket) || !samples || frames == 0 ||
        remote->websocket_out_size != 0) {
        return false;
    }
    if (frames > INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES) {
        frames = INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES;
    }
    size_t sample_bytes = frames * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS * sizeof(int16_t);
    size_t payload_size = 3u + sample_bytes;
    if (payload_size > 65535u || 4u + payload_size > sizeof(remote->websocket_out)) {
        return false;
    }
    remote->websocket_out[0] = 0x82u;
    remote->websocket_out[1] = 126u;
    remote->websocket_out[2] = (uint8_t)(payload_size >> 8);
    remote->websocket_out[3] = (uint8_t)payload_size;
    remote->websocket_out[4] = INTEGRAL_GB_RUNTIME_PACKET_AUDIO;
    remote->websocket_out[5] = (uint8_t)(frames >> 8);
    remote->websocket_out[6] = (uint8_t)frames;
    uint8_t *dest = remote->websocket_out + 7u;
    for (unsigned i = 0; i < frames * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS; i++) {
        uint16_t sample = (uint16_t)samples[i];
        dest[i * 2u] = (uint8_t)sample;
        dest[i * 2u + 1u] = (uint8_t)(sample >> 8);
    }
    remote->websocket_out_size = 4u + payload_size;
    remote->websocket_out_sent = 0;
    return true;
}

static void websocket_flush_output(IntegralGBRuntimeLanRemote *remote)
{
    while (remote->websocket_out_sent < remote->websocket_out_size) {
        const uint8_t *src = remote->websocket_out + remote->websocket_out_sent;
        size_t remaining = remote->websocket_out_size - remote->websocket_out_sent;
        ssize_t n = integral_gb_runtime_socket_write(remote->websocket, src, remaining);
        if (n > 0) {
            remote->websocket_out_sent += (size_t)n;
            continue;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (integral_gb_runtime_socket_error_would_block(error_code) ||
            integral_gb_runtime_socket_error_interrupted(error_code)) {
            return;
        }
        close_websocket(remote);
        return;
    }
    remote->websocket_out_size = 0;
    remote->websocket_out_sent = 0;
}

static void websocket_parse_input(IntegralGBRuntimeLanRemote *remote, const uint8_t *data, size_t size)
{
    if (size < 2u) {
        return;
    }
    uint8_t opcode = (uint8_t)(data[0] & 0x0Fu);
    bool masked = (data[1] & 0x80u) != 0;
    uint64_t payload_len = (uint64_t)(data[1] & 0x7Fu);
    size_t offset = 2u;
    if (payload_len == 126u) {
        if (size < 4u) {
            return;
        }
        payload_len = ((uint64_t)data[2] << 8) | (uint64_t)data[3];
        offset = 4u;
    }
    else if (payload_len == 127u) {
        return;
    }
    if (!masked || payload_len == 0 || payload_len > 16u || size < offset + 4u + (size_t)payload_len) {
        return;
    }
    const uint8_t *mask = data + offset;
    const uint8_t *payload = data + offset + 4u;
    if (opcode == 0x8u) {
        close_websocket(remote);
        return;
    }
    if (opcode == 0x2u || opcode == 0x1u) {
        remote->buttons = (uint8_t)(payload[0] ^ mask[0]);
        if (payload_len >= 2u) {
            remote->fast_enabled = ((payload[1] ^ mask[1]) != 0);
        }
        if (payload_len >= 3u) {
            remote->paused = ((payload[2] ^ mask[2]) != 0);
        }
    }
}

static void websocket_poll(IntegralGBRuntimeLanRemote *remote)
{
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(remote->websocket)) {
        return;
    }
    for (;;) {
        uint8_t buffer[128];
        ssize_t n = integral_gb_runtime_socket_read(remote->websocket, buffer, sizeof(buffer));
        if (n > 0) {
            websocket_parse_input(remote, buffer, (size_t)n);
            if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(remote->websocket)) {
                return;
            }
            continue;
        }
        if (n == 0) {
            close_websocket(remote);
            return;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (integral_gb_runtime_socket_error_would_block(error_code) ||
            integral_gb_runtime_socket_error_interrupted(error_code)) {
            break;
        }
        close_websocket(remote);
        return;
    }

    if (remote->websocket_out_size == 0 &&
        remote->frame_ready &&
        remote->websocket_sent_frame_seq != remote->frame_seq) {
        websocket_prepare_frame(remote);
    }
    websocket_flush_output(remote);
}

void integral_gb_runtime_lan_remote_queue_audio(IntegralGBRuntimeLanRemote *remote, const int16_t *samples, unsigned frames)
{
    if (!remote || !remote->open) {
        return;
    }
    (void)websocket_prepare_audio(remote, samples, frames);
    websocket_flush_output(remote);
}

void integral_gb_runtime_lan_remote_poll(IntegralGBRuntimeLanRemote *remote, const IntegralGBRuntimeSlot *slot)
{
    if (!remote || !remote->open) {
        return;
    }
    websocket_poll(remote);
    for (unsigned i = 0; i < 4; i++) {
        integral_gb_runtime_socket_t fd = integral_gb_runtime_tcp_accept(remote->listener);
        if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
            return;
        }
        (void)integral_gb_runtime_socket_set_nonblocking(fd);
        if (!handle_request(remote, fd, slot)) {
            (void)integral_gb_runtime_socket_close(fd);
        }
    }
}

void integral_gb_runtime_lan_remote_apply_input(const IntegralGBRuntimeLanRemote *remote, IntegralGBRuntimeSlot *slot)
{
    uint8_t buttons = remote && remote->open ? remote->buttons : 0u;
    set_key(slot, GB_KEY_RIGHT, buttons, INTEGRAL_GB_RUNTIME_BTN_RIGHT);
    set_key(slot, GB_KEY_LEFT, buttons, INTEGRAL_GB_RUNTIME_BTN_LEFT);
    set_key(slot, GB_KEY_UP, buttons, INTEGRAL_GB_RUNTIME_BTN_UP);
    set_key(slot, GB_KEY_DOWN, buttons, INTEGRAL_GB_RUNTIME_BTN_DOWN);
    set_key(slot, GB_KEY_A, buttons, INTEGRAL_GB_RUNTIME_BTN_A);
    set_key(slot, GB_KEY_B, buttons, INTEGRAL_GB_RUNTIME_BTN_B);
    set_key(slot, GB_KEY_SELECT, buttons, INTEGRAL_GB_RUNTIME_BTN_SELECT);
    set_key(slot, GB_KEY_START, buttons, INTEGRAL_GB_RUNTIME_BTN_START);
}

bool integral_gb_runtime_lan_remote_fast_enabled(const IntegralGBRuntimeLanRemote *remote)
{
    return remote && remote->open && remote->fast_enabled;
}

bool integral_gb_runtime_lan_remote_paused(const IntegralGBRuntimeLanRemote *remote)
{
    return remote && remote->open && remote->paused;
}

const char *integral_gb_runtime_lan_remote_host(const IntegralGBRuntimeLanRemote *remote)
{
    return remote && remote->host[0] != '\0' ? remote->host : "127.0.0.1";
}

unsigned integral_gb_runtime_lan_remote_port(const IntegralGBRuntimeLanRemote *remote)
{
    return remote ? remote->port : INTEGRAL_GB_RUNTIME_LAN_REMOTE_DEFAULT_PORT;
}

void integral_gb_runtime_lan_remote_close(IntegralGBRuntimeLanRemote *remote)
{
    if (!remote || !remote->open) {
        return;
    }
    remote->buttons = 0;
    remote->fast_enabled = false;
    remote->paused = false;
    close_websocket(remote);
    if (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(remote->listener)) {
        (void)integral_gb_runtime_socket_close(remote->listener);
    }
    remote->listener = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    remote->open = false;
    integral_gb_runtime_net_shutdown();
}
