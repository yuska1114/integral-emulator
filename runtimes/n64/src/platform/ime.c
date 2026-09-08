/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "platform/ime.h"

#include <SDL.h>
#include <stdio.h>

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#endif

#ifdef __APPLE__
static bool is_ascii_keyboard_source(TISInputSourceRef source)
{
    CFStringRef category;
    CFStringRef type;
    CFBooleanRef ascii;
    CFBooleanRef enabled;
    CFBooleanRef selectable;
    if (!source) return false;
    category = (CFStringRef)TISGetInputSourceProperty(
        source, kTISPropertyInputSourceCategory);
    type = (CFStringRef)TISGetInputSourceProperty(
        source, kTISPropertyInputSourceType);
    ascii = (CFBooleanRef)TISGetInputSourceProperty(
        source, kTISPropertyInputSourceIsASCIICapable);
    enabled = (CFBooleanRef)TISGetInputSourceProperty(
        source, kTISPropertyInputSourceIsEnabled);
    selectable = (CFBooleanRef)TISGetInputSourceProperty(
        source, kTISPropertyInputSourceIsSelectCapable);
    return category && CFEqual(category, kTISCategoryKeyboardInputSource) &&
           type && (CFEqual(type, kTISTypeKeyboardLayout) ||
                    CFEqual(type, kTISTypeKeyboardInputMode)) &&
           ascii == kCFBooleanTrue && enabled == kCFBooleanTrue &&
           selectable == kCFBooleanTrue;
}

static bool select_ascii_input_source(void)
{
    TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
    CFArrayRef sources;
    CFIndex index;
    if (is_ascii_keyboard_source(source)) {
        CFRelease(source);
        return true;
    }
    if (source) CFRelease(source);
    source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    if (is_ascii_keyboard_source(source)) {
        OSStatus status = TISSelectInputSource(source);
        CFRelease(source);
        if (status == noErr) return true;
    }
    else if (source) {
        CFRelease(source);
    }
    sources = TISCreateInputSourceList(NULL, false);
    if (!sources) return false;
    for (index = 0; index < CFArrayGetCount(sources); ++index) {
        TISInputSourceRef candidate = (TISInputSourceRef)
            CFArrayGetValueAtIndex(sources, index);
        if (is_ascii_keyboard_source(candidate) &&
            TISSelectInputSource(candidate) == noErr) {
            CFRelease(sources);
            return true;
        }
    }
    CFRelease(sources);
    return false;
}
#endif

bool integral_n64_runtime_ime_force_direct_input(void)
{
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0u) return true;
    SDL_StopTextInput();
#ifdef __APPLE__
    {
        if (!select_ascii_input_source()) {
            fprintf(stderr, "N64 Runtime: no ASCII-capable input source is enabled\n");
            return false;
        }
        return true;
    }
#elif defined(_WIN32)
    /*
     * N64 controls are read from SDL physical scancodes, so gameplay does not
     * require an installed US keyboard layout.  Requiring 00000409 here made
     * the Runtime exit before Core execution on Japanese-only Windows hosts.
     */
    return true;
#else
    /* SDL keyboard events are physical and no text-input context is active. */
    return true;
#endif
}
