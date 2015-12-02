#ifndef CS428_PACKET_H
#define CS428_PACKET_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t cs428_seq_no_t;
typedef uint64_t cs428_filesize_t;
enum {
    CS428_FILENAME_MAX = 256,
    CS428_CONTENT_SIZE = 512,
};

typedef struct {
    cs428_seq_no_t sequence_number;
    bool is_content;
    union {
        struct {
            char filename[CS428_FILENAME_MAX];
            cs428_filesize_t filesize;
        };
        char content[CS428_CONTENT_SIZE];
    };
} cs428_packet_t ;

enum {
    CS428_PACKET_SIZE =
        8 // sequence number
        + 1 // packet type
        + 512, // content
    CS428_WINDOW_SIZE = 16,
    CS428_SERVER_PORT = 9999,
};

#endif // CS428_PACKET_H
