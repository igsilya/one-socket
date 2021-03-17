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

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <socketpair-broker/proto.h>
#include <socketpair-broker/helper.h>

#include "socket-util.h"

static int sp_broker_get_pair_validate(const struct sp_broker_msg *,
                                       char **err);

static void
set_error(char **err, const char *fmt, ...)
{
    const int max_size =  1024;
    char res[max_size];
    va_list ap;
    int len;

    if (!err) {
        return;
    }

    va_start(ap, fmt);
    len = vsnprintf(res, max_size, fmt, ap);
    va_end(ap);
    if (len < 0 || len >= max_size) {
        return;
    }

    *err = malloc(len + 1);
    if (!*err) {
        perror("Memory allocation failed");
        abort();
    }
    memcpy(*err, res, len + 1);
}

struct sp_broker_messages {
    int len;
    int n_fds;
    const char *name;
    int (*validate) (const struct sp_broker_msg *, char **);
} sp_broker_msgs[] = {
    [SP_BROKER_NONE]     = { .len = 0, .n_fds = 0, .name = "SP_BROKER_NONE", },
    [SP_BROKER_GET_PAIR] = { .len = sizeof (struct sp_broker_get_pair_request),
                             .n_fds = 0,
                             .name = "SP_BROKER_GET_PAIR",
                             .validate = sp_broker_get_pair_validate, },
    [SP_BROKER_SET_PAIR] = { .len = sizeof (uint64_t),
                             .n_fds = 1,
                             .name = "SP_BROKER_SET_PAIR", },
};

static int
sp_broker_get_pair_validate(const struct sp_broker_msg *msg, char **err)
{
    const struct sp_broker_get_pair_request *request = &msg->payload.get_pair;

    if (request->mode >= SP_BROKER_PAIR_MODE_MAX) {
        set_error(err, "Unexpected pair mode (%d)", request->mode);
        return -1;
    }

    if (!request->key_len || request->key_len > SP_BROKER_MAX_KEY_LENGTH) {
        set_error(err, "SP_BROKER_GET_PAIR: Invalid key length %"PRIu16
                       ". Valid range: [0-%d].",
                  request->key_len, SP_BROKER_MAX_KEY_LENGTH);
        return -1;
    }
    return 0;
}

int
sp_broker_message_validate(const struct sp_broker_msg *msg,
                           const enum sp_broker_request *expected,
                           int n_expected, char **err)
{
    uint32_t flags = msg->flags;

    if ((flags & SP_BROKER_PROTOCOL_VERSION_MASK)
        != SP_BROKER_PROTOCOL_VERSION) {
        set_error(err, "Request with unsupported protocol version 0x%"PRIx16
                       ". Supported version: 0x%"PRIx16,
                  flags & SP_BROKER_PROTOCOL_VERSION_MASK,
                  SP_BROKER_PROTOCOL_VERSION);
        return -1;
    }

    flags ^= msg->flags & SP_BROKER_PROTOCOL_VERSION_MASK;
    if (flags) {
        set_error(err,
                  "Request with unsupported protocol flags 0x%"PRIx32".",
                  flags);
        return -1;
    }

    if (msg->request == SP_BROKER_NONE || msg->request >= SP_BROKER_MAX) {
        set_error(err, "Unexpected request (%d)", msg->request);
        return -1;
    }

    if (msg->size != sp_broker_msgs[msg->request].len) {
        set_error(err, "Request %s: unexpected message size. "
                       "Expected: %d, Received: %d",
                  sp_broker_msgs[msg->request].name,
                  sp_broker_msgs[msg->request].len, msg->size);
       return -1;
    }

    if (msg->n_fds != sp_broker_msgs[msg->request].n_fds) {
        set_error(err, "Request %s: unexpected number of file descriptors. "
                       "Expected: %d, Received: %d",
                  sp_broker_msgs[msg->request].name,
                  sp_broker_msgs[msg->request].n_fds, msg->n_fds);
       return -1;
    }

    if (expected) {
        bool found = false;
        int i;

        for (i = 0; i < n_expected; i++) {
            if (msg->request == expected[i]) {
                found = true;
                break;
            }
        }
        if (!found) {
            set_error(err, "Unexpected request (%s)",
                      sp_broker_msgs[msg->request].name);
            return -1;
        }
    }

    if (sp_broker_msgs[msg->request].validate) {
        return sp_broker_msgs[msg->request].validate(msg, err);
    }

    return 0;
}

int
sp_broker_connect(const char *sock_path, bool nonblock, char **err)
{
    int broker_fd;

    broker_fd = socket_connect(sock_path, nonblock);
    if (broker_fd < 0) {
        set_error(err, "Failed to connect to broker on '%s': %s",
                  sock_path, strerror(errno));
        return -1;
    }
    return broker_fd;
}

static int
sp_broker_send_get_pair__(int broker_fd, const char *key,
                          enum sp_broker_get_pair_mode mode, char **err)
{
    struct sp_broker_msg msg;
    int key_len;

    key_len = strlen(key);

    memset(&msg, 0, sizeof msg);
    msg.request = SP_BROKER_GET_PAIR;
    msg.flags |= SP_BROKER_PROTOCOL_VERSION;
    msg.size = sizeof msg.payload.get_pair;
    msg.payload.get_pair.mode = mode;
    msg.payload.get_pair.key_len = key_len;
    memcpy(msg.payload.get_pair.key, key, key_len);

    if (socket_send_message(broker_fd, (char *) &msg, SP_BROKER_MESSAGE_SIZE,
                            NULL, 0) != SP_BROKER_MESSAGE_SIZE) {
        set_error(err, "Failed to send SP_BROKER_GET_PAIR: %s",
                  strerror(errno));
        return -1;
    }
    return 0;
}

int
sp_broker_send_get_pair(int broker_fd, const char *key,
                        bool server, char **err)
{
    return sp_broker_send_get_pair__(broker_fd, key,
                                     server ? SP_BROKER_PAIR_MODE_SERVER
                                            : SP_BROKER_PAIR_MODE_CLIENT,
                                     err);
}

int
sp_broker_send_get_pair_nondirectional(int broker_fd, const char *key,
                                       char **err)
{
    return sp_broker_send_get_pair__(broker_fd, key,
                                     SP_BROKER_PAIR_MODE_NONE, err);
}

int
sp_broker_receive_set_pair(int broker_fd, char **err)
{
    enum sp_broker_request expected = SP_BROKER_SET_PAIR;
    struct sp_broker_msg msg;
    int save_errno;
    char *err2;
    int i;

    if (socket_read_message(broker_fd, (char *) &msg, SP_BROKER_MESSAGE_SIZE,
                            msg.fds, sizeof msg.fds, &msg.n_fds)
        != SP_BROKER_MESSAGE_SIZE) {
        save_errno = errno;
        set_error(err, "Failed to read message from broker: %s",
                  errno ? strerror(errno) : "EOF");
        errno = save_errno;
        return -1;
    }

    if (sp_broker_message_validate(&msg, &expected, 1, &err2) < 0) {
        set_error(err, "Validation failed: %s", err2 ? err2 : "Unknown error");
        free(err2);
        for (i = 0; i < msg.n_fds; i++) {
            close(msg.fds[i]);
        }
        errno = EPROTO;
        return -1;
    }

    return msg.fds[0];
}


static int
sp_broker_get_pair__(const char *sock_path, const char *key,
                     bool directional, bool server, char **err)
{
    int peer_fd = -1;
    int broker_fd;
    int ret;

    broker_fd = sp_broker_connect(sock_path, false, err);
    if (broker_fd < 0) {
        return -1;
    }

    ret = directional
          ? sp_broker_send_get_pair(broker_fd, key, server, err)
          : sp_broker_send_get_pair_nondirectional(broker_fd, key, err);
    if (ret < 0) {
        goto exit_close;
    }

    peer_fd = sp_broker_receive_set_pair(broker_fd, err);
    if (peer_fd < 0) {
        goto exit_close;
    }

exit_close:
    close(broker_fd);
    return peer_fd;
}

int
sp_broker_get_pair(const char *sock_path, const char *key,
                   bool server, char **err)
{
    return sp_broker_get_pair__(sock_path, key, true, server, err);
}

int
sp_broker_get_pair_nondirectional(const char *sock_path, const char *key,
                                  char **err)
{
    return sp_broker_get_pair__(sock_path, key, false, false, err);
}
