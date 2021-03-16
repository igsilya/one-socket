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
#define DEFAULT_RUNDIR          "/var/run"

int
main(void)
{
    const char *sock_path = getenv("ONE_SOCKET_PATH");
    worker_handle_t worker;
    int ret;

    printf("One Socket v" VERSION_STR ".\n");

    if (sock_path && strlen(sock_path) >= PATH_MAX) {
        fprintf(stderr, "One socket path is too long (%s). "
                        "Falling back to default.\n",
                        sock_path);
        sock_path = NULL;
    }
    if (!sock_path) {
        sock_path = DEFAULT_RUNDIR"/"DEFAULT_SOCK_NAME;
    }

    worker = worker_thread_start(sock_path);
    if (!worker) {
        fprintf(stderr, "Failed to start worker thread.\n");
        exit(EXIT_FAILURE);
    }

    /* TODO: daemonize. */

    ret = worker_thread_join(worker);
    if (ret) {
        fprintf(stderr, "Failed to join worker thread: %s.\n", strerror(ret));
        exit(EXIT_FAILURE);
    }

    return 0;
}
