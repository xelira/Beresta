#pragma once

//: Рантайм для Windows-цели. Компилируется тем же MinGW-компилятором, что и
//: линковка программы. ABI (Microsoft x64) обеспечивает C-компилятор, поэтому
//: здесь нет ни одной ассемблерной строки.
inline const char* WINDOWS_RUNTIME_C = R"C(
#include <winsock2.h>
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
)C";
