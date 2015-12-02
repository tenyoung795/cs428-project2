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
    CS428_MAX_CONTENT_SIZE = 512,
    CS428_MIN_PACKET_SIZE =
        sizeof(cs428_seq_no_t) // sequence number
        + 1, // packet type
    CS428_MAX_PACKET_SIZE =
        sizeof(cs428_seq_no_t) // sequence number
        + 1 // packet type
        + CS428_MAX_CONTENT_SIZE, // content
    CS428_START_PACKET_SIZE = 
        sizeof(cs428_seq_no_t) // sequence number
        + 1 // packet type
        + CS428_FILENAME_MAX // filename
        + sizeof(cs428_filesize_t), // filesize
    CS428_WINDOW_SIZE = 16,
    CS428_SERVER_PORT = 9999,
};

#endif // CS428_PACKET_H
