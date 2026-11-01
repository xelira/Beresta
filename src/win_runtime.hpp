#pragma once

//: Рантайм для Windows-цели. Компилируется тем же MinGW-компилятором, что и
//: линковка программы. ABI (Microsoft x64) обеспечивает C-компилятор, поэтому
//: здесь нет ни одной ассемблерной строки.
inline const char* WINDOWS_RUNTIME_C = R"C(
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <windows.h>

typedef long long b_int;

void* b_stack_top = 0;

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
)C";
