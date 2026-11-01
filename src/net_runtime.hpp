#pragma once

//: Сетевой рантайм для Linux: сокеты и строковые помощники.
//: Компилируется вместе с программой тем же cc, что и линковка.
inline const char* NET_RUNTIME_C = R"C(
#include <arpa/inet.h>
#include <ctype.h>
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
)C";
