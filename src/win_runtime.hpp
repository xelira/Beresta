#pragma once

//: Рантайм для Windows-цели. Компилируется тем же MinGW-компилятором, что и
//: линковка программы. ABI (Microsoft x64) обеспечивает C-компилятор, поэтому
//: здесь нет ни одной ассемблерной строки.
inline const char* WINDOWS_RUNTIME_C = R"C(
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>

typedef long long b_int;

void* b_stack_top = 0;
long long b_argc = 0;
char** b_argv = 0;

static int b_remove_utf8(const char* path) {
    wchar_t wide[1024];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1024);
    return _wremove(wide);
}

static FILE* b_fopen_utf8(const char* path, const char* mode) {
    wchar_t wide_path[1024];
    wchar_t wide_mode[8];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, 1024);
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wide_mode, 8);
    return _wfopen(wide_path, wide_mode);
}

static void __attribute__((constructor)) b_init_console(void) {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}

/* --- GC ------------------------------------------------------------ */
typedef struct Block {
    struct Block* next;
    size_t size;
} Block;

static Block* b_gc_head = 0;
static size_t b_gc_bytes = 0;
static size_t b_gc_limit = 1048576;
static size_t b_gc_live = 0;

void b_panic(const char* message) {
    printf("береста: %s\n", message);
    fflush(stdout);
    exit(1);
}

void* b_gc_alloc(size_t size) {
    size_t aligned = (size + 7) & ~(size_t)7;
    Block* block = (Block*)malloc(sizeof(Block) + aligned);
    if (!block) b_panic("не хватило памяти");
    block->next = b_gc_head;
    block->size = aligned;
    b_gc_head = block;
    b_gc_bytes += aligned;

    if (b_gc_bytes > b_gc_limit) {
        block->size = aligned | 1;
        extern void b_gc_collect(void);
        b_gc_collect();
    }
    return (char*)block + sizeof(Block);
}

void b_gc_mark(const void* candidate) {
    if (!candidate) return;
    const char* pointer = (const char*)candidate;
    for (Block* block = b_gc_head; block; block = block->next) {
        const char* start = (const char*)block + sizeof(Block);
        size_t size = block->size & ~(size_t)1;
        if (pointer >= start && pointer < start + size) {
            block->size |= 1;
            return;
        }
    }
}

void b_gc_collect(void) {
    volatile char marker = 0;
    const char* bottom =
        (const char*)(((uintptr_t)&marker + 7) & ~(uintptr_t)7);
    const char* top = (const char*)((uintptr_t)b_stack_top & ~(uintptr_t)7);
    if (bottom < top) {
        for (const char* cursor = bottom; cursor + 8 <= top; cursor += 8) {
            const void* value = *(const void* const*)cursor;
            b_gc_mark(value);
        }
    }

    size_t live = 0;
    Block** link = &b_gc_head;
    while (*link) {
        Block* block = *link;
        size_t size = block->size & ~(size_t)1;
        if (block->size & 1) {
            block->size = size;
            live += size;
            link = &block->next;
        } else {
            *link = block->next;
            free(block);
        }
    }

    b_gc_bytes = 0;
    b_gc_live = live;
    b_gc_limit = live * 2 > 1048576 ? live * 2 : 1048576;
}

b_int b_gc_live_bytes(void) { return (b_int)b_gc_live; }

/* --- числа и строки ------------------------------------------------ */

b_int b_div(b_int a, b_int b) {
    if (b == 0) b_panic("деление на ноль");
    return a / b;
}

b_int b_mod(b_int a, b_int b) {
    if (b == 0) b_panic("остаток от деления на ноль");
    return a % b;
}

b_int b_str_len(const char* text) {
    b_int length = 0;
    const unsigned char* cursor = (const unsigned char*)text;
    while (*cursor) {
        if ((*cursor & 0xC0) != 0x80) ++length;
        ++cursor;
    }
    return length;
}

char* b_str_concat(const char* left, const char* right) {
    size_t a = strlen(left);
    size_t b = strlen(right);
    char* result = (char*)b_gc_alloc(a + b + 1);
    memcpy(result, left, a);
    memcpy(result + a, right, b + 1);
    return result;
}

char* b_str_from_int(b_int value) {
    char* buffer = (char*)b_gc_alloc(32);
    snprintf(buffer, 32, "%lld", value);
    return buffer;
}

char* b_str_from_float(double value) {
    char* buffer = (char*)b_gc_alloc(64);
    snprintf(buffer, 64, "%.15g", value);
    return buffer;
}

char* b_str_from_bool(b_int value) {
    return (char*)(value ? "правда" : "кривда");
}

b_int b_str_to_int(const char* text) {
    return (b_int)strtoll(text, 0, 10);
}

b_int b_str_bytes(const char* text) { return (b_int)strlen(text); }

b_int b_random(b_int bound) {
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        unsigned entropy = (unsigned)time(0) ^ (unsigned)clock() ^
                           (unsigned)(uintptr_t)&seeded;
        srand(entropy);
    }
    if (bound <= 0) return 0;
    return (b_int)(rand() % bound);
}

/* --- ввод-вывод ----------------------------------------------------- */

char* b_read_line(void) {
    size_t capacity = 128;
    size_t length = 0;
    char* buffer = (char*)b_gc_alloc(capacity);
    for (;;) {
        int ch = getchar();
        if (ch == EOF || ch == '\n') break;
        if (length + 1 >= capacity) {
            size_t next = capacity * 2;
            char* grown = (char*)b_gc_alloc(next);
            memcpy(grown, buffer, length);
            buffer = grown;
            capacity = next;
        }
        buffer[length++] = (char)ch;
    }
    buffer[length] = 0;
    return buffer;
}

char* b_read_file(const char* path) {
    FILE* file = b_fopen_utf8(path, "rb");
    if (!file) return (char*)"";
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size < 0) size = 0;
    fseek(file, 0, SEEK_SET);
    char* buffer = (char*)b_gc_alloc((size_t)size + 1);
    size_t read = fread(buffer, 1, (size_t)size, file);
    buffer[read] = 0;
    fclose(file);
    return buffer;
}

b_int b_write_file(const char* path, const char* text) {
    FILE* file = b_fopen_utf8(path, "wb");
    if (!file) return 0;
    fputs(text, file);
    fclose(file);
    return 1;
}

/* --- фразы ---------------------------------------------------------- */

typedef struct Phrase {
    const char* key;
    const char* value;
} Phrase;

static const Phrase b_phrases[] = {
    {"привет", "Здравствуй, путник."},
    {"прощание", "Ступай с миром."},
    {"спасибо", "Благодарствую."},
    {"пожалуйста", "На здоровье."},
    {"да", "Так."},
    {"нет", "Никак нет."},
    {"береста", "Береста шелестит."},
    {"тишина", "В лесу тихо."},
    {"ошибка", "Что-то пошло не так."},
    {"дорога", "Дорога зовёт."},
    {"утро", "Утро доброе."},
    {"вечер", "Вечер тихий."},
};

char* b_phrase(const char* key) {
    for (size_t i = 0; i < sizeof(b_phrases) / sizeof(b_phrases[0]); ++i) {
        if (strcmp(b_phrases[i].key, key) == 0) {
            return (char*)b_phrases[i].value;
        }
    }
    return (char*)"";
}

/* --- сеть ------------------------------------------------------------ */

static int b_net_ready = 0;

static void b_net_init(void) {
    if (!b_net_ready) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        b_net_ready = 1;
    }
}

long long b_listen(long long port) {
    b_net_init();
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) return -1;

    BOOL yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((u_short)port);

    if (bind(listener, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        closesocket(listener);
        return -1;
    }
    if (listen(listener, 128) == SOCKET_ERROR) {
        closesocket(listener);
        return -1;
    }
    return (long long)listener;
}

long long b_accept(long long listener) {
    SOCKET client = accept((SOCKET)listener, 0, 0);
    if (client == INVALID_SOCKET) return -1;
    return (long long)client;
}

char* b_read_socket(long long connection, long long limit) {
    if (limit <= 0) limit = 4096;
    char* buffer = (char*)b_gc_alloc((size_t)limit + 1);
    int got = recv((SOCKET)connection, buffer, (int)limit, 0);
    if (got < 0) got = 0;
    buffer[got] = 0;
    return buffer;
}

long long b_write_socket(long long connection, const char* text) {
    size_t total = strlen(text);
    size_t sent = 0;
    while (sent < total) {
        int part = send((SOCKET)connection, text + sent, (int)(total - sent), 0);
        if (part <= 0) return -1;
        sent += (size_t)part;
    }
    return (long long)sent;
}

long long b_close_socket(long long connection) {
    return (long long)closesocket((SOCKET)connection);
}

long long b_contains(const char* text, const char* piece) {
    return strstr(text, piece) != 0;
}

long long b_starts_with(const char* text, const char* prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* --- окружение, время, файлы, строки --------------------------------- */

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
    Sleep((DWORD)milliseconds);
}

long b_file_exists(const char* path) {
    FILE* file = b_fopen_utf8(path, "rb");
    if (file == 0) return 0;
    fclose(file);
    return 1;
}

long long b_file_size(const char* path) {
    FILE* file = b_fopen_utf8(path, "rb");
    if (file == 0) return -1;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return (long long)size;
}

long b_remove_file(const char* path) { return b_remove_utf8(path) == 0; }

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

static int b_hex_digit(char ch) {
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
            int high = b_hex_digit(text[i + 1]);
            int low = b_hex_digit(text[i + 2]);
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



/* --- TLS: на Windows пока заглушки ---------------------------------- */

long long b_tls_server(long long port, const char* certificate, const char* key) {
    (void)port;
    (void)certificate;
    (void)key;
    return -1;
}

long long b_tls_accept(long long server) {
    (void)server;
    return -1;
}

char* b_tls_read(long long connection, long long limit) {
    (void)connection;
    (void)limit;
    return (char*)"";
}

long long b_tls_write(long long connection, const char* text) {
    (void)connection;
    (void)text;
    return -1;
}

long long b_tls_close(long long connection) {
    (void)connection;
    return 0;
}

long long b_tls_close_server(long long server) {
    (void)server;
    return 0;
}

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

/* --- HTTP-клиент (без TLS) ------------------------------------------ */

char* b_http_get(const char* url) {
    const char* rest = url;
    if (strncmp(rest, "http://", 7) == 0) {
        rest += 7;
    } else {
        return (char*)"";
    }

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
    if (port[0] == 0) strcpy(port, "80");

    if (*tail == '/') {
        strncpy(path, tail, sizeof(path) - 1);
    } else {
        strcpy(path, "/");
    }

    b_net_init();

    struct addrinfo hints;
    struct addrinfo* addresses = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &addresses) != 0) return (char*)"";

    SOCKET fd = INVALID_SOCKET;
    for (struct addrinfo* address = addresses; address != 0; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        if (connect(fd, address->ai_addr, (int)address->ai_addrlen) == 0) break;
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(addresses);
    if (fd == INVALID_SOCKET) return (char*)"";

    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: beresta\r\n"
             "Connection: close\r\n\r\n",
             path, host);

    send(fd, request, (int)strlen(request), 0);

    size_t capacity = 4096;
    size_t used = 0;
    char* buffer = (char*)malloc(capacity);
    if (buffer == 0) {
        closesocket(fd);
        return (char*)"";
    }

    for (;;) {
        if (used + 2049 > capacity) {
            capacity *= 2;
            char* grown = (char*)realloc(buffer, capacity);
            if (grown == 0) break;
            buffer = grown;
        }
        int got = recv(fd, buffer + used, 2048, 0);
        if (got <= 0) break;
        used += (size_t)got;
    }
    buffer[used] = 0;

    char* result = (char*)b_gc_alloc(used + 1);
    memcpy(result, buffer, used + 1);
    free(buffer);
    closesocket(fd);
    return result;
}
)C";
