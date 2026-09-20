/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <SDL.h>
#undef main
#define main fixed_host_product_main
#include "integral_gb_runtime_fixed_host.c"
#undef main
#ifdef _WIN32
#define main SDL_main
#endif
#include <assert.h>

static void set_ir_delay(const char *value)
{
    const char *name = "INTEGRAL_EMULATOR_GB_IR_OFF_DELAY_TICKS";
#ifdef _WIN32
    assert(_putenv_s(name, value ? value : "") == 0);
    if (value && !*value) value = NULL;
#else
    assert((value ? setenv(name, value, 1) : unsetenv(name)) == 0);
#endif
    const char *actual = getenv(name);
    assert(value ? actual && !strcmp(actual, value) : actual == NULL);
}

static void push(SDL_Keycode key)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    assert(SDL_PushEvent(&e) == 1);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
#ifdef _WIN32
    /* Assertions must be visible to the test runner, not a GUI dialog. */
    _set_error_mode(_OUT_TO_STDERR);
#endif
    char *args[] = {"fixed", "--role", "remote", "--relay-host", "localhost",
                    "--relay-transport", "plain", "--session", "option-test"};
    const char *values[] = {"0", "32", "256", "257", "-1", "bad"};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
        Options parsed;
        set_ir_delay(values[i]);
        int result = parse_options(9, args, &parsed);
        assert(i < 3 ? result == 0 : result != 0);
        if (i < 3) assert(parsed.ir_off_delay_ticks == (unsigned)atoi(values[i]));
    }
    Options parsed;
    set_ir_delay(NULL);
    assert(parse_options(9, args, &parsed) == 0 && parsed.ir_off_delay_ticks == 32);
    set_ir_delay("");
#ifdef _WIN32
    /* The Windows CRT treats an empty value as removal. */
    assert(parse_options(9, args, &parsed) == 0 && parsed.ir_off_delay_ticks == 32);
#else
    assert(parse_options(9, args, &parsed) != 0);
#endif
    set_ir_delay("32");
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    Options options = {.screenshot_key = SDLK_F8, .escape_key = SDLK_F9};
    integral_gb_runtime_key_config_slot1_default(&options.keys);
    for (unsigned role = 1; role <= 2; ++role) {
        IntegralGBRuntimeFixedHostProductRuntime product;
        assert(integral_gb_runtime_fixed_host_product_runtime_init(&product, role));
        bool capture = false;
        push(SDLK_ESCAPE); push(SDLK_F5); push(SDLK_LSHIFT);
        assert(poll_input(&product, NULL, &options, &capture) == 0);
        assert(!product.exit_confirming && !capture);
        push(SDLK_F8);
        assert(poll_input(&product, NULL, &options, &capture) == 0 && capture);
        push(SDLK_F9);
        assert(poll_input(&product, NULL, &options, &capture) == 0);
        assert(product.exit_confirming && !product.exit_confirm_yes);
        push(SDLK_RETURN);
        assert(poll_input(&product, NULL, &options, &capture) == 0 && !product.exit_confirming);
        SDL_Event close;
        SDL_zero(close);
        close.type = SDL_QUIT;
        assert(SDL_PushEvent(&close) == 1);
        assert(poll_input(&product, NULL, &options, &capture) == 0 && product.exit_confirming);
        push(SDLK_RIGHT); push(SDLK_RETURN);
        assert(poll_input(&product, NULL, &options, &capture) == (IntegralGBRuntimeFixedHostExitSelection)role);
    }
    HostVideo video = {0};
    IntegralGBRuntimeLinkEngine engine = {0};
    host_video_capture(&video, &engine);
    assert(!strcmp(video.notice, "SCREENSHOT SAVED"));
    SDL_Quit();
    puts("Fixed Host/Remote configured capture, escape, cancel and confirmed close PASS");
    return 0;
}
