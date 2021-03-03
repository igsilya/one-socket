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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "socket-util.h"

#include <socketpair-broker/helper.h>

int
main(int argc, char **argv)
{
    char *sock_path;
    char *key, *mode;
    int peer_fd;
    char *err;

    if (argc < 4) {
        printf("Usage: %s SP_BROKER_SOCKET KEY server|client\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    sock_path = argv[1];
    key = argv[2];
    mode = argv[3];

    peer_fd = sp_broker_get_pair(sock_path, key, &err);
    if (peer_fd < 0) {
        printf("Failed to get peer from broker on '%s': %s.\n",
               sock_path, err);
        free(err);
        exit(EXIT_FAILURE);
    }

    char buf;
    int ret;
    if (!strcmp(mode, "server")) {
        for (;;) {
            ret = socket_read_message(peer_fd, &buf, 1, NULL, 0, 0);
            if (ret <= 0) {
                perror("Failed to read");
                exit(EXIT_FAILURE);
            }
            printf("Received: %c\n", buf);
            buf += rand() % ('z' - buf);
            ret = socket_send_message(peer_fd, &buf, 1, NULL, 0);
            if (ret < 0) {
                perror("Failed to send");
                exit(EXIT_FAILURE);
            }
            printf("Sent    : %c\n", buf);
        }
    }
    while (scanf("%c", &buf) == 1) {
        if (!isalpha(buf)) {
            continue;
        }
        ret = socket_send_message(peer_fd, &buf, 1, NULL, 0);
        if (ret < 0) {
            perror("Failed to send");
            exit(EXIT_FAILURE);
        }
        printf("Sent    : %c\n", buf);
        ret = socket_read_message(peer_fd, &buf, 1, NULL, 0, 0);
        if (ret <= 0) {
            perror("Failed to read");
            exit(EXIT_FAILURE);
        }
        printf("Received: %c\n", buf);
    }

    return 0;
}
