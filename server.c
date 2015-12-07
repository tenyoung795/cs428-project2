#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

typedef struct cs428_session {
    struct sockaddr_in address;
    uint64_t first_seq_no;
    char filename[CS428_FILENAME_MAX];
    uint64_t filesize;

    char tmp_filename[sizeof("XXXXXX")];
    int fd;

    uint64_t last_received_frame;
    unsigned int window : CS428_WINDOW_SIZE;

    struct cs428_session *prev;
    struct cs428_session *next;
} cs428_session_t;

typedef struct {
    int fd;
    cs428_session_t *sessions;
} cs428_server_t;

static bool cs428_sockaddr_in_eq(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_port == b->sin_port
        && a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static uint64_t cs428_last_content_frame(uint64_t filesize) {
    return 1 // metadata packet
        + filesize / CS428_MAX_CONTENT_SIZE; // full content packets
}

static int cs428_write_all(int fd, const void *buf, size_t remaining) {
    while (remaining > 0) {
        ssize_t result = write(fd, buf, remaining);
        if (result < 0) return -errno;
        buf += result;
        remaining -= result;
    }
    return 0;
}

static cs428_session_t *cs428_find_session(cs428_session_t *head,
                                           const struct sockaddr_in *address) {
    for (cs428_session_t *iter = head; iter; iter = iter->next) {
        if (cs428_sockaddr_in_eq(address, &iter->address)) {
            return iter;
        }
    }
    return NULL;
}

static int cs428_session_prepend(cs428_session_t **head,
                                 const struct sockaddr_in *address,
                                 uint64_t first_seq_no,
                                 const char *filename,
                                 uint64_t filesize) {
    cs428_session_t *session = malloc(sizeof(*session));
    if (!session) return -errno;

    int result = 0;

    session->address = *address;
    session->first_seq_no = first_seq_no;
    memcpy(session->filename, filename, CS428_FILENAME_MAX);
    if (access(dirname(session->filename), W_OK)) {
        result = -errno;
        goto free_session;
    }
    memcpy(session->filename, filename, CS428_FILENAME_MAX);
    session->filesize = filesize;

    memcpy(session->tmp_filename, "XXXXXX", sizeof(session->tmp_filename));
    session->fd = mkstemp(session->tmp_filename);
    if (session->fd == -1) {
        result = -errno;
        goto free_session;
    }
    if (fallocate(session->fd, 0, 0, filesize) && ftruncate(session->fd, filesize)) {
        result = -errno;
        goto close_fd;
    }

    session->last_received_frame = 0;
    session->window = 0;

    session->prev = NULL;
    session->next = *head;
    *head = session;
    return 0;

    close_fd:
    close(session->fd);
    unlink(session->tmp_filename);

    free_session:
    free(session);
    return result;
}

static bool cs428_session_frame_received(const cs428_session_t *session,
                                         uint64_t frame) {
    return session->window & (1 << (frame % CS428_WINDOW_SIZE));
}

static int cs428_session_process_content(cs428_session_t *session,
                                         uint64_t seq_no,
                                         uint64_t content_size,
                                         const void *content) {
    uint64_t frame = seq_no - session->first_seq_no;
    uint64_t last_content_frame = cs428_last_content_frame(session->filesize);
    uint64_t window_end = session->last_received_frame + CS428_WINDOW_SIZE;
    uint64_t largest_frame_acceptable = window_end < last_content_frame
        ? window_end : last_content_frame;

    uint64_t expected_content_size = frame < last_content_frame
        ? CS428_MAX_CONTENT_SIZE
        : session->filesize % CS428_MAX_CONTENT_SIZE;
    if (content_size != expected_content_size) {
        return 0;
    }

    if (frame <= session->last_received_frame) {
        return 1;
    }

    if (frame > largest_frame_acceptable
        || cs428_session_frame_received(session, frame)) {
        return 0;
    }

    if (lseek(session->fd, (frame - 1) * CS428_MAX_CONTENT_SIZE, SEEK_SET) == -1) {
        return -errno;
    }
    int result = cs428_write_all(session->fd, content, content_size);
    if (result < 0) return result;

    session->window |= 1 << (frame % CS428_WINDOW_SIZE);

    bool should_ack = false;
    while (session->last_received_frame < largest_frame_acceptable) {
        uint64_t next_frame = session->last_received_frame + 1;
        if (!cs428_session_frame_received(session, next_frame)) break;

        session->window &= ~(1 << (next_frame % CS428_WINDOW_SIZE));
        if (next_frame == last_content_frame
            && (fsync(session->fd)
                || rename(session->tmp_filename, session->filename))) {
            return -errno;
        }
        ++session->last_received_frame;
        should_ack = true;
    }
    return should_ack;
}

static int cs428_server_init(cs428_server_t *server) {
    server->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server->fd == -1) {
        return -errno;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(CS428_SERVER_PORT),
        .sin_addr = { htonl(INADDR_ANY) },
    };
    if (bind(server->fd, (struct sockaddr *)&address, sizeof(address))) {
        int result = errno;
        close(server->fd);
        return -result;
    }

    server->sessions = NULL;
    return 0;
}

static void cs428_server_ack(const cs428_server_t *server, const cs428_session_t *session) {
    uint64_t ack_no = session->first_seq_no + session->last_received_frame + 1;
    uint64_t result = cs428_hton64(ack_no);
    if (sendto(server->fd, &result, sizeof(result), 0,
               (struct sockaddr *)&session->address, sizeof(session->address)) < 0) {
        perror("could not ack");
    }
}

static void cs428_server_run(cs428_server_t *server) {
    while (true) {
        char packet[CS428_MAX_PACKET_SIZE];
        struct sockaddr_in address;
        socklen_t address_size = sizeof(address);
        ssize_t result = recvfrom(server->fd, packet, sizeof(packet), 0,
                                  (struct sockaddr *)&address, &address_size);
        if (result == -1) {
            fprintf(stderr, "could not receive packet: %s\n", strerror(-result));
            continue;
        }

        if (result < CS428_MIN_PACKET_SIZE) {
            continue;
        }

        switch (packet[8]) {
        case CS428_METADATA: {
            if (result < CS428_START_PACKET_SIZE
                || packet[8 + 1 + CS428_FILENAME_MAX - 1] != '\0'
                || cs428_find_session(server->sessions, &address) != NULL) {
                break;
            }

            uint64_t seq_no = cs428_ntoh64(packet);
            const char *filename = packet + 8 + 1;
            uint64_t filesize = cs428_ntoh64(packet + 8 + 1 + CS428_FILENAME_MAX);
            int session_result = cs428_session_prepend(
                &server->sessions,
                &address,
                seq_no,
                filename, filesize);
            if (session_result) {
                fprintf(stderr, "could not create new session: %s\n", strerror(-session_result));
                break;
            }

            cs428_server_ack(server, server->sessions);
            break;
        }
        case CS428_CONTENT: {
            cs428_session_t *session = cs428_find_session(server->sessions, &address);
            if (session == NULL) {
                break;
            }

            uint64_t seq_no = cs428_ntoh64(packet);
            uint64_t content_size = result - 8 - 1;
            const void *content = packet + 8 + 1;
            uint64_t last_content_frame = cs428_last_content_frame(session->filesize);
            int session_result = cs428_session_process_content(session, seq_no,
                                                               content_size, content);
            if (session_result < 0) {
                fprintf(stderr, "could not process content: %s\n", strerror(-session_result));
                break;
            }

            if (session_result) {
                cs428_server_ack(server, session);
            }

            if (session->last_received_frame == last_content_frame) {
                uint64_t frame = seq_no - session->first_seq_no;
                if (frame != last_content_frame + 1) continue;
                
                if (session == server->sessions) {
                    server->sessions = session->next;
                } else {
                    session->prev->next = session->next;
                }
                close(session->fd);
                free(session);
            } 
            break;
        }
        default:
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    cs428_server_t server;
    int result = cs428_server_init(&server);
    if (result) {
        fprintf(stderr, "%s: could not open socket: %s\n", argv[0], strerror(-result));
        return EXIT_FAILURE;
    }
    cs428_server_run(&server);
}
