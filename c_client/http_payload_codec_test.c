/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_payload_codec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_tests(void)
{
    char out[32];
    json_escape("a\"b\\c\n", out, sizeof(out));
    assert(strcmp(out, "a\\\"b\\\\c") == 0);
    json_escape("abc", out, 3);
    assert(strcmp(out, "ab") == 0);
    json_escape("", out, 1);
    assert(out[0] == 0);
    assert(extract_json_string("{\"s\":\"abc\"}", "s", out, 3) == 0);
    assert(strcmp(out, "ab") == 0);
    assert(extract_json_string("{\"s\":\"\"}", "s", out, sizeof(out)) == -1);
    assert(extract_json_string_value("{\"s\":\"\"}", "s", out, sizeof(out), true) == 0);
    assert(out[0] == 0);
    strcpy(out, "unchanged");
    assert(extract_json_string("{}", "s", out, sizeof(out)) == -1);
    assert(strcmp(out, "unchanged") == 0);
    assert(extract_json_string("{\"s\":\"a\\\"b\\\\c\"}", "s", out, sizeof(out)) == 0);
    assert(strcmp(out, "a\"b\\c") == 0);
    /* Existing helpers remove escape slashes; they do not decode JSON Unicode. */
    assert(extract_json_string("{\"s\":\"\\u0041\"}", "s", out, sizeof(out)) == 0);
    assert(strcmp(out, "u0041") == 0);
    const char *token = " \t\"abcd\",next";
    const char *end = copy_json_string_token(token, out, 3);
    assert(end && strcmp(end, ",next") == 0 && strcmp(out, "ab") == 0);
    assert(copy_json_string_token("\"unfinished", out, sizeof(out)) == NULL);
    assert(copy_json_string_token("null", out, sizeof(out)) == NULL);
    token = "\n\t x";
    assert(*skip_json_spaces(token) == 'x');
    int value = 77;
    long long large = 0;
    assert(extract_json_bool("{\"b\": true}", "b", &value) == 0 && value == 1);
    assert(extract_json_bool("{\"b\":false}", "b", &value) == 0 && value == 0);
    value = 77;
    assert(extract_json_bool("{\"b\":null}", "b", &value) == -1 && value == 77);
    assert(extract_json_int("{\"n\": -2147483647}", "n", &value) == 0 && value == -2147483647);
    assert(extract_json_int("{\"n\":2147483647}", "n", &value) == 0 && value == 2147483647);
    assert(extract_json_int("{}", "n", &value) == -1);
    assert(extract_json_int("{\"n\":\"text\"}", "n", &value) == -1);
    assert(extract_json_int64("{\"n\":9223372036854775807}", "n", &large) == 0 && large == 9223372036854775807LL);
    assert(extract_json_int64("{\"n\":-9223372036854775807}", "n", &large) == 0 && large == -9223372036854775807LL);
    assert(extract_json_int64("{}", "n", &large) == -1);
    const char *object = "{\"a\":{\"b\":\"}\\\"{\"}},tail";
    assert(strcmp(json_matching_end(object, '{', '}'), "},tail") == 0);
    assert(json_matching_end("{", '{', '}') == NULL);
    assert(json_matching_end(NULL, '{', '}') == NULL);
    assert(json_matching_end("[]", '{', '}') == NULL);
    assert(*json_matching_end("[[1],2]", '[', ']') == ']');
    puts("HTTP JSON: strings, escapes, missing keys, integers, capacities, nested delimiters PASS");
}

int main(void)
{
    json_tests();
    const char *plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char *encoded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    char text[32];
    unsigned char bytes[32];
    size_t size;
    for (unsigned i = 0; i < 7; ++i) {
        base64_encode((const unsigned char *)plain[i], strlen(plain[i]), text, sizeof(text));
        assert(strcmp(text, encoded[i]) == 0);
        size = 999;
        assert(base64_decode(encoded[i], bytes, strlen(plain[i]), &size) == 0);
        assert(size == strlen(plain[i]) && memcmp(bytes, plain[i], size) == 0);
    }
    base64_encode((const unsigned char *)"foo", 3, text, 4);
    assert(text[0] == 0);
    base64_encode((const unsigned char *)"foo", 3, text, 5);
    assert(strcmp(text, "Zm9v") == 0);
    base64_encode((const unsigned char *)"foo", 3, text, 1);
    assert(text[0] == 0);
    assert(base64_decode(" Z m\n9\tv", bytes, 3, &size) == 0 && size == 3);
    const char *bad[] = {"Z", "Zm", "Zm9", "!!!!", "=m9v", "Z=9v", "Zm9v "};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        size = 999;
        assert(base64_decode(bad[i], bytes, sizeof(bytes), &size) == -1);
        assert(size == 999);
    }
    for (size_t cap = 0; cap < 3; ++cap) {
        memset(bytes, 0x55, sizeof(bytes));
        size = 999;
        assert(base64_decode("Zm9v", bytes, cap, &size) == -1);
        assert(size == 999 && bytes[cap] == 0x55);
    }
    const size_t n = 128u * 1024u;
    unsigned char *save = malloc(n), *decoded = malloc(n);
    char *large = malloc(4 * ((n + 2) / 3) + 1);
    assert(save && decoded && large);
    for (size_t i = 0; i < n; ++i) save[i] = (unsigned char)(i * 37);
    base64_encode(save, n, large, 4 * ((n + 2) / 3) + 1);
    assert(base64_decode(large, decoded, n, &size) == 0);
    assert(size == n && memcmp(save, decoded, n) == 0);
    free(large); free(decoded); free(save);
    puts("HTTP Base64: vectors, padding, whitespace, capacities, errors, 128KiB SAV PASS");
    return 0;
}
