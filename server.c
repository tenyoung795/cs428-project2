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
#include <time.h>
#include <unistd.h>

#include "common.h"

typedef struct cs428_session {
    struct sockaddr_in address;
    uint64_t first_seq_no;
    char filename[CS428_FILENAME_MAX];
    uint64_t filesize;

    char tmp_filename[sizeof("XXXXXX")];
    int fd;

    uint64_t last_frame_received;
    unsigned int window : CS428_WINDOW_SIZE;
    char frames[CS428_WINDOW_SIZE][CS428_MAX_CONTENT_SIZE];
    enum {
        CS428_STATE_INCOMPLETE,
        CS428_STATE_WRITTEN,
        CS428_STATE_SYNCED,
        CS428_STATE_RENAMED,
    } last_content_frame_state;

    struct cs428_session *prev;
    struct cs428_session *next;
} cs428_session_t;

typedef struct {
    int fd;
    cs428_session_t *sessions;

    char random_state[256];
    struct random_data random;
    double fraction_recv_dropped;
    double fraction_send_dropped;
} cs428_server_t;

static bool cs428_sockaddr_in_eq(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_port == b->sin_port
        && a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static uint64_t cs428_last_content_frame(uint64_t filesize) {
    return 1 // metadata packet
        + filesize / CS428_MAX_CONTENT_SIZE; // full content packets
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

    session->last_frame_received = 0;
    session->window = 0;
    session->last_content_frame_state = CS428_STATE_INCOMPLETE;

    session->prev = NULL;
    session->next = *head;
    *head = session;
    if (session->next) {
        session->next->prev = session;
    }
    return 0;

    close_fd:
    close(session->fd);
    unlink(session->tmp_filename);

    free_session:
    free(session);
    return result;
}

static bool cs428_session_slot_received(const cs428_session_t *session,
                                        size_t slot) {
    return session->window & (1 << slot);
}

static size_t cs428_session_slot(uint64_t frame) {
    return (frame - 1) % CS428_WINDOW_SIZE;
}

static int cs428_session_process_content(cs428_session_t *session,
                                         uint64_t seq_no,
                                         uint64_t content_size,
                                         const void *content) {
    uint64_t frame = seq_no - session->first_seq_no;
    uint64_t last_content_frame = cs428_last_content_frame(session->filesize);
    uint64_t window_end = session->last_frame_received + CS428_WINDOW_SIZE;
    uint64_t largest_frame_acceptable = window_end < last_content_frame
        ? window_end : last_content_frame;

    uint64_t expected_content_size = frame < last_content_frame
        ? CS428_MAX_CONTENT_SIZE
        : session->filesize % CS428_MAX_CONTENT_SIZE;
    size_t slot = cs428_session_slot(frame);
    if (content_size != expected_content_size
        || frame <= session->last_frame_received
        || frame > largest_frame_acceptable
        || cs428_session_slot_received(session, slot)) {
        return 0;
    }

    if (slot == 0 && frame > 1
        && write(session->fd, session->frames, sizeof(session->frames)) < 0) {
        return -errno;
    }

    session->window |= 1 << slot;
    memcpy(session->frames + slot, content, content_size);

    while (session->last_frame_received < largest_frame_acceptable) {
        if (!cs428_session_slot_received(session, slot)) break;
        session->window &= ~(1 << slot);
        slot = (slot + 1) % CS428_WINDOW_SIZE;
        ++session->last_frame_received;
    }

    return 0;
}

static int cs428_session_try_finish(cs428_session_t *session) {
    uint64_t last_content_frame = cs428_last_content_frame(session->filesize);
    if (session->last_frame_received != last_content_frame) {
        return 0;
    }

    switch (session->last_content_frame_state) {
    case CS428_STATE_INCOMPLETE: {
        size_t last_slot = cs428_session_slot(last_content_frame);
        uint64_t remainder = session->filesize % CS428_MAX_CONTENT_SIZE;
        size_t count = last_slot * sizeof(*session->frames) + remainder;
        if (write(session->fd, session->frames, count) < 0) {
            return -errno;
        }
        ++session->last_content_frame_state;
    }
    case CS428_STATE_WRITTEN:
        if (fsync(session->fd)) {
            return -errno;
        }
        ++session->last_content_frame_state;
    case CS428_STATE_SYNCED:
        if (rename(session->tmp_filename, session->filename)) {
            return- errno;
        }
        ++session->last_content_frame_state;
    case CS428_STATE_RENAMED:
        break;
    }
    return 1;
}

static int cs428_server_init(cs428_server_t *server, in_port_t port,
                             double fraction_recv_dropped, double fraction_send_dropped) {
    server->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server->fd == -1) {
        return -errno;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { htonl(INADDR_ANY) },
    };
    if (bind(server->fd, (struct sockaddr *)&address, sizeof(address))) {
        int result = errno;
        close(server->fd);
        return -result;
    }

    server->sessions = NULL;

    memset(&server->random, 0, sizeof(server->random));
    initstate_r((unsigned int)clock(),
                server->random_state, sizeof(server->random_state), &server->random);
    server->fraction_recv_dropped = fraction_recv_dropped;
    server->fraction_send_dropped = fraction_send_dropped;
    return 0;
}

static void cs428_server_ack(cs428_server_t *server, const cs428_session_t *session) {
    {
        int32_t random_result;
        random_r(&server->random, &random_result);
        if (random_result < server->fraction_send_dropped * RAND_MAX) {
            return;
        }
    }

    uint64_t ack_no = session->first_seq_no + session->last_frame_received
        - (session->last_frame_received == cs428_last_content_frame(session->filesize)
           && session->last_content_frame_state != CS428_STATE_RENAMED);
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

        {
            int32_t random_result;
            random_r(&server->random, &random_result);
            if (random_result < server->fraction_recv_dropped * RAND_MAX) {
                continue;
            }
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
            uint64_t previous_last_frame_received = session->last_frame_received;
            int session_result = cs428_session_process_content(session, seq_no,
                                                               content_size, content);
            if (session_result < 0) {
                fprintf(stderr, "error on processing content: %s\n", strerror(-session_result));
            }

            int finish_result = cs428_session_try_finish(session);
            if (finish_result < 0) {
                fprintf(stderr, "error on finishing session: %s\n", strerror(-finish_result));
            }

            uint64_t frame = seq_no - session->first_seq_no;
            if (frame <= previous_last_frame_received
                || session->last_frame_received > previous_last_frame_received
                || session->last_content_frame_state == CS428_STATE_RENAMED) {
                cs428_server_ack(server, session);
            }

            uint64_t last_content_frame = cs428_last_content_frame(session->filesize);
            if (finish_result && frame == last_content_frame + 1) {
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
    double fraction_recv_dropped = 0;
    double fraction_send_dropped = 0;

    bool done = false;
    while (!done) {
        switch (getopt(argc, argv, "r:s:")) {
        case -1:
            done = true;
            break;
        case 'r':
            fraction_recv_dropped = atof(optarg);
            break;
        case 's':
            fraction_send_dropped = atof(optarg);
            break;
        default:
            break;
        }
    }

    if (!argv[optind]) {
        fprintf(stderr, "Usage: %s [-r FRACTION_RECV_DROPPED] [-s FRACTION_SEND_DROPPED] PORT\n", argv[0]);
        return EXIT_FAILURE;
    }
    in_port_t port = atoi(argv[optind]);
    cs428_server_t server;
    int result = cs428_server_init(&server, port, fraction_recv_dropped, fraction_send_dropped);
    if (result) {
        fprintf(stderr, "%s: could not open socket: %s\n", argv[0], strerror(-result));
        return EXIT_FAILURE;
    }
    cs428_server_run(&server);
}
