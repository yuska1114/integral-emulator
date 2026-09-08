/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_route_engine.h"
#include "../server/content_hash.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strncasecmp _strnicmp
#endif

#define ROUTE_MAX 256u
#define DNS_MAX 64u
#define STATE_MAX 64u
#define HEADER_MAX 32u
#define REQUEST_HEADER_PREDICATE_MAX 16u
#define REQUEST_BODY_SLICE_MAX 8u
#define PARSED_HEADER_MAX 64u
#define REQUEST_MAX (1024u * 1024u)

typedef enum JsonKind { J_UNDEFINED, J_OBJECT, J_ARRAY, J_STRING, J_PRIMITIVE } JsonKind;
typedef struct JsonToken { JsonKind kind; int start; int end; int size; int parent; } JsonToken;
typedef struct JsonParser { unsigned pos; unsigned next; int parent; } JsonParser;

typedef struct RouteHeader { char name[65]; char value[1025]; } RouteHeader;
typedef enum HeaderMatch { HEADER_PRESENT, HEADER_ABSENT, HEADER_EXACT, HEADER_PREFIX, HEADER_SHA256 } HeaderMatch;
typedef struct RequestHeaderPredicate {
    char name[65]; HeaderMatch match; size_t max_length; char value[513]; char sha256[65];
} RequestHeaderPredicate;
typedef struct RequestBodySlice { size_t offset, size; unsigned char data[64]; } RequestBodySlice;
typedef struct Route {
    char host[254]; char method[9]; char path[1025];
    char http_version[4];
    unsigned status; RouteHeader headers[HEADER_MAX]; unsigned header_count;
    uint64_t requires_true, requires_false, sets_true, sets_false;
    unsigned char *body; size_t body_size;
    size_t discard_limit, sink_limit;
    RequestHeaderPredicate request_headers[REQUEST_HEADER_PREDICATE_MAX];
    unsigned request_header_count;
    bool has_request_body, same_body_replay, replay_digest_set;
    size_t request_body_min, request_body_max;
    char request_body_sha256[65], replay_digest[65];
    RequestBodySlice request_body_slices[REQUEST_BODY_SLICE_MAX];
    unsigned request_body_slice_count, max_uses, uses;
} Route;
typedef struct RouteSocket {
    bool open, connected, ready, closed;
    enum mobile_socktype type;
    struct mobile_addr peer;
    unsigned char *request; size_t request_size;
    unsigned char *response; size_t response_size, response_cursor;
} RouteSocket;

struct IntegralGBRuntimeMobileRouteEngine {
    char dns[DNS_MAX][254]; unsigned dns_count;
    char state_names[STATE_MAX][129]; unsigned state_count; uint64_t state_bits;
    Route routes[ROUTE_MAX]; unsigned route_count;
    RouteSocket sockets[MOBILE_MAX_CONNECTIONS];
    unsigned errors;
    IntegralGBRuntimeMobileRouteStats stats;
};

static void fail(char *out, size_t size, const char *message) { if (out && size) snprintf(out, size, "%s", message); }
static JsonToken *alloc_token(JsonParser *p, JsonToken *tokens, size_t count) {
    if (p->next >= count) return NULL;
    JsonToken *t = &tokens[p->next++]; memset(t, 0, sizeof(*t)); t->start = t->end = -1; t->parent = -1; return t;
}
static int parse_json(const char *text, JsonToken *tokens, size_t count) {
    JsonParser p = {0, 0, -1};
    for (; text[p.pos]; ++p.pos) {
        char c = text[p.pos];
        if (isspace((unsigned char)c) || c == ':' || c == ',') continue;
        if (c == '{' || c == '[') {
            JsonToken *t = alloc_token(&p, tokens, count); if (!t) return -1;
            t->kind = c == '{' ? J_OBJECT : J_ARRAY; t->start = (int)p.pos; t->parent = p.parent;
            if (p.parent >= 0) tokens[p.parent].size++;
            p.parent = (int)p.next - 1;
        } else if (c == '}' || c == ']') {
            JsonKind expected = c == '}' ? J_OBJECT : J_ARRAY; int i = p.parent;
            if (i < 0 || tokens[i].kind != expected) return -1;
            tokens[i].end = (int)p.pos + 1; p.parent = tokens[i].parent;
        } else if (c == '"') {
            JsonToken *t = alloc_token(&p, tokens, count); if (!t) return -1;
            t->kind = J_STRING; t->start = (int)++p.pos; t->parent = p.parent;
            while (text[p.pos] && text[p.pos] != '"') {
                if ((unsigned char)text[p.pos] < 0x20u) return -1;
                if (text[p.pos] == '\\') {
                    char escaped = text[++p.pos];
                    if (!escaped || !strchr("\"\\/bfnrt", escaped)) return -1;
                }
                ++p.pos;
            }
            if (text[p.pos] != '"') return -1;
            t->end = (int)p.pos; if (p.parent >= 0) tokens[p.parent].size++;
        } else {
            JsonToken *t = alloc_token(&p, tokens, count); if (!t) return -1;
            t->kind = J_PRIMITIVE; t->start = (int)p.pos; t->parent = p.parent;
            while (text[p.pos] && !isspace((unsigned char)text[p.pos]) && !strchr(",]}:", text[p.pos])) ++p.pos;
            t->end = (int)p.pos; --p.pos; if (p.parent >= 0) tokens[p.parent].size++;
        }
    }
    return p.parent == -1 && p.next && tokens[0].kind == J_OBJECT ? (int)p.next : -1;
}
static int next_token(const JsonToken *tokens, int count, int index) {
    int end = tokens[index].end; for (++index; index < count && tokens[index].start < end; ++index) {} return index;
}
static int token_equal(const char *json, const JsonToken *t, const char *value) {
    size_t n = strlen(value); return t->kind == J_STRING && t->end - t->start == (int)n && !memcmp(json + t->start, value, n);
}
static int object_value(const char *json, const JsonToken *tokens, int count, int object, const char *key) {
    if (object < 0 || tokens[object].kind != J_OBJECT) return -1;
    int i = object + 1;
    while (i < count && tokens[i].start < tokens[object].end) {
        int value = i + 1; if (value >= count || tokens[i].kind != J_STRING) return -1;
        if (token_equal(json, &tokens[i], key)) return value;
        i = next_token(tokens, count, value);
    }
    return -1;
}
static int copy_string(const char *json, const JsonToken *t, char *out, size_t size) {
    int n = t->end - t->start; size_t used = 0;
    if (t->kind != J_STRING || n < 0) return -1;
    for (int i = 0; i < n; ++i) {
        unsigned char value = (unsigned char)json[t->start + i];
        if (value == '\\') {
            if (++i >= n) return -1;
            char escaped = json[t->start + i];
            if (escaped == '"' || escaped == '\\' || escaped == '/') value = (unsigned char)escaped;
            else if (escaped == 'b') value = '\b';
            else if (escaped == 'f') value = '\f';
            else if (escaped == 'n') value = '\n';
            else if (escaped == 'r') value = '\r';
            else if (escaped == 't') value = '\t';
            else return -1;
        }
        if (used + 1u >= size) return -1;
        out[used++] = (char)value;
    }
    out[used] = 0; return 0;
}
static int integer_value(const char *json, const JsonToken *t, size_t *out) {
    char value[32], *end = NULL; int n = t->end - t->start;
    if (t->kind != J_PRIMITIVE || n <= 0 || (size_t)n >= sizeof(value)) return -1;
    memcpy(value, json + t->start, (size_t)n); value[n] = 0; errno = 0;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || !end || *end || parsed > SIZE_MAX) return -1;
    *out = (size_t)parsed;
    return 0;
}
static int bool_value(const char *json, const JsonToken *t, bool *out) {
    if (t->kind != J_PRIMITIVE) return -1;
    if (t->end - t->start == 4 && !memcmp(json + t->start, "true", 4)) { *out = true; return 0; }
    if (t->end - t->start == 5 && !memcmp(json + t->start, "false", 5)) { *out = false; return 0; }
    return -1;
}
static char *read_text(const char *path, size_t max) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0 || (size_t)ftell(f) > max || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    long length = ftell(f); (void)length;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    length = ftell(f); if (length < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    char *data = malloc((size_t)length + 1u); if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)length, f) != (size_t)length || fclose(f)) { free(data); return NULL; }
    data[length] = 0; return data;
}
static const IntegralGBRuntimeMobileManifestArtifact *artifact_role(const IntegralGBRuntimeMobileSessionManifest *m, const char *role) {
    for (size_t i = 0; i < m->artifact_count; ++i) {
        if (!strcmp(m->artifacts[i].role, role)) return &m->artifacts[i];
    }
    return NULL;
}
static const IntegralGBRuntimeMobileManifestArtifact *artifact_id(const IntegralGBRuntimeMobileSessionManifest *m, const char *id) {
    for (size_t i = 0; i < m->artifact_count; ++i) {
        if (!strcmp(m->artifacts[i].content_id, id)) return &m->artifacts[i];
    }
    return NULL;
}
static int state_index(IntegralGBRuntimeMobileRouteEngine *e, const char *name) {
    for (unsigned i = 0; i < e->state_count; ++i) {
        if (!strcmp(e->state_names[i], name)) return (int)i;
    }
    return -1;
}
static int parse_state_object(IntegralGBRuntimeMobileRouteEngine *e, const char *json, const JsonToken *tokens, int count, int object, uint64_t *set_true, uint64_t *set_false, bool define) {
    if (object < 0) return 0;
    if (tokens[object].kind != J_OBJECT) return -1;
    int i = object + 1;
    while (i < count && tokens[i].start < tokens[object].end) {
        int value = i + 1; char name[129]; bool enabled;
        if (value >= count || copy_string(json, &tokens[i], name, sizeof(name)) || bool_value(json, &tokens[value], &enabled)) return -1;
        int index = state_index(e, name);
        if (define) {
            if (index >= 0 || e->state_count >= STATE_MAX) return -1;
            index = (int)e->state_count++; strcpy(e->state_names[index], name);
            if (enabled) e->state_bits |= UINT64_C(1) << index;
        } else if (index < 0) return -1;
        if (set_true && set_false) {
            if (enabled) *set_true |= UINT64_C(1) << index;
            else *set_false |= UINT64_C(1) << index;
        }
        i = next_token(tokens, count, value);
    }
    return 0;
}
static int b64(char c) { if (c >= 'A' && c <= 'Z') return c-'A'; if (c >= 'a' && c <= 'z') return c-'a'+26; if (c >= '0' && c <= '9') return c-'0'+52; if (c=='+') return 62; if (c=='/') return 63; if (c=='=') return -2; return -1; }
static int sha256_string(char value[65]) {
    if (strlen(value) != 64u) return -1;
    for (size_t i = 0; i < 64u; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!isxdigit(c)) return -1;
        value[i] = (char)tolower(c);
    }
    return 0;
}
static int decode_fixed(const char *json, const JsonToken *t, unsigned char **out, size_t *size_out) {
    int n=t->end-t->start; if (t->kind!=J_STRING || n<0 || n%4) return -1;
    size_t cap=(size_t)n/4u*3u; unsigned char *data=malloc(cap?cap:1u); if(!data)return -1; size_t used=0;
    for(int o=0;o<n;o+=4){int v[4];for(int i=0;i<4;i++)v[i]=b64(json[t->start+o+i]);if(v[0]<0||v[1]<0||(v[2]==-2&&v[3]!=-2)){free(data);return -1;} unsigned x=(unsigned)v[0]<<18|(unsigned)v[1]<<12|(v[2]>=0?(unsigned)v[2]<<6:0)|(v[3]>=0?(unsigned)v[3]:0);data[used++]=(unsigned char)(x>>16);if(v[2]>=0)data[used++]=(unsigned char)(x>>8);if(v[3]>=0)data[used++]=(unsigned char)x;}
    *out=data;*size_out=used;return 0;
}
static int parse_request_headers(Route *r, const char *json, const JsonToken *tokens, int count, int array) {
    if (array < 0) return 0;
    if (tokens[array].kind != J_ARRAY) return -1;
    int i = array + 1;
    while (i < count && tokens[i].start < tokens[array].end) {
        if (r->request_header_count >= REQUEST_HEADER_PREDICATE_MAX || tokens[i].kind != J_OBJECT) return -1;
        RequestHeaderPredicate *p = &r->request_headers[r->request_header_count];
        int name = object_value(json, tokens, count, i, "name");
        int presence = object_value(json, tokens, count, i, "presence");
        int maximum = object_value(json, tokens, count, i, "max_length");
        int exact = object_value(json, tokens, count, i, "value_exact");
        int prefix = object_value(json, tokens, count, i, "value_prefix");
        int digest = object_value(json, tokens, count, i, "value_sha256");
        size_t parsed_max = 2048u;
        if (name < 0 || presence < 0 || copy_string(json, &tokens[name], p->name, sizeof(p->name)) || !p->name[0] ||
            (maximum >= 0 && integer_value(json, &tokens[maximum], &parsed_max)) || parsed_max > 2048u) return -1;
        for (char *c = p->name; *c; ++c) {
            if (!isalnum((unsigned char)*c) && *c != '-') return -1;
            *c = (char)tolower((unsigned char)*c);
        }
        if (!strcmp(p->name, "host") || !strcmp(p->name, "content-length") || !strcmp(p->name, "connection") || !strcmp(p->name, "proxy-authorization")) return -1;
        for (unsigned prior = 0; prior < r->request_header_count; ++prior) if (!strcmp(p->name, r->request_headers[prior].name)) return -1;
        p->max_length = parsed_max;
        if (token_equal(json, &tokens[presence], "absent")) {
            if (maximum >= 0 || exact >= 0 || prefix >= 0 || digest >= 0) return -1;
            p->match = HEADER_ABSENT;
        } else if (token_equal(json, &tokens[presence], "present")) {
            unsigned matchers = (exact >= 0) + (prefix >= 0) + (digest >= 0);
            if (matchers > 1u) return -1;
            p->match = HEADER_PRESENT;
            if (exact >= 0) { if (copy_string(json, &tokens[exact], p->value, sizeof(p->value)) || strlen(p->value) > p->max_length) return -1; p->match = HEADER_EXACT; }
            if (prefix >= 0) { if (copy_string(json, &tokens[prefix], p->value, sizeof(p->value)) || strlen(p->value) > p->max_length) return -1; p->match = HEADER_PREFIX; }
            if (digest >= 0) { if (copy_string(json, &tokens[digest], p->sha256, sizeof(p->sha256)) || sha256_string(p->sha256)) return -1; p->match = HEADER_SHA256; }
        } else return -1;
        r->request_header_count++;
        i = next_token(tokens, count, i);
    }
    return 0;
}
static int parse_request_body(Route *r, const char *json, const JsonToken *tokens, int count, int object) {
    if (object < 0) return 0;
    if (tokens[object].kind != J_OBJECT) return -1;
    int minimum = object_value(json, tokens, count, object, "min_length");
    int maximum = object_value(json, tokens, count, object, "max_length");
    int digest = object_value(json, tokens, count, object, "sha256");
    int slices = object_value(json, tokens, count, object, "slices");
    if (minimum < 0 || maximum < 0 || slices < 0 ||
        integer_value(json, &tokens[minimum], &r->request_body_min) ||
        integer_value(json, &tokens[maximum], &r->request_body_max) ||
        r->request_body_min > r->request_body_max || r->request_body_max > REQUEST_MAX ||
        tokens[slices].kind != J_ARRAY) return -1;
    if (digest >= 0 && (copy_string(json, &tokens[digest], r->request_body_sha256, sizeof(r->request_body_sha256)) || sha256_string(r->request_body_sha256))) return -1;
    int i = slices + 1;
    while (i < count && tokens[i].start < tokens[slices].end) {
        if (r->request_body_slice_count >= REQUEST_BODY_SLICE_MAX || tokens[i].kind != J_OBJECT) return -1;
        RequestBodySlice *slice = &r->request_body_slices[r->request_body_slice_count];
        int offset = object_value(json, tokens, count, i, "offset");
        int fixed = object_value(json, tokens, count, i, "fixed_base64");
        unsigned char *decoded = NULL; size_t decoded_size = 0;
        if (offset < 0 || fixed < 0 || integer_value(json, &tokens[offset], &slice->offset) ||
            decode_fixed(json, &tokens[fixed], &decoded, &decoded_size) || !decoded_size || decoded_size > sizeof(slice->data) ||
            slice->offset > r->request_body_max || decoded_size > r->request_body_max - slice->offset) { free(decoded); return -1; }
        slice->size = decoded_size; memcpy(slice->data, decoded, decoded_size); free(decoded);
        for (unsigned prior = 0; prior < r->request_body_slice_count; ++prior) {
            RequestBodySlice *other = &r->request_body_slices[prior];
            if (slice->offset < other->offset + other->size && other->offset < slice->offset + slice->size) return -1;
        }
        r->request_body_slice_count++;
        i = next_token(tokens, count, i);
    }
    r->has_request_body = true;
    return 0;
}
static const RequestHeaderPredicate *route_header(const Route *route, const char *name) {
    for (unsigned i = 0; i < route->request_header_count; ++i) {
        if (!strcmp(route->request_headers[i].name, name)) return &route->request_headers[i];
    }
    return NULL;
}
static bool starts_with(const char *value, const char *prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}
static bool header_predicates_disjoint(const RequestHeaderPredicate *a, const RequestHeaderPredicate *b) {
    if (!a || !b) return false;
    if ((a->match == HEADER_ABSENT) != (b->match == HEADER_ABSENT)) return true;
    if (a->match == HEADER_ABSENT) return false;
    if (a->match == HEADER_EXACT && strlen(a->value) > b->max_length) return true;
    if (b->match == HEADER_EXACT && strlen(b->value) > a->max_length) return true;
    if (a->match == HEADER_EXACT && b->match == HEADER_EXACT) return strcmp(a->value, b->value) != 0;
    if (a->match == HEADER_PREFIX && b->match == HEADER_PREFIX) return !starts_with(a->value, b->value) && !starts_with(b->value, a->value);
    if (a->match == HEADER_EXACT && b->match == HEADER_PREFIX) return !starts_with(a->value, b->value);
    if (a->match == HEADER_PREFIX && b->match == HEADER_EXACT) return !starts_with(b->value, a->value);
    if (a->match == HEADER_SHA256 && b->match == HEADER_SHA256) return strcmp(a->sha256, b->sha256) != 0;
    if ((a->match == HEADER_EXACT && b->match == HEADER_SHA256) ||
        (a->match == HEADER_SHA256 && b->match == HEADER_EXACT)) {
        const RequestHeaderPredicate *exact = a->match == HEADER_EXACT ? a : b;
        const RequestHeaderPredicate *digest = a->match == HEADER_SHA256 ? a : b;
        unsigned char raw[32]; char hex[65];
        integral_gb_runtime_content_sha256(exact->value, strlen(exact->value), raw);
        integral_gb_runtime_content_sha256_hex(raw, hex);
        return strcmp(hex, digest->sha256) != 0;
    }
    return false;
}
static bool body_predicates_disjoint(const Route *a, const Route *b) {
    if (!a->has_request_body || !b->has_request_body) return false;
    if (a->request_body_max < b->request_body_min || b->request_body_max < a->request_body_min) return true;
    if (a->request_body_sha256[0] && b->request_body_sha256[0] &&
        strcmp(a->request_body_sha256, b->request_body_sha256)) return true;
    for (unsigned ai = 0; ai < a->request_body_slice_count; ++ai) {
        const RequestBodySlice *left = &a->request_body_slices[ai];
        for (unsigned bi = 0; bi < b->request_body_slice_count; ++bi) {
            const RequestBodySlice *right = &b->request_body_slices[bi];
            size_t start = left->offset > right->offset ? left->offset : right->offset;
            size_t left_end = left->offset + left->size, right_end = right->offset + right->size;
            size_t stop = left_end < right_end ? left_end : right_end;
            if (start < stop && memcmp(left->data + start - left->offset,
                                       right->data + start - right->offset, stop - start)) return true;
        }
    }
    return false;
}
static bool routes_overlap(const Route *a, const Route *b) {
    if (strcmp(a->host, b->host) || strcmp(a->method, b->method) || strcmp(a->path, b->path) ||
        a->requires_true != b->requires_true || a->requires_false != b->requires_false) return false;
    for (unsigned i = 0; i < a->request_header_count; ++i) {
        const RequestHeaderPredicate *other = route_header(b, a->request_headers[i].name);
        if (header_predicates_disjoint(&a->request_headers[i], other)) return false;
    }
    return !body_predicates_disjoint(a, b);
}
static int validate_schema_two_routes(IntegralGBRuntimeMobileRouteEngine *engine) {
    uint64_t full_state = engine->state_count == 64u ? UINT64_MAX :
        (engine->state_count ? (UINT64_C(1) << engine->state_count) - 1u : 0u);
    for (unsigned i = 0; i < engine->route_count; ++i) {
        Route *route = &engine->routes[i];
        if (!route->max_uses || (route->requires_true | route->requires_false) != full_state) return -1;
        for (unsigned prior = 0; prior < i; ++prior) if (routes_overlap(route, &engine->routes[prior])) return -1;
    }
    uint64_t states[ROUTE_MAX + 1u]; bool reached[ROUTE_MAX] = {false};
    unsigned state_count = 1u; states[0] = engine->state_bits;
    bool changed = true;
    while (changed) {
        changed = false;
        for (unsigned i = 0; i < engine->route_count; ++i) {
            Route *route = &engine->routes[i];
            for (unsigned state_index_value = 0; state_index_value < state_count; ++state_index_value) {
                uint64_t state = states[state_index_value];
                if ((state & route->requires_true) != route->requires_true ||
                    ((~state) & route->requires_false) != route->requires_false) continue;
                reached[i] = true;
                uint64_t result = (state | route->sets_true) & ~route->sets_false;
                bool known = false;
                for (unsigned known_index = 0; known_index < state_count; ++known_index) if (states[known_index] == result) known = true;
                if (!known) { if (state_count >= ROUTE_MAX + 1u) return -1; states[state_count++] = result; changed = true; }
                break;
            }
        }
    }
    for (unsigned i = 0; i < engine->route_count; ++i) if (!reached[i]) return -1;
    return 0;
}
static int read_slice(const IntegralGBRuntimeMobileSessionManifest *m, const char *id, size_t offset, size_t length, unsigned char **out) {
    const IntegralGBRuntimeMobileManifestArtifact *a=artifact_id(m,id); if(!a||offset>a->size||length>a->size-offset)return -1;
    FILE *f=fopen(a->path,"rb"); if(!f)return -1; unsigned char *data=malloc(length?length:1u); if(!data){fclose(f);return -1;}
    int ok=fseek(f,(long)offset,SEEK_SET)==0&&fread(data,1,length,f)==length&&fclose(f)==0; if(!ok){free(data);return -1;}*out=data;return 0;
}
static int load_dns(IntegralGBRuntimeMobileRouteEngine *e, const IntegralGBRuntimeMobileSessionManifest *m) {
    const IntegralGBRuntimeMobileManifestArtifact *a=artifact_role(m,"dns_routes"); if(!a)return -1; char *json=read_text(a->path,1024u*1024u); if(!json)return -1;
    JsonToken *tokens=calloc(4096,sizeof(*tokens)); int count=tokens?parse_json(json,tokens,4096):-1; int routes=count>0?object_value(json,tokens,count,0,"routes"):-1;
    if(routes<0||tokens[routes].kind!=J_ARRAY){free(tokens);free(json);return -1;} int i=routes+1;
    while(i<count&&tokens[i].start<tokens[routes].end){int name=object_value(json,tokens,count,i,"name"),endpoint=object_value(json,tokens,count,i,"endpoint");if(e->dns_count>=DNS_MAX||name<0||endpoint<0||!token_equal(json,&tokens[endpoint],"mobile-gateway")||copy_string(json,&tokens[name],e->dns[e->dns_count],sizeof(e->dns[0]))){free(tokens);free(json);return -1;}for(char*p=e->dns[e->dns_count];*p;p++)*p=(char)tolower((unsigned char)*p);e->dns_count++;i=next_token(tokens,count,i);}
    free(tokens);free(json);return e->dns_count?0:-1;
}
static int load_http(IntegralGBRuntimeMobileRouteEngine *e, const IntegralGBRuntimeMobileSessionManifest *m) {
    const IntegralGBRuntimeMobileManifestArtifact *a=artifact_role(m,"http_routes"); if(!a)return -1; char *json=read_text(a->path,16u*1024u*1024u); if(!json)return -1;
    JsonToken *tokens=calloc(32768,sizeof(*tokens)); int count=tokens?parse_json(json,tokens,32768):-1; int schema=count>0?object_value(json,tokens,count,0,"schema_version"):-1, initial=count>0?object_value(json,tokens,count,0,"initial_state"):-1, routes=count>0?object_value(json,tokens,count,0,"routes"):-1;size_t schema_version=0;
    if(schema<0||integer_value(json,&tokens[schema],&schema_version)||schema_version+1u!=m->runtime_capability_version||schema_version<1u||schema_version>2u||initial<0||routes<0||parse_state_object(e,json,tokens,count,initial,NULL,NULL,true)||tokens[routes].kind!=J_ARRAY){free(tokens);free(json);return -1;} int i=routes+1;
    while(i<count&&tokens[i].start<tokens[routes].end){if(e->route_count>=ROUTE_MAX){free(tokens);free(json);return -1;}Route*r=&e->routes[e->route_count];int host=object_value(json,tokens,count,i,"host"),method=object_value(json,tokens,count,i,"method"),path=object_value(json,tokens,count,i,"path"),status=object_value(json,tokens,count,i,"status"),version=object_value(json,tokens,count,i,"http_version");size_t n;strcpy(r->http_version,"1.1");if(host<0||method<0||path<0||status<0||copy_string(json,&tokens[host],r->host,sizeof(r->host))||copy_string(json,&tokens[method],r->method,sizeof(r->method))||copy_string(json,&tokens[path],r->path,sizeof(r->path))||(version>=0&&copy_string(json,&tokens[version],r->http_version,sizeof(r->http_version)))||(strcmp(r->http_version,"1.0")&&strcmp(r->http_version,"1.1"))||integer_value(json,&tokens[status],&n)||n<100||n>599){free(tokens);free(json);return -1;}r->status=(unsigned)n;for(char*p=r->host;*p;p++)*p=(char)tolower((unsigned char)*p);for(char*p=r->method;*p;p++)*p=(char)toupper((unsigned char)*p);
        int req=object_value(json,tokens,count,i,"requires"),sets=object_value(json,tokens,count,i,"sets");if(parse_state_object(e,json,tokens,count,req,&r->requires_true,&r->requires_false,false)||parse_state_object(e,json,tokens,count,sets,&r->sets_true,&r->sets_false,false)){free(tokens);free(json);return -1;}
        int headers=object_value(json,tokens,count,i,"headers");if(headers>=0){int h=headers+1;while(h<count&&tokens[h].start<tokens[headers].end){int value=h+1;if(r->header_count>=HEADER_MAX||copy_string(json,&tokens[h],r->headers[r->header_count].name,sizeof(r->headers[0].name))||copy_string(json,&tokens[value],r->headers[r->header_count].value,sizeof(r->headers[0].value))){free(tokens);free(json);return -1;}r->header_count++;h=next_token(tokens,count,value);}}
        int body=object_value(json,tokens,count,i,"body");if(body>=0){int fixed=object_value(json,tokens,count,body,"fixed_base64");if(fixed>=0){if(decode_fixed(json,&tokens[fixed],&r->body,&r->body_size)){free(tokens);free(json);return -1;}}else{int cid=object_value(json,tokens,count,body,"content_id"),off=object_value(json,tokens,count,body,"offset"),len=object_value(json,tokens,count,body,"length");char id[129];size_t offset,length;if(cid<0||off<0||len<0||copy_string(json,&tokens[cid],id,sizeof(id))||integer_value(json,&tokens[off],&offset)||integer_value(json,&tokens[len],&length)||read_slice(m,id,offset,length,&r->body)){free(tokens);free(json);return -1;}r->body_size=length;}}
        int discard=object_value(json,tokens,count,i,"discard_body"),sink=object_value(json,tokens,count,i,"sink_body");if(discard>=0&&integer_value(json,&tokens[discard],&r->discard_limit)){free(tokens);free(json);return -1;}if(sink>=0&&integer_value(json,&tokens[sink],&r->sink_limit)){free(tokens);free(json);return -1;}
        int request_headers=object_value(json,tokens,count,i,"request_headers"),request_body=object_value(json,tokens,count,i,"request_body"),max_uses=object_value(json,tokens,count,i,"max_uses"),same_body=object_value(json,tokens,count,i,"same_body_replay");size_t use_limit=0;bool replay=false;
        if((schema_version==1u&&(request_headers>=0||request_body>=0||max_uses>=0||same_body>=0))||(schema_version==2u&&max_uses<0)||parse_request_headers(r,json,tokens,count,request_headers)||parse_request_body(r,json,tokens,count,request_body)||(max_uses>=0&&(integer_value(json,&tokens[max_uses],&use_limit)||use_limit<1u||use_limit>16u))||(same_body>=0&&bool_value(json,&tokens[same_body],&replay))||(replay&&!r->has_request_body)||r->request_body_max>((r->discard_limit>r->sink_limit)?r->discard_limit:r->sink_limit)){free(tokens);free(json);return -1;}r->max_uses=(unsigned)use_limit;r->same_body_replay=replay;
        e->route_count++;i=next_token(tokens,count,i);
    }
    free(tokens);free(json);return e->route_count && (schema_version != 2u || !validate_schema_two_routes(e)) ? 0 : -1;
}

IntegralGBRuntimeMobileRouteEngine *integral_gb_runtime_mobile_route_engine_new(const IntegralGBRuntimeMobileSessionManifest *m,char *error,size_t error_size){IntegralGBRuntimeMobileRouteEngine*e=calloc(1,sizeof(*e));if(!e||load_dns(e,m)||load_http(e,m)){integral_gb_runtime_mobile_route_engine_free(e);fail(error,error_size,"route package validation failed");return NULL;}return e;}
static void socket_reset(RouteSocket*s){free(s->request);free(s->response);memset(s,0,sizeof(*s));}
void integral_gb_runtime_mobile_route_engine_free(IntegralGBRuntimeMobileRouteEngine*e){if(!e)return;for(unsigned i=0;i<e->route_count;i++)free(e->routes[i].body);for(unsigned i=0;i<MOBILE_MAX_CONNECTIONS;i++)socket_reset(&e->sockets[i]);free(e);}
static bool loopback(const struct mobile_addr*addr,unsigned port){if(!addr||addr->type!=MOBILE_ADDRTYPE_IPV4)return false;const struct mobile_addr4*a=(const struct mobile_addr4*)addr;return a->port==port&&a->host[0]==127&&a->host[1]==0&&a->host[2]==0&&a->host[3]==1;}
static void copy_addr(struct mobile_addr*d,const struct mobile_addr*s){memset(d,0,sizeof(*d));if(!s)return;if(s->type==MOBILE_ADDRTYPE_IPV4)memcpy(d,s,sizeof(struct mobile_addr4));else if(s->type==MOBILE_ADDRTYPE_IPV6)memcpy(d,s,sizeof(struct mobile_addr6));}
bool integral_gb_runtime_mobile_route_sock_open(IntegralGBRuntimeMobileRouteEngine*e,unsigned c,enum mobile_socktype type,enum mobile_addrtype at,unsigned bind){(void)bind;if(!e||c>=MOBILE_MAX_CONNECTIONS||at!=MOBILE_ADDRTYPE_IPV4||e->sockets[c].open){if(e)e->errors++;return false;}RouteSocket*s=&e->sockets[c];memset(s,0,sizeof(*s));s->request=malloc(REQUEST_MAX+1u);if(!s->request){e->errors++;return false;}s->open=true;s->type=type;return true;}
void integral_gb_runtime_mobile_route_sock_close(IntegralGBRuntimeMobileRouteEngine*e,unsigned c){if(e&&c<MOBILE_MAX_CONNECTIONS)socket_reset(&e->sockets[c]);}
int integral_gb_runtime_mobile_route_sock_connect(IntegralGBRuntimeMobileRouteEngine*e,unsigned c,const struct mobile_addr*a){if(!e||c>=MOBILE_MAX_CONNECTIONS||!e->sockets[c].open||e->sockets[c].type!=MOBILE_SOCKTYPE_TCP||!loopback(a,80)){if(e)e->errors++;return -1;}e->sockets[c].connected=true;copy_addr(&e->sockets[c].peer,a);return 1;}
static int dns_response(IntegralGBRuntimeMobileRouteEngine*e,RouteSocket*s,const unsigned char*q,size_t n,const struct mobile_addr*a){if(!loopback(a,MOBILE_DNS_PORT)||n<17||n>512)return -1;size_t o=12,u=0;char host[254];while(o<n&&q[o]){unsigned label=q[o++];if(!label||label>63||o+label>n||u+label+1>=sizeof(host))return -1;if(u)host[u++]='.';memcpy(host+u,q+o,label);u+=label;o+=label;}host[u]=0;for(char*p=host;*p;p++)*p=(char)tolower((unsigned char)*p);bool allowed=false;for(unsigned i=0;i<e->dns_count;i++)if(!strcmp(host,e->dns[i]))allowed=true;if(!allowed)return -1;s->response=malloc(n+16u);if(!s->response)return -1;memcpy(s->response,q,n);s->response[2]=0x81;s->response[3]=0x80;s->response[6]=0;s->response[7]=1;static const unsigned char answer[]={0xC0,0x0C,0,1,0,1,0,0,0,0,0,4,127,0,0,1};memcpy(s->response+n,answer,sizeof(answer));s->response_size=n+sizeof(answer);s->ready=true;copy_addr(&s->peer,a);e->stats.dns_requests++;return (int)n;}
static unsigned char*header_end(unsigned char*d,size_t n){for(size_t i=0;i+3<n;i++)if(d[i]=='\r'&&d[i+1]=='\n'&&d[i+2]=='\r'&&d[i+3]=='\n')return d+i+4;return NULL;}
typedef struct ParsedHeader { char name[65]; const unsigned char *value; size_t value_size; } ParsedHeader;
static int parse_decimal(const unsigned char *data,size_t size,size_t *out){size_t value=0;if(!size)return-1;for(size_t i=0;i<size;i++){if(data[i]<'0'||data[i]>'9'||value>(SIZE_MAX-(size_t)(data[i]-'0'))/10u)return-1;value=value*10u+(size_t)(data[i]-'0');}*out=value;return 0;}
static int parse_http_headers(unsigned char *request,size_t header_size,ParsedHeader headers[PARSED_HEADER_MAX],unsigned *count){unsigned char*line_end=NULL;for(size_t i=0;i+1<header_size;i++)if(request[i]=='\r'&&request[i+1]=='\n'){line_end=request+i;break;}if(!line_end)return-1;unsigned char*cursor=line_end+2;*count=0;while(cursor+1<request+header_size){if(cursor[0]=='\r'&&cursor[1]=='\n')return 0;if(*count>=PARSED_HEADER_MAX||*cursor==' '||*cursor=='\t')return-1;unsigned char*line_stop=NULL;for(unsigned char*p=cursor;p+1<request+header_size;p++)if(p[0]=='\r'&&p[1]=='\n'){line_stop=p;break;}if(!line_stop)return-1;unsigned char*colon=memchr(cursor,':',(size_t)(line_stop-cursor));if(!colon||colon==cursor||(size_t)(colon-cursor)>64u)return-1;ParsedHeader*h=&headers[(*count)++];size_t name_size=(size_t)(colon-cursor);for(size_t i=0;i<name_size;i++){unsigned char c=cursor[i];if(!isalnum(c)&&c!='-')return-1;h->name[i]=(char)tolower(c);}h->name[name_size]=0;const unsigned char*value=colon+1;const unsigned char*value_end=line_stop;while(value<value_end&&(*value==' '||*value=='\t'))value++;while(value_end>value&&(value_end[-1]==' '||value_end[-1]=='\t'))value_end--;for(const unsigned char*p=value;p<value_end;p++)if(*p<0x20u||*p==0x7fu)return-1;h->value=value;h->value_size=(size_t)(value_end-value);for(unsigned prior=0;prior+1u<*count;prior++)if(!strcmp(h->name,headers[prior].name))return-1;cursor=line_stop+2;}return-1;}
static const ParsedHeader*find_header(const ParsedHeader*headers,unsigned count,const char*name){for(unsigned i=0;i<count;i++)if(!strcmp(headers[i].name,name))return&headers[i];return NULL;}
static void bytes_sha256(const unsigned char*data,size_t size,char out[65]){unsigned char digest[32];integral_gb_runtime_content_sha256(data,size,digest);integral_gb_runtime_content_sha256_hex(digest,out);}
static bool header_predicates_match(const Route*r,const ParsedHeader*headers,unsigned count){for(unsigned i=0;i<r->request_header_count;i++){const RequestHeaderPredicate*p=&r->request_headers[i];const ParsedHeader*h=find_header(headers,count,p->name);if(p->match==HEADER_ABSENT){if(h)return false;continue;}if(!h||h->value_size>p->max_length)return false;if(p->match==HEADER_EXACT&&(h->value_size!=strlen(p->value)||memcmp(h->value,p->value,h->value_size)))return false;if(p->match==HEADER_PREFIX&&(h->value_size<strlen(p->value)||memcmp(h->value,p->value,strlen(p->value))))return false;if(p->match==HEADER_SHA256){char digest[65];bytes_sha256(h->value,h->value_size,digest);if(strcmp(digest,p->sha256))return false;}}return true;}
static bool body_predicate_matches(const Route*r,const unsigned char*body,size_t size,char digest[65]){if(!r->has_request_body){digest[0]=0;return true;}if(size<r->request_body_min||size>r->request_body_max)return false;bytes_sha256(body,size,digest);if(r->request_body_sha256[0]&&strcmp(digest,r->request_body_sha256))return false;for(unsigned i=0;i<r->request_body_slice_count;i++){const RequestBodySlice*s=&r->request_body_slices[i];if(s->offset>size||s->size>size-s->offset||memcmp(body+s->offset,s->data,s->size))return false;}return true;}
static const char*status_reason(unsigned status){switch(status){case 200:return"OK";case 204:return"No Content";case 400:return"Bad Request";case 401:return"Unauthorized";case 403:return"Forbidden";case 404:return"Not Found";case 409:return"Conflict";case 413:return"Payload Too Large";case 503:return"Service Unavailable";default:return"Response";}}
static int prepare_http(IntegralGBRuntimeMobileRouteEngine*e,RouteSocket*s){unsigned char*end=header_end(s->request,s->request_size);if(!end)return 0;size_t hs=(size_t)(end-s->request),content_length=0;char method[9],path[1025],version[16],extra[2],host[254]={0};s->request[s->request_size]=0;unsigned char*request_line_end=NULL;for(unsigned char*p=s->request;p+1<end;p++)if(p[0]=='\r'&&p[1]=='\n'){request_line_end=p;break;}if(!request_line_end)return-1;unsigned char saved=*request_line_end;*request_line_end=0;int request_fields=sscanf((char*)s->request,"%8s %1024s %15s %1s",method,path,version,extra);*request_line_end=saved;if(request_fields!=3||path[0]!='/'||(strcmp(version,"HTTP/1.0")&&strcmp(version,"HTTP/1.1")))return -1;ParsedHeader parsed[PARSED_HEADER_MAX];unsigned parsed_count=0;if(parse_http_headers(s->request,hs,parsed,&parsed_count))return-1;const ParsedHeader*host_header=find_header(parsed,parsed_count,"host");if(host_header){size_t n=0;while(n<host_header->value_size&&n+1u<sizeof(host)&&host_header->value[n]!=':'){host[n]=(char)tolower(host_header->value[n]);n++;}if(!n||n+1u>=sizeof(host)||(n<host_header->value_size&&host_header->value[n]!=':'))return-1;host[n]=0;}else if(e->dns_count==1u)strcpy(host,e->dns[0]);else return-1;const ParsedHeader*length_header=find_header(parsed,parsed_count,"content-length");if(length_header&&parse_decimal(length_header->value,length_header->value_size,&content_length))return-1;
    if(content_length>REQUEST_MAX-hs)return -1;
    if(s->request_size<hs+content_length)return 0;
    if(s->request_size!=hs+content_length)return-1;
    e->stats.http_requests++;
    if(content_length){e->stats.body_requests++;e->stats.body_bytes+=(uint64_t)content_length;}
    for(char*p=method;*p;p++)*p=(char)toupper((unsigned char)*p);
    Route*r=NULL;char selected_digest[65]={0};unsigned matches=0;bool selected_replay=false;
    for(unsigned i=0;i<e->route_count;i++){
        Route*x=&e->routes[i];char body_digest[65]={0};
        if(strcmp(x->host,host)||strcmp(x->method,method)||strcmp(x->path,path))continue;
        if((e->state_bits&x->requires_true)!=x->requires_true||((~e->state_bits)&x->requires_false)!=x->requires_false)continue;
        if(x->max_uses&&x->uses>=x->max_uses)continue;
        if(!header_predicates_match(x,parsed,parsed_count)||!body_predicate_matches(x,end,content_length,body_digest))continue;
        if(x->same_body_replay&&x->replay_digest_set&&strcmp(x->replay_digest,body_digest))continue;
        r=x;strcpy(selected_digest,body_digest);selected_replay=x->same_body_replay&&x->replay_digest_set;matches++;
    }
    if(!r||matches!=1u){
        /* Request bodies and authentication headers remain suppressed.  The
           route identity and state are sufficient to diagnose a fail-closed
           package mismatch such as selecting one scenario for another. */
        fprintf(stderr,
                "[mobile-route] unmatched method=%s host=%s path=%s state=0x%016llx matches=%u\n",
                method, host, path, (unsigned long long)e->state_bits, matches);
        return -1;
    }
    if(content_length>r->discard_limit&&content_length>r->sink_limit&&content_length>0)return -1;
    size_t cap=2048u+r->header_count*1100u+r->body_size;
    s->response=malloc(cap);
    if(!s->response)return -1;
    int n=snprintf((char*)s->response,cap,"HTTP/%s %u %s\r\nContent-Length: %zu\r\nConnection: close\r\n",r->http_version,r->status,status_reason(r->status),r->body_size);
    if(n<0||(size_t)n>=cap){free(s->response);s->response=NULL;return -1;}
    size_t used=(size_t)n;
    for(unsigned i=0;i<r->header_count;i++){
        n=snprintf((char*)s->response+used,cap-used,"%s: %s\r\n",r->headers[i].name,r->headers[i].value);
        if(n<0||(size_t)n>=cap-used)return -1;
        used+=(size_t)n;
    }
    if(used+2u+r->body_size>cap)return -1;
    memcpy(s->response+used,"\r\n",2);used+=2;
    memcpy(s->response+used,r->body,r->body_size);used+=r->body_size;
    s->response_size=used;s->ready=true;
    e->state_bits|=r->sets_true;e->state_bits&=~r->sets_false;r->uses++;
    e->stats.matched_responses++;
    if(r->status==401u)e->stats.unauthorized_responses++;
    if(selected_replay)e->stats.replay_responses++;
    if(r->same_body_replay&&!r->replay_digest_set){strcpy(r->replay_digest,selected_digest);r->replay_digest_set=true;}
    return 1;
}
int integral_gb_runtime_mobile_route_sock_send(IntegralGBRuntimeMobileRouteEngine*e,unsigned c,const void*data,unsigned n,const struct mobile_addr*a){if(!e||c>=MOBILE_MAX_CONNECTIONS||!e->sockets[c].open||!data){if(e)e->errors++;return -1;}RouteSocket*s=&e->sockets[c];if(s->type==MOBILE_SOCKTYPE_UDP){int rc=dns_response(e,s,data,n,a);if(rc<0)e->errors++;return rc;}if(!s->connected||n>REQUEST_MAX-s->request_size){e->errors++;return -1;}memcpy(s->request+s->request_size,data,n);s->request_size+=n;int rc=prepare_http(e,s);if(rc<0){e->errors++;return -1;}return (int)n;}
int integral_gb_runtime_mobile_route_sock_recv(IntegralGBRuntimeMobileRouteEngine*e,unsigned c,void*data,unsigned n,struct mobile_addr*a){if(!e||c>=MOBILE_MAX_CONNECTIONS||!e->sockets[c].open){if(e)e->errors++;return -1;}RouteSocket*s=&e->sockets[c];if(!data)return s->closed?-2:0;if(!s->ready)return 0;if(s->response_cursor>=s->response_size){s->closed=true;return -2;}size_t left=s->response_size-s->response_cursor,count=n<left?n:left;memcpy(data,s->response+s->response_cursor,count);s->response_cursor+=count;if(a&&s->type==MOBILE_SOCKTYPE_UDP)copy_addr(a,&s->peer);return (int)count;}
unsigned integral_gb_runtime_mobile_route_error_count(const IntegralGBRuntimeMobileRouteEngine*e){return e?e->errors:1u;}
void integral_gb_runtime_mobile_route_stats(const IntegralGBRuntimeMobileRouteEngine*e,IntegralGBRuntimeMobileRouteStats*stats){if(!stats)return;if(e)*stats=e->stats;else memset(stats,0,sizeof(*stats));}
