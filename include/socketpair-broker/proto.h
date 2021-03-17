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

#ifndef __SOCKET_PAIR_BROKER_PROTO_H
#define __SOCKET_PAIR_BROKER_PROTO_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

enum sp_broker_request {
    SP_BROKER_NONE = 0,
    SP_BROKER_GET_PAIR = 1,
    SP_BROKER_SET_PAIR = 2,
    SP_BROKER_MAX = 3
};

#define SP_BROKER_MAX_KEY_LENGTH 1024

enum sp_broker_get_pair_mode {
    SP_BROKER_PAIR_MODE_NONE = 0,
    SP_BROKER_PAIR_MODE_CLIENT = 1,
    SP_BROKER_PAIR_MODE_SERVER = 2,
    SP_BROKER_PAIR_MODE_MAX = 3
};

struct sp_broker_get_pair_request {
    uint16_t mode;     /* enum sp_broker_get_pair_mode */
    uint16_t key_len;
    uint8_t key[SP_BROKER_MAX_KEY_LENGTH];
} __attribute__((__packed__));

struct sp_broker_msg {
    uint32_t request;  /* enum sp_broker_request */
#define SP_BROKER_PROTOCOL_VERSION_MASK   0xf
    uint32_t flags;
    uint32_t size;     /* Size of the 'payload' below. */
    union {
        uint64_t u64;
        struct sp_broker_get_pair_request get_pair;
    } payload;
#define SP_BROKER_PROTOCOL_MAX_FDS        64
    int fds[SP_BROKER_PROTOCOL_MAX_FDS];
    int n_fds;
} __attribute__((__packed__));

/* Message size without space for file dscriptors. */
#define SP_BROKER_MESSAGE_SIZE  offsetof(struct sp_broker_msg, fds[0])

/* The supported version of a protocol. */
#define SP_BROKER_PROTOCOL_VERSION 0x1

#endif
