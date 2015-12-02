#ifndef CS428_PACKET_H
#define CS428_PACKET_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t cs428_seq_no_t;
typedef uint64_t cs428_filesize_t;
typedef enum {
    CS428_METADATA = 0,
    CS428_CONTENT = 1,
} cs428_packet_type_t;
enum {
    CS428_FILENAME_MAX = 256,
    CS428_CONTENT_SIZE = 512,
};

typedef struct {
    char filename[CS428_FILENAME_MAX];
    char filesize[sizeof(cs428_filesize_t)];
} cs428_metadata_t;

typedef struct {
    char sequence_number[sizeof(cs428_seq_no_t)];
    char packet_type;
    union {
        cs428_metadata_t metadata;
        char content[CS428_CONTENT_SIZE];
    };
} cs428_packet_t;

enum {
    CS428_MIN_PACKET_SIZE =
        sizeof(cs428_seq_no_t) // sequence number
        + 1 // packet type
        + 1, // content
    CS428_START_PACKET_SIZE = 
        sizeof(cs428_seq_no_t) // sequence number
        + 1 // packet type
        + sizeof(cs428_metadata_t),
    CS428_WINDOW_SIZE = 16,
    CS428_SERVER_PORT = 9999,
};

#endif // CS428_PACKET_H
