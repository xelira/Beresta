#pragma once

//: Сетевой рантайм для Linux: сокеты и строковые помощники.
//: Компилируется вместе с программой тем же cc, что и линковка.
inline const char* NET_RUNTIME_C = R"C(
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

extern void* b_gc_alloc(size_t size);

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
)C";
