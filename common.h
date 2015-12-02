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

struct cs428_packet {
    cs428_seq_no_t sequence_number;
    bool is_content;
    union {
        struct {
            char filename[CS428_FILENAME_MAX];
            cs428_filesize_t filesize;
        };
        char content[CS428_CONTENT_SIZE];
    };
};

#endif // CS428_PACKET_H
