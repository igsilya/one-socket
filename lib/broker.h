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

#ifndef __ONE_SOCKET_BROKER_H
#define __ONE_SOCKET_BROKER_H

#include <stdbool.h>

struct client_info;

enum client_state {
    CLIENT_STATE_NEW,             /* Client just connected. */
    CLIENT_STATE_PAIR_REQUESTED,  /* GET_RAIR request received. */
    CLIENT_STATE_DEAD,            /* Some error appeared on connection. */
    CLIENT_STATE_COMPLETE,        /* SET_PAIR request sent. */
    CLIENT_STATE_VICTIM,          /* Client chosen to be disconnected. */
};

static inline const char *
client_state_str(enum client_state state)
{
    switch (state) {
        case CLIENT_STATE_NEW: return "NEW";
        case CLIENT_STATE_PAIR_REQUESTED: return "PAIR_REQUESTED";
        case CLIENT_STATE_DEAD: return "DEAD";
        case CLIENT_STATE_COMPLETE: return "COMPLETE";
        case CLIENT_STATE_VICTIM: return "VICTIM";
    };
    return "<None>";
}

static inline bool
client_waits_disconnection(enum client_state state)
{
    return state == CLIENT_STATE_DEAD     ||
           state == CLIENT_STATE_COMPLETE ||
           state == CLIENT_STATE_VICTIM;
}

int client_accept(int id, int listen_fd, struct client_info **client);
void client_destroy(struct client_info *);

enum client_state client_state(struct client_info *);
void client_state_set(struct client_info *, enum client_state);

int client_fd(struct client_info *);
const char * client_name(struct client_info *);

void client_recv_and_handle_request(int id, struct client_info *,
                                    struct client_info **clients,
                                    int n_clients);

#endif
