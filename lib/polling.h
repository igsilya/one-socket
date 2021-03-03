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

#ifndef __ONE_SOCKET_POLLING_H
#define __ONE_SOCKET_POLLING_H

#include <stdbool.h>

struct poll_event {
    bool error;
    void *data;
};

int poll_add(int id, int poll_fd, int fd, void *data, const char *name);
int poll_del(int id, int poll_fd, int fd, const char *name);
int poll_wait_for_events(int id, int poll_fd,
                         struct poll_event *events, int max_events);
int poll_create(int id);
void poll_destroy(int poll_fd);

#endif
