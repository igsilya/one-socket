/*
 * Copyright (c) 2021 Ilya Maximets <i.maximets@ovn.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include "worker.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "broker.h"
#include "polling.h"
#include "socket-util.h"

#define DEFAULT_MAX_CLIENTS     1000

#define CONTROL_FD_DATA         0
#define LISTEN_FD_DATA          1

struct worker_thread_info {
    const int id;                 /* ID of the thread. */
    const pthread_t thread;       /* pthread handle. */
    int control_pipe[2];          /* pipe with the main thread. */
    char sock_path[PATH_MAX + 1]; /* Path of the listening socket. */
    pthread_mutex_t mutex;        /* Protects members of this structure. */
};

/* Tries to disconnect one client.  Returns 'true' on success.
 * On failure returns 'false'.  Caller will likely need to re-create polling
 * instance. */
static bool
disconnect_one_client(int id, int poll_fd,
                      struct client_info **clients, int *n_clients,
                      int index, const char *reason)
{
    int n = *n_clients;

    if (index >= n) {
        fprintf(stderr,
                "[%02d] client_disconnect: index (%d) >= n_clients (%d).\n",
                id, index, n);
        abort();
    }

    printf("[%02d] Disconnecting %s. Reason: %s.\n",
           id, client_name(clients[index]), reason);
    if (poll_del(id, poll_fd,
                 client_fd(clients[index]), client_name(clients[index]))) {
        fprintf(stderr, "[%02d] Failed to remove fd %d from polling.\n",
                id, client_fd(clients[index]));
        return false;
    }

    client_destroy(clients[index]);
    clients[index] = NULL;
    n--;
    if (index < n) {
        clients[index] = clients[n];
        clients[n] = NULL;
    }
    *n_clients = n;
    return true;
}

static int
get_new_poll(int id, int control_fd, int listen_fd, int *poll_fd)
{
    *poll_fd = poll_create(id);
    if (*poll_fd < 0) {
        goto err;
    }

    /* Adding control pipe to receive commands from the main thread. */
    if (poll_add(id, *poll_fd, control_fd,
                 (void *) CONTROL_FD_DATA, "control pipe")) {
        goto err_close;
    }

    /* Adding listening socket to accept clients. */
    if (poll_add(id, *poll_fd, listen_fd,
                 (void *) LISTEN_FD_DATA, "listening socket")) {
        goto err_close;
    }

    return 0;

err_close:
    poll_destroy(*poll_fd);
err:
    return -1;
}

static void *
worker_thread_main(void *aux_)
{
    struct worker_thread_info *worker = aux_;
    struct client_info **clients;
    struct poll_event *events;
    int max_events = DEFAULT_MAX_CLIENTS + 2;
    int listen_fd, control_fd, poll_fd;
    int n_clients;
    bool restart;
    int id;
    int i;

restart:
    restart = false;

    pthread_mutex_lock(&worker->mutex);
    id = worker->id;
    control_fd = worker->control_pipe[1];

    printf("[%02d] Worker thread %02d started.\n", id, id);

    listen_fd = socket_create_listening(worker->sock_path, true, true);
    if (listen_fd < 0) {
        fprintf(stderr, "[%02d] Failed to create socket (%s): %s\n",
                id, worker->sock_path, strerror(errno));
        pthread_mutex_unlock(&worker->mutex);
        goto exit_listen;
    }

    printf("[%02d] Serving on socket '%s'.\n", id, worker->sock_path);
    pthread_mutex_unlock(&worker->mutex);

    if (get_new_poll(id, control_fd, listen_fd, &poll_fd)) {
        goto exit_epoll_failure;
    }

    events = calloc(max_events, sizeof *events);
    clients = calloc(max_events, sizeof *clients);
    if (!events || !clients) {
        fprintf(stderr, "[%02d] Failed to allocate memory for clients: %s\n",
                id, strerror(errno));
        abort();
    }

    n_clients = 0;
    for (;;) {
        bool too_many_fds = false;
        int n_events;

        n_events = poll_wait_for_events(id, poll_fd, events, max_events);
        if (n_events < 0) {
            fprintf(stderr,
                    "[%02d] Polling failed. "
                    "Disconnecting all clients and restarting.\n",
                    id);
            restart = true;
            goto exit;
        }
#if DEBUG
        printf("--- Got %d polling events.\n", n_events);
#endif
        for (i = 0; i < n_events; i++) {
            struct poll_event *event = &events[i];
            struct client_info *client;

            if (event->data == (void *) CONTROL_FD_DATA) {
#if DEBUG
                printf("--- Control pipe event.\n");
#endif
                if (event->error) {
                    fprintf(stderr,
                            "[%02d] Control pipe failed. Aborting.\n", id);
                    abort();
                }
                /* TODO: read and handle control messages. */
                continue;
            } else if (event->data == (void *) LISTEN_FD_DATA) {
#if DEBUG
                printf("--- Listen event.\n");
#endif
                if (event->error) {
                    fprintf(stderr,
                            "[%02d] listening socket failed. "
                            "Disconnecting all clients and restarting.\n",
                            id);
                    restart = true;
                    goto exit;
                }
                /* Event on a listening socket.  Trying to accept clients. */
                if (client_accept(id, listen_fd, &clients[n_clients])) {
                    if (errno == EMFILE || errno == ENFILE) {
                        /* Maximum nuber of file descriptors reached.
                         * We will not be able to accept any new client but
                         * the process will wake up instantly from poll since
                         * there is an incoming connection.  Disconnecting
                         * one clinet to be able to accept the new one. */
                        too_many_fds = true;
                    }
                    continue;
                }

                if (!poll_add(id, poll_fd,
                              client_fd(clients[n_clients]),
                              clients[n_clients],
                              client_name(clients[n_clients]))) {
                    printf("[%02d] Accepted: %s.\n",
                           id, client_name(clients[n_clients]));
                    n_clients++;
                } else {
                    client_destroy(clients[n_clients]);
                    clients[n_clients] = NULL;
                }
                continue;
            }

            /* We have an event on client socket. */
            client = (struct client_info *) event->data;
#if DEBUG
            printf("--- New event from %s.\n", client_name(client));
#endif
            if (event->error) {
                printf("[%02d] Connection with %s is broken.\n",
                       id, client_name(client));
                client_state_set(client, CLIENT_STATE_DEAD);
                continue;
            }
            client_recv_and_handle_request(id, client, clients, n_clients);
        }

        if (too_many_fds || n_clients == max_events - 2) {
            /* Too many clients.  Randomly choosing a victim. */
            int victim = rand() % (n_clients ? n_clients : 1);

            client_state_set(clients[victim], CLIENT_STATE_VICTIM);
        }

        /* Cleanup completed and dead clients. */
        for (i = n_clients - 1; i >= 0; i--) {
            struct client_info *client = clients[i];
            enum client_state state = client_state(client);

            if (!client_waits_disconnection(state)) {
                continue;
            }
            if (!disconnect_one_client(id, poll_fd, clients, &n_clients, i,
                                       client_state_str(state))) {
                fprintf(stderr,
                        "[%02d] Disconnecting all clients and restarting.\n",
                        id);
                restart = true;
                goto exit;
            }
        }
#if DEBUG
        printf("--- Number of clients: %d.\n", n_clients);
#endif
    }

exit:
    for (i = 0; i < n_clients; i++) {
        client_destroy(clients[i]);
    }
    free(clients);
    free(events);
    poll_destroy(poll_fd);
exit_epoll_failure:
    close(listen_fd);
    if (restart) {
        goto restart;
    }
exit_listen:
    printf("[%02d] Worker thread stopped.\n", id);
    return NULL;
}

int
start_worker_thread(const char *sock_path)
{
    struct worker_thread_info *aux = calloc(1, sizeof *aux);
    static int counter = 1;
    pthread_t thread;
    int len;

    if (!aux) {
        perror("start_worker_thread: Failed to allocate memory");
        abort();
    }

    if (pthread_mutex_init(&aux->mutex, NULL)) {
        perror("start_worker_thread: Failed to initialize mutex");
        goto err;
    }
    pthread_mutex_lock(&aux->mutex);
    *((int *) &aux->id) = counter++;

    len = strnlen(sock_path, PATH_MAX);
    memcpy(aux->sock_path, sock_path, len);
    aux->sock_path[len] = '\0';

    if (pipe(aux->control_pipe)) {
        perror("start_worker_thread: Failed to create control pipe");
        goto err_unlock;
    }

    if (pthread_create(&thread, NULL, worker_thread_main, (void *) aux)) {
        perror("Can't start worker thread, pthread_create() failed");
        goto err_unlock;
    }

    *((pthread_t *) &aux->thread) = thread;
    pthread_mutex_unlock(&aux->mutex);
    return 0;

err_unlock:
    pthread_mutex_unlock(&aux->mutex);
    pthread_mutex_destroy(&aux->mutex);
err:
    free(aux);
    return -1;
}
