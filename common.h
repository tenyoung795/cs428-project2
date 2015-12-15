#ifndef CS428_PACKET_H
#define CS428_PACKET_H

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    CS428_METADATA = 0,
    CS428_CONTENT = 1,
} cs428_packet_type_t;

enum {
    CS428_FILENAME_MAX = 256,
    CS428_MAX_CONTENT_SIZE = 512,
    CS428_MIN_PACKET_SIZE =
        8 // sequence number
        + 1, // packet type
    CS428_MAX_PACKET_SIZE =
        8 // sequence number
        + 1 // packet type
        + CS428_MAX_CONTENT_SIZE, // content
    CS428_START_PACKET_SIZE = 
        8 // sequence number
        + 1 // packet type
        + CS428_FILENAME_MAX // filename
        + 8, // filesize
    CS428_WINDOW_SIZE = 16,
};

static uint64_t cs428_ntoh64(const void *x) {
    uint32_t from[2];
    memcpy(from, x, 8);
    uint64_t a = ntohl(from[0]);
    uint32_t b = ntohl(from[1]);
    return (a << 32) | b;
}

static uint64_t cs428_hton64(uint64_t x) {
    union {
        uint32_t from[2];
        uint64_t to;
    } result;
    result.from[0] = htonl((uint32_t)(x >> 32));
    result.from[1] = htonl((uint32_t)x);
    return result.to;
}

#endif // CS428_PACKET_H
