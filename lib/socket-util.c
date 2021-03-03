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

#include "socket-util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_LISTEN_BACKLOG      64

/* Sets nonblocking mode for the socket 'fd'.  Returns 0 on success.
 * On error, -1 is returned and 'errno' is set.
 * If 'name' provided, it will be shown in a error message. */
int
socket_set_nonblock(int fd, const char *name)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK)) {
        int save_errno = errno;

        fprintf(stderr, "%s: Faied to set nonblocking mode for the socket "
                "%d %s%s%s: %s.\n", __func__, fd,
                name ? "(" : "", name ? name : "", name ? ")" : "",
                strerror(save_errno));
        errno = save_errno;
        return -1;
    }
    return 0;
}

/* Attempts to accept connection on  alistening socket 'fd'. */
int
socket_accept(int fd)
{
    return accept(fd, NULL, NULL);
}

/* Attempts to create a pair of connected unix domain sockets. */
int
socket_pair_get(int sp[2])
{
    return socketpair(AF_UNIX, SOCK_STREAM, 0, sp);
}

/* Creates a socket using path 'path'.  If 'nonblock' equals 'true', sets
 * nonblocking mode.
 *
 * On success: returns a file descriptor, 'un' filled with the socket info.
 * On failure returns -1 and errno indicates the error. */
static int
socket_create(const char *path, bool nonblock, struct sockaddr_un *un)
{
    int save_errno;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        save_errno = errno;
        fprintf(stderr, "%s: Failed to create socket: %s.\n", __func__,
                strerror(errno));
        goto out_error;
    }

    if (nonblock && socket_set_nonblock(fd, path)) {
        save_errno = errno;
        goto out_fd_error;
    }

    memset(un, 0, sizeof *un);
    un->sun_family = AF_UNIX;
    strncpy(un->sun_path, path, sizeof un->sun_path);
    un->sun_path[sizeof un->sun_path  - 1] = '\0';

    return fd;
out_fd_error:
    close(fd);
out_error:
    errno = save_errno;
    return -1;
}

/* Creates a socket using path 'path'.  If 'force' equals 'true', unlinks
 * the existing socket file first.  If 'nonblock' equals 'true', sets
 * nonblocking mode.
 *
 * Returns a file descriptor on success.  On failure returns -1 and errno
 * indicates the error. */
int
socket_create_listening(const char *path, bool force, bool nonblock)
{
    struct sockaddr_un un;
    int save_errno;
    int fd;

    fd = socket_create(path, nonblock, &un);
    if (fd < 0) {
        return -1;;
    }

    if (force) {
        unlink(path);
    }

    if (bind(fd, (struct sockaddr *) &un, sizeof un)) {
        save_errno = errno;
        fprintf(stderr, "%s: bind() failed: %s.\n", __func__, strerror(errno));
        goto out_fd_error;
    }

    if (listen(fd, MAX_LISTEN_BACKLOG)) {
        save_errno = errno;
        fprintf(stderr, "%s: listen() failed: %s/\n",
                __func__, strerror(errno));
        goto out_fd_error;
    }

    return fd;
out_fd_error:
    close(fd);
    errno = save_errno;
    return -1;
}

/* Creates a new client socket and connects to socket 'path'.  If 'nonblock'
 * is 'true' blocks until connection established, otherwise returns instantly.
 * On success returns new socket file descriptor, if 'nonblock' is 'true',
 * resulted socket has O_NONBLOCK set.
 * On failure returns -1 and sets appropriate errno. */
int
socket_connect(const char *path, bool nonblock)
{
    struct sockaddr_un un;
    int ret;
    int fd;

    fd = socket_create(path, nonblock, &un);
    if (fd < 0) {
        return -1;;
    }

    do {
        ret = connect(fd, (struct sockaddr *) &un, sizeof un);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

#define MAX_FDS 64

/* Reads a message from socket 'fd' to bufer 'buf'.  If 'fds' and 'n_fds' are
 * not NULL and 'fds_len' is larger than zero, additioanlly receives all file
 * descriptors passed with this message.  File descriptors returned to the
 * caller in 'fds' array, number of received descriptors returned in 'n_fds'.
 *
 * On success returns number of received bytes.
 * On failure returns -1 and sets errno appropriately. */
int
socket_read_message(int fd, char *buf, int buflen,
                    int *fds, int fds_len, int *n_fds)
{
    union {
        char control[CMSG_SPACE(MAX_FDS * sizeof(int))];
        struct cmsghdr align;
    } u;
    struct cmsghdr *cmsg;
    struct msghdr msgh;
    struct iovec iov;
    bool receive_fds;
    int ret;
    int i;

    memset(&msgh, 0, sizeof msgh);
    iov.iov_base = buf;
    iov.iov_len  = buflen;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    receive_fds = (fds && n_fds && fds_len > 0);

    if (receive_fds) {
        *n_fds = 0;

        if (fds_len > MAX_FDS) {
            fds_len = MAX_FDS;
        }
        msgh.msg_control = u.control;
        msgh.msg_controllen = CMSG_SPACE(fds_len * sizeof(int));
    }

    do {
        ret = recvmsg(fd, &msgh, 0);
    } while (ret < 0 && errno == EINTR);

    if (!ret) {
        return 0;
    } else if (ret < 1) {
        return -1;
    }

    if (receive_fds) {
        for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL;
             cmsg = CMSG_NXTHDR(&msgh, cmsg)) {
            if ((cmsg->cmsg_level == SOL_SOCKET) &&
                (cmsg->cmsg_type == SCM_RIGHTS)) {
                *n_fds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                memcpy(fds, CMSG_DATA(cmsg), *n_fds * sizeof(int));
                break;
            }
        }
    }

    if (msgh.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
        if (receive_fds) {
            /* Closing all the received file descriptors. */
            for (i = 0; i < *n_fds; i++) {
                close(fds[i]);
            }
        }
        errno = EIO;
        return -1;
    }

    return ret;
}

/* Sends message containing first 'buflen' bytes from 'buf' and first 'n_fds'
 * file descriptors from 'fds'.  If 'fds' is NULL or 'n_fds' is zero, no
 * doesn't send any file descriptors.
 *
 * On success returns number of bytes sent.
 * On failure returns -1 and sets errno appropriately. */
int
socket_send_message(int fd, char *buf, int buflen, int *fds, int n_fds)
{
    union {
        char control[CMSG_SPACE(MAX_FDS * sizeof *fds)];
        struct cmsghdr align;
    } u;
    struct cmsghdr *cmsg;
    struct msghdr msgh;
    struct iovec iov;
    int ret;

    memset(&msgh, 0, sizeof msgh);
    iov.iov_base = buf;
    iov.iov_len  = buflen;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    if (fds && n_fds > 0) {
        if (n_fds > MAX_FDS) {
            errno = E2BIG;
            return -1;
        }
        msgh.msg_control = u.control;
        msgh.msg_controllen = CMSG_SPACE(n_fds * sizeof *fds);
        cmsg = CMSG_FIRSTHDR(&msgh);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(n_fds * sizeof *fds);
        memcpy(CMSG_DATA(cmsg), fds, n_fds * sizeof *fds);
    }

    do {
        ret = sendmsg(fd, &msgh, MSG_NOSIGNAL);
    } while (ret < 0 && errno == EINTR);

    if (ret < 1) {
        return -1;
    }
    return ret;
}
