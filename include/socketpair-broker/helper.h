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

#ifndef __SOCKET_PAIR_BROKER_HELPER_H
#define __SOCKET_PAIR_BROKER_HELPER_H

#include <stdbool.h>

struct sp_broker_msg;
enum sp_broker_request;

/* Validates the message 'msg' to follow the SocketPair Broker Protocol.
 * If 'expected' array provided, also checks if the message is one of the
 * expected messages.  'n_expected' is the number of elements in 'expected'
 * array.
 *
 * Returns 0 on success.  On failure returns -1 and, if 'err' provided,
 * stores the error message there.  User takes the ownership of the error
 * message and should release it by calling free(). */
int sp_broker_message_validate(const struct sp_broker_msg *msg,
                               const enum sp_broker_request *expected,
                               int n_expected, char **err);

/* Connects to the SocketPair Broker on socket 'sock_path' and requests
 * a pair for a key 'key'.  Waits for all operations to finish.
 *
 * On success returns a file descriptor of a socket that could be used
 * to communicate with paired process that provided same 'key'.
 * On error returns -1 and, if 'err' provided, stores the error message there.
 * User takes the ownership of the error message and should release it by
 * calling free().*/
int sp_broker_get_pair(const char *sock_path, const char *key, char **err);

/* Connects to the SocketPair Broker on socket 'sock_path'.  If 'nonblock'
 * set to 'true', connects in nonblocking mode, otherwise waits for connection
 * establishment.
 *
 * On success returns a file descriptor of a new connection to the Broker.
 * If 'nonblock' is 'true', resulted socket has O_NONBLOCK set.
 * On failure returns -1 and sets errno.  If 'err' provided, stores the error
 * message there.  User takes the ownership of the error message and should
 * release it by calling free(). */
int sp_broker_connect(const char *sock_path, bool nonblock, char **err);

/* Sends SP_BROKER_GET_PAIR request with key 'key' to the  SocketPair Broker on
 * socket 'broker_fd'.
 *
 * On success returns 0.
 * On failure returns -1 and sets errno.  If 'err' provided, stores the error
 * message there.  User takes the ownership of the error message and should
 * release it by calling free(). */
int sp_broker_send_get_pair(int broker_fd, const char *key, char **err);

/* Attempts to receive SP_BROKER_SET_PAIR request from the  SocketPair Broker
 * on socket 'broker_fd'.
 *
 * On success returns file descriptor received from the Broker.
 * On failure returns -1 and sets errno.  If 'err' provided, stores the error
 * message there.  User takes the ownership of the error message and should
 * release it by calling free(). */
int sp_broker_receive_set_pair(int broker_fd, char **err);

#endif
