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

#include "polling.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

int
poll_add(int id, int poll_fd, int fd, void *data, const char *name)
{
    struct epoll_event event;

    memset(&event, 0, sizeof event);
    event.events = EPOLLIN | EPOLLPRI;
    event.data.ptr = data;

    if (epoll_ctl(poll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        fprintf(stderr, "[%02d] Failed to add fd %d %s%s%s to epoll: %s\n",
                id, fd, name ? "(" : "", name ? name : "", name ? ")" : "",
                strerror(errno));
        return -1;
    }
    return 0;
}

int
poll_del(int id, int poll_fd, int fd, const char *name)
{
    if (epoll_ctl(poll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        fprintf(stderr, "[%02d] Failed to del fd %d %s%s%s from epoll: %s\n",
                id, fd, name ? "(" : "", name ? name : "", name ? ")" : "",
                strerror(errno));
        return -1;
    }
    return 0;
}

static void
poll_event_init(struct poll_event *event, bool error, void *data)
{
    event->error = error;
    event->data = data;
}

int
poll_wait_for_events(int id, int poll_fd,
                     struct poll_event *events, int max_events)
{
    struct epoll_event epoll_events[max_events];
    int i, n_events;

    do {
        n_events = epoll_wait(poll_fd, epoll_events, max_events, -1);
    } while ((n_events < 0 && errno == EINTR) || n_events == 0);

    if (n_events < 0) {
        fprintf(stderr, "[%02d] epoll_wait failed: %s\n",
                id, strerror(errno));
        return n_events;
    }

    for (i = 0; i < n_events; i++) {
        bool error = epoll_events[i].events & (EPOLLERR | EPOLLHUP);

        poll_event_init(&events[i], error, epoll_events[i].data.ptr);
    }
    return n_events;
}

int
poll_create(int id)
{
    int epoll_fd = epoll_create(1);

    if (epoll_fd < 0) {
        fprintf(stderr, "[%02d] Failed to create epoll: %s\n",
                id, strerror(errno));
    }
    return epoll_fd;
}

void
poll_destroy(int poll_fd)
{
    close(poll_fd);
}
