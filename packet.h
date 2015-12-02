#ifndef CS428_PACKET_H
#define CS428_PACKET_H

#include <stdbool.h>
#include <stdint.h>

struct cs428_packet {
    uint64_t sequence_number;
    bool is_content;
    union {
        struct {
            char filename[256];
            uint64_t filesize;
        };
        char content[512];
    };
};

#endif // CS428_PACKET_H
