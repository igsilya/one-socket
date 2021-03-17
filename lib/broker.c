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

#include "broker.h"

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

#include "socket-util.h"

#include <socketpair-broker/proto.h>
#include <socketpair-broker/helper.h>

#define CLIENT_NAME_MAX 1024

struct client_info {
    int fd;                                 /* File descriptor. */
    enum client_state state;                /* Current state. */
    enum sp_broker_get_pair_mode mode;      /* NONE, CLIENT or SERVER. */
    int key_len;                            /* 'key' length. */
    uint8_t key[SP_BROKER_MAX_KEY_LENGTH];  /* Key to find a pair. */
    char name[CLIENT_NAME_MAX];             /* Client name for logs. */
};

enum client_state
client_state(struct client_info *info)
{
    return info->state;
}

void
client_state_set(struct client_info *info, enum client_state state)
{
    info->state = state;
}

int
client_fd(struct client_info *info)
{
    return info->fd;
}

const char *
client_name(struct client_info *info)
{
    return info->name;
}

int
client_accept(int id, int listen_fd, struct client_info **info)
{
    int client_fd = socket_accept(listen_fd);
    static __thread int seq_no = 0;

    if (client_fd < 0) {
        fprintf(stderr, "[%02d] accept() failed: %s\n", id, strerror(errno));
        return -1;
    }

    if (socket_set_nonblock(client_fd, "client")) {
        close(client_fd);
        return -1;
    }

    *info = calloc(1, sizeof **info);
    if (!*info) {
        fprintf(stderr, "[%02d] Failed to allocate memory for a client: %s\n",
                id, strerror(errno));
        abort();
    }
    (*info)->fd = client_fd;
    (*info)->state = CLIENT_STATE_NEW;
    (*info)->mode = SP_BROKER_PAIR_MODE_MAX;
    snprintf((*info)->name, CLIENT_NAME_MAX, "client-%02d-%04d-%04d",
             id, seq_no++, client_fd);
    return 0;
}

void
client_destroy(struct client_info *info)
{
    if (!info) {
        return;
    }
    close(info->fd);
    free(info);
}

static int
client_send_msg(int id, struct client_info *info, struct sp_broker_msg *msg)
{
    if (socket_send_message(info->fd, (char *) msg, SP_BROKER_MESSAGE_SIZE,
                            msg->fds, msg->n_fds) < 0) {
        printf("[%02d] Failed to send SP_BROKER_SET_PAIR request to %s: %s.\n",
               id, info->name, strerror(errno));
        return -1;
    }
    return 0;
}

static int
client_recv_msg(int id, struct client_info *info, struct sp_broker_msg *msg)
{
    if (socket_read_message(info->fd, (char *) msg, SP_BROKER_MESSAGE_SIZE,
                            msg->fds, sizeof msg->fds, &msg->n_fds) < 0) {
        printf("[%02d] Failed to receive message from %s: %s.\n",
               id, info->name, strerror(errno));
        return -1;
    }
    return 0;
}

static bool
client_match(const struct client_info *client,
             const struct sp_broker_get_pair_request *get_pair)
{
    if (client->mode >= SP_BROKER_PAIR_MODE_MAX) {
        return false;
    }

    /* Both modes should be NONE or they should be opposite. */
    if (client->mode == SP_BROKER_PAIR_MODE_NONE
        || get_pair->mode == SP_BROKER_PAIR_MODE_NONE) {
        if (client->mode != get_pair->mode) {
            return false;
        }
    } else if (client->mode == get_pair->mode) {
        return false;
    }

    return client->key_len == get_pair->key_len &&
           !memcmp(client->key, get_pair->key, get_pair->key_len);
}

static struct client_info *
client_lookup(struct client_info **clients, int n_clients,
              const struct sp_broker_get_pair_request *get_pair)
{
    int i;

    /* Might be slow.  TODO: Optimize with hashes or hash maps. */
    for (i = 0; i < n_clients; i++) {
        if (clients[i]->state == CLIENT_STATE_PAIR_REQUESTED
            && client_match(clients[i], get_pair)) {
            return clients[i];
        }
    }
    return NULL;
}

static int
client_create_and_send_socketpair(int id, struct client_info *a,
                                          struct client_info *b)
{
    struct sp_broker_msg msg;
    int ret = 0;
    int sp[2];

    printf("[%02d] Creating socket pair for %s and %s.\n",
                id, client_name(a), client_name(b));

    if (socket_pair_get(sp)) {
        fprintf(stderr, "[%02d] Failed to create socketpair: %s.\n",
                id, strerror(errno));
        /* We can't just leave both clients in PAIR_REQUESTED state because
         * we will never match them again.  Closing both to trigger re-connect.
         * Maybe they will be lucky net time.  */
        a->state = CLIENT_STATE_DEAD;
        b->state = CLIENT_STATE_DEAD;
        return -1;
    }

    memset(&msg, 0, sizeof msg);
    msg.request = SP_BROKER_SET_PAIR;
    msg.flags |= SP_BROKER_PROTOCOL_VERSION;
    msg.size = sizeof msg.payload.u64;
    msg.n_fds = 1;
    msg.fds[0] = sp[0];

    if (client_send_msg(id, a, &msg) < 0) {
        /* We have a chance to keep one of the clients.  Ony marking failed
         * one as dead. */
        a->state = CLIENT_STATE_DEAD;
        ret = -1;
    }

    msg.fds[0] = sp[1];
    if (client_send_msg(id, b, &msg) < 0) {
        /* We already sent reply to one of the clients, need to close them
         * both so both will reconnect. */
        a->state = CLIENT_STATE_DEAD;
        b->state = CLIENT_STATE_DEAD;
        ret = -1;
    }

    /* Closing the socket pair from our side. */
    close(sp[0]);
    close(sp[1]);

    if (!ret) {
        a->state = CLIENT_STATE_COMPLETE;
        b->state = CLIENT_STATE_COMPLETE;
    }
    return ret;
}

static const char *
pair_mode_str(enum sp_broker_get_pair_mode mode)
{
    switch (mode) {
        case SP_BROKER_PAIR_MODE_NONE:   return "none";
        case SP_BROKER_PAIR_MODE_CLIENT: return "client";
        case SP_BROKER_PAIR_MODE_SERVER: return "server";
        default: return "<unknown>";
    }
}

static int
client_handle_get_pair(int id, struct client_info *info,
                       struct sp_broker_msg *msg,
                       struct client_info **clients, int n_clients)
{
    struct client_info *pair;

    if (info->state != CLIENT_STATE_NEW) {
        printf("[%02d] Unexpected request SP_BROKER_GET_PAIR from %s.  "
               "Key is already set.\n", id, info->name);
        return -1;
    }

    /* Looking for pair before updating info for the current client to avoid
     * finding it.  */
    pair = client_lookup(clients, n_clients, &msg->payload.get_pair);

    /* Updating info for the current client.  */
    info->mode = msg->payload.get_pair.mode;
    info->key_len = msg->payload.get_pair.key_len;
    memcpy(info->key, msg->payload.get_pair.key, info->key_len);
    info->state = CLIENT_STATE_PAIR_REQUESTED;

    printf("[%02d] %s: key received, mode: %s.\n",
           id, client_name(info), pair_mode_str(info->mode));

    if (pair) {
        /* Pair found! */
        return client_create_and_send_socketpair(id, pair, info);
    }
    return 0;
}

void
client_recv_and_handle_request(int id, struct client_info *info,
                               struct client_info **clients, int n_clients)
{
    enum sp_broker_request supported_requests[] = { SP_BROKER_GET_PAIR, };
    struct sp_broker_msg msg;
    int i, result = 0;
    char *err;

    memset(&msg, 0, sizeof msg);
    if (client_recv_msg(id, info, &msg) < 0) {
        info->state = CLIENT_STATE_DEAD;
        goto exit;
    }

    result = sp_broker_message_validate(&msg, supported_requests,
                                        sizeof supported_requests, &err);
    if (result) {
        printf("[%02d] %s: Protocol error: %s.\n",
               id, info->name, err ? err : "Unknown error");
        free(err);
        info->state = CLIENT_STATE_DEAD;
        goto exit;
    }

    if (msg.request != SP_BROKER_GET_PAIR) {
        /* We're not supporting any other types of requsts and validation
         * went wrong. */
        abort();
    }

    if (client_handle_get_pair(id, info, &msg, clients, n_clients)) {
        info->state = CLIENT_STATE_DEAD;
    }

exit:
    /* Closing all received file descriptors if any. */
    for (i = 0; i < msg.n_fds; i++) {
        close(msg.fds[i]);
    }
}
