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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "socket-util.h"
#include "worker.h"

#define DEFAULT_SOCK_NAME       "one.socket"
#define DEFAULT_CTL_SOCK_NAME   "one-socket.ctl"
#define DEFAULT_RUNDIR          "/var/run"

int
main(void)
{
    const char *ctl_sock_path = getenv("ONE_SOCKET_CTL_PATH");
    const char *sock_path = getenv("ONE_SOCKET_PATH");
    int control_fd;
    int ctl_client_fd;

    if (ctl_sock_path && strlen(ctl_sock_path) >= PATH_MAX) {
        fprintf(stderr, "Control socket path is too long (%s). "
                        "Falling back to default.\n",
                        ctl_sock_path);
        ctl_sock_path = NULL;
    }
    if (!ctl_sock_path) {
        ctl_sock_path = DEFAULT_RUNDIR"/"DEFAULT_CTL_SOCK_NAME;
    }

    control_fd = socket_create_listening(ctl_sock_path, true, false);
    if (control_fd < 0) {
        fprintf(stderr, "Failed to create control socket (%s): %s\n",
                ctl_sock_path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("One Socket v" VERSION_STR " started with control socket '%s'\n",
           ctl_sock_path);


    if (sock_path && strlen(sock_path) >= PATH_MAX) {
        fprintf(stderr, "One socket path is too long (%s). "
                        "Falling back to default.\n",
                        sock_path);
        sock_path = NULL;
    }
    if (!sock_path) {
        sock_path = DEFAULT_RUNDIR"/"DEFAULT_SOCK_NAME;
    }

    if (start_worker_thread(sock_path)) {
        fprintf(stderr, "Failed to start worker thread.\n");
        exit(EXIT_FAILURE);
    }

    /* TODO: daemonize. */

    for (;;) {
        ctl_client_fd = socket_accept(control_fd);
        if (ctl_client_fd < 0) {
            perror("accept() failed on control socket");
            continue;
        }

        /* TODO: Receive the message and reply on control commands. */

        close(ctl_client_fd);
    }

    return 0;
}
