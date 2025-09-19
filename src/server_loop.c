/* server_loop.c — poll() based echo + schema guard on port 1234 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <jansson.h>   /* -ljansson */

#define LISTEN_PORT 1234
#define MAX_CLIENTS 64
#define BUF_SIZE    8192

typedef struct {
    int   fd;
    size_t len;
    char  buf[BUF_SIZE];
} client_t;

static int set_reuseaddr(int fd) {
    int yes = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
}

static int make_listen_socket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    if (set_reuseaddr(fd) < 0) { perror("setsockopt(SO_REUSEADDR)"); close(fd); return -1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, 128) < 0) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

static void remove_client(struct pollfd *pfds, client_t *clients, int *nfds, int idx) {
    close(pfds[idx].fd);
    /* compact arrays: move last into idx (never move pfds[0]) */
    pfds[idx]    = pfds[*nfds - 1];
    clients[idx] = clients[*nfds - 1];
    (*nfds)--;
}

static int send_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/* Send {"status":"error","code":1300,"error":"invalid request schema"}\n */
static void send_invalid_schema(int fd) {
    json_t *obj = json_object();
    json_object_set_new(obj, "status", json_string("error"));
    json_object_set_new(obj, "code",   json_integer(1300));
    json_object_set_new(obj, "error",  json_string("invalid request schema"));

    char *s = json_dumps(obj, JSON_COMPACT);
    json_decref(obj);
    if (s) {
        (void)send_all(fd, s, strlen(s));
        (void)send_all(fd, "\n", 1);
        free(s);
    }
}

/* --- replace your handle_line() with this --- */
static void send_all_json(int fd, json_t *obj) {
    char *s = json_dumps(obj, JSON_COMPACT);
    if (s) { (void)send_all(fd, s, strlen(s)); (void)send_all(fd, "\n", 1); free(s); }
}

static void send_error_json(int fd, int code, const char *msg) {
    json_t *o = json_object();
    json_object_set_new(o, "status", json_string("error"));
    json_object_set_new(o, "code",   json_integer(code));
    json_object_set_new(o, "error",  json_string(msg));
    send_all_json(fd, o);
    json_decref(o);
}

static void send_ok_json(int fd, json_t *data /* may be NULL */) {
    json_t *o = json_object();
    json_object_set_new(o, "status", json_string("OK"));
    if (data) json_object_set(o, "data", data); /* borrowed */
    send_all_json(fd, o);
    json_decref(o);
}

static void handle_line(int fd, const char *line, size_t len) {
    /* strip CR/LF for parsing */
    const char *start = line;
    size_t plen = len;
    while (plen && (start[plen - 1] == '\n' || start[plen - 1] == '\r')) plen--;

    json_error_t jerr;
    json_t *root = json_loadb(start, plen, 0, &jerr);
    if (!root || !json_is_object(root)) {
        if (root) json_decref(root);
        send_error_json(fd, 1300, "invalid request schema");
        return;
    }

    json_t *cmd = json_object_get(root, "command");
    json_t *evt = json_object_get(root, "event");
    if (!(cmd && json_is_string(cmd)) && !(evt && json_is_string(evt))) {
        json_decref(root);
        send_error_json(fd, 1300, "invalid request schema");
        return;
    }

    /* Only process client commands here */
    if (cmd && json_is_string(cmd)) {
        const char *c = json_string_value(cmd);

        /* Very small dispatcher to demonstrate error mapping */
        if (strcmp(c, "login") == 0) {
            json_t *name = json_object_get(root, "player_name");
            json_t *pass = json_object_get(root, "password");
            if (!json_is_string(name) || !json_is_string(pass)) {
                send_error_json(fd, 1201, "missing required fields");
            } else {
                /* TODO: authenticate; for now, stub success */
                send_ok_json(fd, NULL);
            }
        }
        else if (strcmp(c, "PLAYERINFO") == 0) {
            json_t *pn = json_object_get(root, "player_num");
            if (!json_is_integer(pn)) {
                send_error_json(fd, 1201, "missing required fields");
            } else {
                /* TODO: fetch; stub OK */
                send_ok_json(fd, NULL);
            }
        }
        else if (strcmp(c, "SHIPINFO") == 0) {
            json_t *sn = json_object_get(root, "ship_num");
            if (!json_is_integer(sn)) {
                send_error_json(fd, 1201, "missing required fields");
            } else {
                send_ok_json(fd, NULL);
            }
        }
        else if (strcmp(c, "DESCRIPTION") == 0 ||
                 strcmp(c, "MYINFO") == 0 ||
                 strcmp(c, "ONLINE") == 0 ||
                 strcmp(c, "QUIT") == 0) {
            /* Stubs; plug auth checks later (->1101) */
            send_ok_json(fd, NULL);
        }
        else {
            /* Unknown command */
            send_error_json(fd, 1400, "unknown command");
        }
        json_decref(root);
        return;
    }

    /* If it's an 'event' from client (unexpected), reject */
    send_error_json(fd, 1300, "invalid request schema");
    json_decref(root);
}

int server_loop(volatile sig_atomic_t *running) {
    fprintf(stderr, "Server loop starting...\n");

    int listen_fd = make_listen_socket(LISTEN_PORT);
    if (listen_fd < 0) {
        fprintf(stderr, "Server loop exiting due to listen socket error.\n");
        return -1;
    }
    fprintf(stderr, "Listening on 0.0.0.0:%d\n", LISTEN_PORT);

    struct pollfd pfds[1 + MAX_CLIENTS];
    client_t      clients[1 + MAX_CLIENTS];
    int nfds = 1;

    pfds[0].fd      = listen_fd;
    pfds[0].events  = POLLIN;
    pfds[0].revents = 0;

    /* clients[0] is unused (slot 0 reserved for listen_fd) */
    memset(&clients, 0, sizeof(clients));

    while (*running) {
        int rc = poll(pfds, nfds, 1000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }
        if (rc == 0) continue;

        /* New connection */
        if (pfds[0].revents & POLLIN) {
            struct sockaddr_in cli;
            socklen_t clilen = sizeof(cli);
            int cfd = accept(listen_fd, (struct sockaddr *)&cli, &clilen);
            if (cfd >= 0) {
                if (nfds >= 1 + MAX_CLIENTS) {
                    close(cfd);
                } else {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                    fprintf(stderr, "Client connected: %s:%u (fd=%d)\n",
                            ip, (unsigned)ntohs(cli.sin_port), cfd);
                    pfds[nfds].fd      = cfd;
                    pfds[nfds].events  = POLLIN;
                    pfds[nfds].revents = 0;
                    clients[nfds].fd   = cfd;
                    clients[nfds].len  = 0;
                    nfds++;
                }
            }
        }

        /* Client I/O */
        for (int i = nfds - 1; i >= 1; --i) {
            if (!(pfds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)))
                continue;

            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                remove_client(pfds, clients, &nfds, i);
                continue;
            }

            if (pfds[i].revents & POLLIN) {
                char tmp[BUF_SIZE];
                ssize_t n = read(pfds[i].fd, tmp, sizeof(tmp));
                if (n <= 0) {
                    remove_client(pfds, clients, &nfds, i);
                    continue;
                }

                /* Append to client buffer; drop if overflow */
                client_t *cl = &clients[i];
                size_t avail = sizeof(cl->buf) - cl->len;
                size_t take  = (n <= (ssize_t)avail) ? (size_t)n : avail;
                memcpy(cl->buf + cl->len, tmp, take);
                cl->len += take;

                /* Process complete lines */
                size_t start = 0;
                for (size_t k = 0; k < cl->len; ++k) {
                    if (cl->buf[k] == '\n') {
                        size_t linelen = (k + 1) - start;
                        handle_line(pfds[i].fd, cl->buf + start, linelen);
                        start = k + 1;
                    }
                }

                /* Shift remaining partial line to front */
                if (start > 0) {
                    memmove(cl->buf, cl->buf + start, cl->len - start);
                    cl->len -= start;
                }

                /* If buffer is full and no newline seen, force an error reply and reset */
                if (cl->len == sizeof(cl->buf)) {
                    send_invalid_schema(pfds[i].fd);
                    cl->len = 0;
                }
            }
        }
    }

    for (int i = 1; i < nfds; ++i) close(pfds[i].fd);
    close(listen_fd);
    fprintf(stderr, "Server loop exiting...\n");
    return 0;
}
