#pragma once

//: Сетевой рантайм для Linux: сокеты и строковые помощники.
//: Компилируется вместе с программой тем же cc, что и линковка.
inline const char* NET_RUNTIME_C = R"C(
#ifdef BERESTA_TLS
#include <openssl/ssl.h>
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

extern void* b_gc_alloc(size_t size);
extern long long b_argc;
extern char** b_argv;

long b_listen(long port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        close(fd);
        return -1;
    }
    return (long)fd;
}

long b_accept(long listener) {
    int fd = accept((int)listener, 0, 0);
    return (long)fd;
}

char* b_read_socket(long connection, long limit) {
    if (limit <= 0) limit = 4096;
    char* buffer = (char*)b_gc_alloc((size_t)limit + 1);
    ssize_t got = read((int)connection, buffer, (size_t)limit);
    if (got < 0) got = 0;
    buffer[got] = 0;
    return buffer;
}

long b_write_socket(long connection, const char* text) {
    size_t total = strlen(text);
    size_t sent = 0;
    while (sent < total) {
        ssize_t part = write((int)connection, text + sent, total - sent);
        if (part <= 0) return -1;
        sent += (size_t)part;
    }
    return (long)sent;
}

long b_close_socket(long connection) {
    return (long)close((int)connection);
}

long b_contains(const char* text, const char* piece) {
    return strstr(text, piece) != 0;
}

long b_starts_with(const char* text, const char* prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* --- окружение, время, файлы ---------------------------------------- */

long long b_arg_count(void) { return b_argc; }

char* b_arg(long long index) {
    if (index < 0 || index >= b_argc || b_argv == 0) return (char*)"";
    return b_argv[index];
}

char* b_getenv(const char* name) {
    const char* value = getenv(name);
    return (char*)(value != 0 ? value : "");
}

long long b_time(void) { return (long long)time(0); }

void b_sleep(long long milliseconds) {
    if (milliseconds <= 0) return;
    struct timespec spec;
    spec.tv_sec = (time_t)(milliseconds / 1000);
    spec.tv_nsec = (long)((milliseconds % 1000) * 1000000);
    nanosleep(&spec, 0);
}

long b_file_exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == 0) return 0;
    fclose(file);
    return 1;
}

long long b_file_size(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == 0) return -1;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return (long long)size;
}

long b_remove_file(const char* path) { return remove(path) == 0; }

/* --- строки ---------------------------------------------------------- */

char* b_substring(const char* text, long long start, long long length) {
    size_t size = strlen(text);
    if (start < 0) start = 0;
    if (length < 0) length = 0;

    size_t position = 0;
    size_t index = 0;
    while (position < size && index < (size_t)start) {
        position++;
        while (position < size && ((unsigned char)text[position] & 0xC0) == 0x80) position++;
        index++;
    }
    size_t byte_start = position;

    size_t taken = 0;
    while (position < size && taken < (size_t)length) {
        position++;
        while (position < size && ((unsigned char)text[position] & 0xC0) == 0x80) position++;
        taken++;
    }
    size_t byte_end = position;

    size_t count = byte_end - byte_start;
    char* result = (char*)b_gc_alloc(count + 1);
    memcpy(result, text + byte_start, count);
    result[count] = 0;
    return result;
}

long long b_find(const char* text, const char* piece) {
    const char* found = strstr(text, piece);
    if (found == 0) return -1;
    long long characters = 0;
    for (const char* cursor = text; cursor < found; ++cursor) {
        if (((unsigned char)*cursor & 0xC0) != 0x80) ++characters;
    }
    return characters;
}

static int b_hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

char* b_url_encode(const char* text) {
    size_t size = strlen(text);
    char* result = (char*)b_gc_alloc(size * 3 + 1);
    size_t out = 0;
    for (size_t i = 0; i < size; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            result[out++] = (char)ch;
        } else {
            static const char* hex = "0123456789ABCDEF";
            result[out++] = '%';
            result[out++] = hex[ch >> 4];
            result[out++] = hex[ch & 15];
        }
    }
    result[out] = 0;
    return result;
}

char* b_url_decode(const char* text) {
    size_t size = strlen(text);
    char* result = (char*)b_gc_alloc(size + 1);
    size_t out = 0;
    for (size_t i = 0; i < size; ++i) {
        if (text[i] == '%' && i + 2 < size) {
            int high = b_hex(text[i + 1]);
            int low = b_hex(text[i + 2]);
            if (high >= 0 && low >= 0) {
                result[out++] = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        if (text[i] == '+') {
            result[out++] = ' ';
        } else {
            result[out++] = text[i];
        }
    }
    result[out] = 0;
    return result;
}

void b_exit(long long code) { exit((int)code); }

void b_abort(const char* message) {
    printf("береста: %s\n", message);
    fflush(stdout);
    exit(1);
}

long long b_run(const char* command) { return (long long)system(command); }

void b_flush(void) { fflush(0); }

/* --- словари -------------------------------------------------------- */

#define B_MAP_CAPACITY 512
#define B_MAP_BYTES 131072

typedef struct {
    int count;
    int used;
} BMapHeader;

typedef struct {
    int key_off;
    int key_len;
    int val_off;
    int val_len;
} BMapEntry;

#define B_MAP_ENTRIES_OFF ((int)sizeof(BMapHeader))
#define B_MAP_DATA_OFF ((int)(sizeof(BMapHeader) + sizeof(BMapEntry) * B_MAP_CAPACITY))
#define B_MAP_DATA_CAPACITY (B_MAP_BYTES - B_MAP_DATA_OFF)

static BMapHeader* b_map_header(void* map) { return (BMapHeader*)map; }
static BMapEntry* b_map_entries(void* map) {
    return (BMapEntry*)((char*)map + B_MAP_ENTRIES_OFF);
}
static char* b_map_data(void* map) { return (char*)map + B_MAP_DATA_OFF; }

void* b_map_new(void) {
    char* block = (char*)b_gc_alloc(B_MAP_BYTES);
    memset(block, 0, sizeof(BMapHeader));
    return block;
}

static int b_map_find(void* map, const char* key) {
    if (map == 0) return -1;
    BMapHeader* header = b_map_header(map);
    BMapEntry* entries = b_map_entries(map);
    char* data = b_map_data(map);
    int length = (int)strlen(key);
    for (int i = 0; i < header->count; ++i) {
        if (entries[i].key_len == length &&
            memcmp(data + entries[i].key_off, key, (size_t)length) == 0) {
            return i;
        }
    }
    return -1;
}

static int b_map_append(void* map, const char* text, int* offset) {
    BMapHeader* header = b_map_header(map);
    char* data = b_map_data(map);
    int length = (int)strlen(text);
    if (header->used + length + 1 > B_MAP_DATA_CAPACITY) return 0;
    *offset = header->used;
    memcpy(data + header->used, text, (size_t)length);
    data[header->used + length] = 0;
    header->used += length + 1;
    return 1;
}

long b_map_put(void* map, const char* key, const char* value) {
    if (map == 0) return 0;
    BMapHeader* header = b_map_header(map);
    BMapEntry* entries = b_map_entries(map);

    int index = b_map_find(map, key);
    if (index >= 0) {
        int value_offset = 0;
        int value_length = (int)strlen(value);
        if (!b_map_append(map, value, &value_offset)) return 0;
        entries[index].val_off = value_offset;
        entries[index].val_len = value_length;
        return 1;
    }

    if (header->count >= B_MAP_CAPACITY) return 0;

    int key_offset = 0;
    int value_offset = 0;
    if (!b_map_append(map, key, &key_offset)) return 0;
    if (!b_map_append(map, value, &value_offset)) return 0;

    entries[header->count].key_off = key_offset;
    entries[header->count].key_len = (int)strlen(key);
    entries[header->count].val_off = value_offset;
    entries[header->count].val_len = (int)strlen(value);
    header->count += 1;
    return 1;
}

char* b_map_get(void* map, const char* key) {
    int index = b_map_find(map, key);
    if (index < 0) return (char*)"";
    BMapEntry* entries = b_map_entries(map);
    return b_map_data(map) + entries[index].val_off;
}

long b_map_has(void* map, const char* key) { return b_map_find(map, key) >= 0; }

long b_map_remove(void* map, const char* key) {
    int index = b_map_find(map, key);
    if (index < 0 || map == 0) return 0;
    BMapHeader* header = b_map_header(map);
    BMapEntry* entries = b_map_entries(map);
    for (int i = index; i + 1 < header->count; ++i) entries[i] = entries[i + 1];
    header->count -= 1;
    return 1;
}

long long b_map_size(void* map) {
    if (map == 0) return 0;
    return b_map_header(map)->count;
}

char* b_map_key_at(void* map, long long index) {
    if (map == 0 || index < 0 || index >= b_map_size(map)) return (char*)"";
    BMapEntry* entries = b_map_entries(map);
    return b_map_data(map) + entries[index].key_off;
}

char* b_map_value_at(void* map, long long index) {
    if (map == 0 || index < 0 || index >= b_map_size(map)) return (char*)"";
    BMapEntry* entries = b_map_entries(map);
    return b_map_data(map) + entries[index].val_off;
}



#ifdef BERESTA_TLS
typedef struct {
    int fd;
    SSL_CTX* context;
} BTlsServer;

long long b_tls_server(long long port, const char* certificate, const char* key) {
    OPENSSL_init_ssl(0, 0);

    SSL_CTX* context = SSL_CTX_new(TLS_server_method());
    if (context == 0) return -1;

    if (SSL_CTX_use_certificate_file(context, certificate, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(context, key, SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(context);
        return -1;
    }

    long listener = b_listen(port);
    if (listener < 0) {
        SSL_CTX_free(context);
        return -1;
    }

    BTlsServer* server = (BTlsServer*)malloc(sizeof(BTlsServer));
    if (server == 0) {
        close((int)listener);
        SSL_CTX_free(context);
        return -1;
    }
    server->fd = (int)listener;
    server->context = context;
    return (long long)server;
}

long long b_tls_accept(long long server) {
    if (server <= 0) return -1;
    BTlsServer* info = (BTlsServer*)server;

    int client = accept(info->fd, 0, 0);
    if (client < 0) return -1;

    SSL* ssl = SSL_new(info->context);
    if (ssl == 0) {
        close(client);
        return -1;
    }

    SSL_set_fd(ssl, client);
    if (SSL_accept(ssl) <= 0) {
        SSL_free(ssl);
        close(client);
        return -1;
    }
    return (long long)ssl;
}

char* b_tls_read(long long connection, long long limit) {
    if (limit <= 0) limit = 4096;
    char* buffer = (char*)b_gc_alloc((size_t)limit + 1);
    int got = SSL_read((SSL*)connection, buffer, (int)limit);
    if (got < 0) got = 0;
    buffer[got] = 0;
    return buffer;
}

long long b_tls_write(long long connection, const char* text) {
    size_t total = strlen(text);
    size_t sent = 0;
    while (sent < total) {
        int part = SSL_write((SSL*)connection, text + sent, (int)(total - sent));
        if (part <= 0) return -1;
        sent += (size_t)part;
    }
    return (long long)sent;
}

long long b_tls_close(long long connection) {
    SSL* ssl = (SSL*)connection;
    if (ssl == 0) return 0;
    SSL_shutdown(ssl);
    SSL_free(ssl);
    return 0;
}

long long b_tls_close_server(long long server) {
    BTlsServer* info = (BTlsServer*)server;
    if (info == 0) return 0;
    close(info->fd);
    SSL_CTX_free(info->context);
    free(info);
    return 0;
}
#endif

/* --- байты, base64, sha1, регистр ----------------------------------- */

long long b_xor(long long a, long long b) { return a ^ b; }

long long b_byte_at(const char* text, long long index) {
    size_t size = strlen(text);
    if (index < 0 || (size_t)index >= size) return 0;
    return (long long)(unsigned char)text[index];
}

char* b_char_from(long long code) {
    char* result = (char*)b_gc_alloc(5);
    unsigned int value = (unsigned int)code;
    if (value < 0x80) {
        result[0] = (char)value;
        result[1] = 0;
    } else if (value < 0x800) {
        result[0] = (char)(0xC0 | (value >> 6));
        result[1] = (char)(0x80 | (value & 0x3F));
        result[2] = 0;
    } else if (value < 0x10000) {
        result[0] = (char)(0xE0 | (value >> 12));
        result[1] = (char)(0x80 | ((value >> 6) & 0x3F));
        result[2] = (char)(0x80 | (value & 0x3F));
        result[3] = 0;
    } else {
        result[0] = (char)(0xF0 | (value >> 18));
        result[1] = (char)(0x80 | ((value >> 12) & 0x3F));
        result[2] = (char)(0x80 | ((value >> 6) & 0x3F));
        result[3] = (char)(0x80 | (value & 0x3F));
        result[4] = 0;
    }
    return result;
}

char* b_byte_string(long long code) {
    char* result = (char*)b_gc_alloc(2);
    result[0] = (char)(code & 0xFF);
    result[1] = 0;
    return result;
}

char* b_lower(const char* text) {
    size_t size = strlen(text);
    char* result = (char*)b_gc_alloc(size + 1);
    size_t output = 0;

    for (size_t i = 0; i < size;) {
        unsigned char ch = (unsigned char)text[i];

        if (ch >= 'A' && ch <= 'Z') {
            result[output++] = (char)(ch + 32);
            i += 1;
        } else if (ch == 0xD0 && i + 1 < size &&
                   (unsigned char)text[i + 1] >= 0x90 &&
                   (unsigned char)text[i + 1] <= 0x9F) {
            /* А-П → а-п */
            result[output++] = (char)0xD0;
            result[output++] = (char)((unsigned char)text[i + 1] + 0x20);
            i += 2;
        } else if (ch == 0xD0 && i + 1 < size &&
                   (unsigned char)text[i + 1] >= 0xA0 &&
                   (unsigned char)text[i + 1] <= 0xAF) {
            /* Р-Я → р-я */
            result[output++] = (char)0xD1;
            result[output++] = (char)((unsigned char)text[i + 1] - 0x20);
            i += 2;
        } else if (ch == 0xD0 && i + 1 < size && (unsigned char)text[i + 1] == 0x81) {
            /* Ё → ё */
            result[output++] = (char)0xD1;
            result[output++] = (char)0x91;
            i += 2;
        } else {
            result[output++] = (char)ch;
            i += 1;
        }
    }

    result[output] = 0;
    return result;
}

static const char B64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* b_base64_raw(const unsigned char* data, size_t size) {
    char* result = (char*)b_gc_alloc(4 * ((size + 2) / 3) + 1);
    size_t input = 0;
    size_t output = 0;

    while (input < size) {
        int remain = (int)(size - input);
        unsigned a = data[input++];
        unsigned b = remain > 1 ? data[input++] : 0;
        unsigned c = remain > 2 ? data[input++] : 0;

        result[output++] = B64_TABLE[a >> 2];
        result[output++] = B64_TABLE[((a & 3) << 4) | (b >> 4)];
        result[output++] = remain > 1 ? B64_TABLE[((b & 15) << 2) | (c >> 6)] : '=';
        result[output++] = remain > 2 ? B64_TABLE[c & 63] : '=';
    }

    result[output] = 0;
    return result;
}

char* b_base64_encode(const char* text) {
    return b_base64_raw((const unsigned char*)text, strlen(text));
}

static int b64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

char* b_base64_decode(const char* text) {
    size_t size = strlen(text);
    char* result = (char*)b_gc_alloc(size + 1);
    size_t output = 0;
    int quartet[4];
    int count = 0;

    for (size_t i = 0; i < size; ++i) {
        char ch = text[i];
        if (ch == '=') break;
        int value = b64_value(ch);
        if (value < 0) continue;
        quartet[count++] = value;
        if (count == 4) {
            unsigned triple = ((unsigned)quartet[0] << 18) | ((unsigned)quartet[1] << 12) |
                              ((unsigned)quartet[2] << 6) | (unsigned)quartet[3];
            result[output++] = (char)((triple >> 16) & 0xFF);
            result[output++] = (char)((triple >> 8) & 0xFF);
            result[output++] = (char)(triple & 0xFF);
            count = 0;
        }
    }

    if (count >= 2) {
        unsigned triple = ((unsigned)quartet[0] << 18) | ((unsigned)quartet[1] << 12) |
                          (count > 2 ? ((unsigned)quartet[2] << 6) : 0);
        result[output++] = (char)((triple >> 16) & 0xFF);
        if (count > 2) result[output++] = (char)((triple >> 8) & 0xFF);
    }

    result[output] = 0;
    return result;
}

typedef struct {
    uint32_t state[5];
    uint64_t length;
    unsigned char buffer[64];
    size_t used;
} BSha1;

static uint32_t b_rol(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

static void b_sha1_block(BSha1* sha, const unsigned char* block) {
    uint32_t words[80];
    for (int i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 80; ++i) {
        words[i] = b_rol(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    }

    uint32_t a = sha->state[0];
    uint32_t b = sha->state[1];
    uint32_t c = sha->state[2];
    uint32_t d = sha->state[3];
    uint32_t e = sha->state[4];

    for (int i = 0; i < 80; ++i) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = b_rol(a, 5) + f + e + k + words[i];
        e = d;
        d = c;
        c = b_rol(b, 30);
        b = a;
        a = temp;
    }

    sha->state[0] += a;
    sha->state[1] += b;
    sha->state[2] += c;
    sha->state[3] += d;
    sha->state[4] += e;
}

static void b_sha1_init(BSha1* sha) {
    sha->state[0] = 0x67452301;
    sha->state[1] = 0xEFCDAB89;
    sha->state[2] = 0x98BADCFE;
    sha->state[3] = 0x10325476;
    sha->state[4] = 0xC3D2E1F0;
    sha->length = 0;
    sha->used = 0;
}

static void b_sha1_update(BSha1* sha, const unsigned char* data, size_t size) {
    sha->length += (uint64_t)size * 8;
    while (size > 0) {
        size_t room = 64 - sha->used;
        size_t take = size < room ? size : room;
        memcpy(sha->buffer + sha->used, data, take);
        sha->used += take;
        data += take;
        size -= take;
        if (sha->used == 64) {
            b_sha1_block(sha, sha->buffer);
            sha->used = 0;
        }
    }
}

static void b_sha1_final(BSha1* sha, unsigned char digest[20]) {
    unsigned char pad[72];
    size_t pad_size = 0;
    uint64_t bits = sha->length;

    pad[pad_size++] = 0x80;
    while ((sha->used + pad_size) % 64 != 56) pad[pad_size++] = 0;
    for (int i = 7; i >= 0; --i) pad[pad_size++] = (unsigned char)((bits >> (i * 8)) & 0xFF);
    b_sha1_update(sha, pad, pad_size);

    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = (unsigned char)(sha->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(sha->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(sha->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)(sha->state[i]);
    }
}

char* b_sha1_hex(const char* text) {
    BSha1 sha;
    b_sha1_init(&sha);
    b_sha1_update(&sha, (const unsigned char*)text, strlen(text));

    unsigned char digest[20];
    b_sha1_final(&sha, digest);

    static const char* hex = "0123456789abcdef";
    char* result = (char*)b_gc_alloc(41);
    for (int i = 0; i < 20; ++i) {
        result[i * 2] = hex[digest[i] >> 4];
        result[i * 2 + 1] = hex[digest[i] & 15];
    }
    result[40] = 0;
    return result;
}


char* b_sha1_base64(const char* text) {
    BSha1 sha;
    b_sha1_init(&sha);
    b_sha1_update(&sha, (const unsigned char*)text, strlen(text));

    unsigned char digest[20];
    b_sha1_final(&sha, digest);
    return b_base64_raw(digest, 20);
}


/* --- SHA-256, случайные коды, утверждения --------------------------- */

typedef struct {
    uint32_t state[8];
    uint64_t length;
    unsigned char buffer[64];
    size_t used;
} BSha256;

static uint32_t b_ror32(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void b_sha256_block(BSha256* sha, const unsigned char* block) {
    static const uint32_t constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    uint32_t words[64];
    for (int i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = b_ror32(words[i - 15], 7) ^ b_ror32(words[i - 15], 18) ^
                      (words[i - 15] >> 3);
        uint32_t s1 = b_ror32(words[i - 2], 17) ^ b_ror32(words[i - 2], 19) ^
                      (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = sha->state[0], b = sha->state[1], c = sha->state[2], d = sha->state[3];
    uint32_t e = sha->state[4], f = sha->state[5], g = sha->state[6], h = sha->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = b_ror32(e, 6) ^ b_ror32(e, 11) ^ b_ror32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + constants[i] + words[i];
        uint32_t s0 = b_ror32(a, 2) ^ b_ror32(a, 13) ^ b_ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f; sha->state[6] += g; sha->state[7] += h;
}

static void b_sha256_init(BSha256* sha) {
    sha->state[0] = 0x6a09e667; sha->state[1] = 0xbb67ae85;
    sha->state[2] = 0x3c6ef372; sha->state[3] = 0xa54ff53a;
    sha->state[4] = 0x510e527f; sha->state[5] = 0x9b05688c;
    sha->state[6] = 0x1f83d9ab; sha->state[7] = 0x5be0cd19;
    sha->length = 0; sha->used = 0;
}

static void b_sha256_update(BSha256* sha, const unsigned char* data, size_t size) {
    sha->length += (uint64_t)size * 8;
    while (size > 0) {
        size_t room = 64 - sha->used;
        size_t take = size < room ? size : room;
        memcpy(sha->buffer + sha->used, data, take);
        sha->used += take; data += take; size -= take;
        if (sha->used == 64) { b_sha256_block(sha, sha->buffer); sha->used = 0; }
    }
}

static void b_sha256_final(BSha256* sha, unsigned char digest[32]) {
    unsigned char pad[72];
    size_t pad_size = 0;
    uint64_t bits = sha->length;

    pad[pad_size++] = 0x80;
    while ((sha->used + pad_size) % 64 != 56) pad[pad_size++] = 0;
    for (int i = 7; i >= 0; --i) pad[pad_size++] = (unsigned char)((bits >> (i * 8)) & 0xFF);
    b_sha256_update(sha, pad, pad_size);

    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = (unsigned char)(sha->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(sha->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(sha->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)(sha->state[i]);
    }
}

char* b_sha256_hex(const char* text) {
    BSha256 sha;
    b_sha256_init(&sha);
    b_sha256_update(&sha, (const unsigned char*)text, strlen(text));

    unsigned char digest[32];
    b_sha256_final(&sha, digest);

    static const char* hex = "0123456789abcdef";
    char* result = (char*)b_gc_alloc(65);
    for (int i = 0; i < 32; ++i) {
        result[i * 2] = hex[digest[i] >> 4];
        result[i * 2 + 1] = hex[digest[i] & 15];
    }
    result[64] = 0;
    return result;
}

char* b_random_code(long long length) {
    if (length <= 0) length = 16;
    if (length > 256) length = 256;

    unsigned char bytes[256];
    int filled = 0;

#ifdef _WIN32
    for (int i = 0; i < (int)length; ++i) bytes[i] = (unsigned char)(rand() & 0xFF);
    filled = 1;
#else
    FILE* source = fopen("/dev/urandom", "rb");
    if (source != 0) {
        if (fread(bytes, 1, (size_t)length, source) == (size_t)length) filled = 1;
        fclose(source);
    }
    if (!filled) {
        for (int i = 0; i < (int)length; ++i) bytes[i] = (unsigned char)(rand() & 0xFF);
    }
#endif

    static const char* hex = "0123456789abcdef";
    char* result = (char*)b_gc_alloc((size_t)length * 2 + 1);
    for (int i = 0; i < (int)length; ++i) {
        result[i * 2] = hex[bytes[i] >> 4];
        result[i * 2 + 1] = hex[bytes[i] & 15];
    }
    result[length * 2] = 0;
    return result;
}

void b_assert(long long condition, const char* message) {
    if (!condition) b_abort(message);
}

/* --- HTTP-клиент ---------------------------------------------------- */

char* b_http_get(const char* url) {
    int secure = 0;
    const char* rest = url;

    if (strncmp(rest, "https://", 8) == 0) {
        secure = 1;
        rest += 8;
    } else if (strncmp(rest, "http://", 7) == 0) {
        rest += 7;
    } else {
        return (char*)"";
    }

#ifndef BERESTA_TLS
    if (secure) return (char*)"";
#endif

    char host[256] = {0};
    char port[16] = {0};
    char path[1024] = {0};

    size_t index = 0;
    while (rest[index] != 0 && rest[index] != ':' && rest[index] != '/' &&
           index < sizeof(host) - 1) {
        host[index] = rest[index];
        index += 1;
    }
    if (host[0] == 0) return (char*)"";

    const char* tail = rest + index;
    if (*tail == ':') {
        tail += 1;
        size_t position = 0;
        while (*tail != 0 && *tail != '/' && position < sizeof(port) - 1) {
            port[position++] = *tail++;
        }
        port[position] = 0;
    }
    if (port[0] == 0) strcpy(port, secure ? "443" : "80");

    if (*tail == '/') {
        strncpy(path, tail, sizeof(path) - 1);
    } else {
        strcpy(path, "/");
    }

    struct addrinfo hints;
    struct addrinfo* addresses = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) return (char*)"";

    int fd = -1;
    for (struct addrinfo* address = addresses; address != 0; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) return (char*)"";

#ifdef BERESTA_TLS
    SSL_CTX* context = 0;
    SSL* ssl = 0;
    if (secure) {
        OPENSSL_init_ssl(0, 0);
        context = SSL_CTX_new(TLS_client_method());
        if (context == 0) {
            close(fd);
            return (char*)"";
        }
        SSL_CTX_set_verify(context, SSL_VERIFY_NONE, 0);
        ssl = SSL_new(context);
        SSL_set_fd(ssl, fd);
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(context);
            close(fd);
            return (char*)"";
        }
    }
#endif

    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: beresta\r\n"
             "Connection: close\r\n\r\n",
             path, host);

#ifdef BERESTA_TLS
    if (secure) SSL_write(ssl, request, (int)strlen(request));
    else
#endif
    send(fd, request, strlen(request), 0);

    size_t capacity = 4096;
    size_t used = 0;
    char* buffer = (char*)malloc(capacity);
    if (buffer == 0) {
#ifdef BERESTA_TLS
        if (ssl) { SSL_free(ssl); SSL_CTX_free(context); }
#endif
        close(fd);
        return (char*)"";
    }

    for (;;) {
        if (used + 2049 > capacity) {
            capacity *= 2;
            char* grown = (char*)realloc(buffer, capacity);
            if (grown == 0) break;
            buffer = grown;
        }

        int got;
#ifdef BERESTA_TLS
        got = secure ? SSL_read(ssl, buffer + used, 2048)
                     : (int)recv(fd, buffer + used, 2048, 0);
#else
        got = (int)recv(fd, buffer + used, 2048, 0);
#endif
        if (got <= 0) break;
        used += (size_t)got;
    }
    buffer[used] = 0;

    char* result = (char*)b_gc_alloc(used + 1);
    memcpy(result, buffer, used + 1);
    free(buffer);

#ifdef BERESTA_TLS
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(context);
    }
#endif
    close(fd);
    return result;
}
)C";
