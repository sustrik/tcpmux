/*

  Copyright (c) 2015 Martin Sustrik

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation

  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom
  the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.

*/

#include <assert.h>
#include <libmill.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "tcpmux.h"

struct tcpmuxsock {
    int fd;
};

tcpmuxsock tcpmuxlisten(int port, const char *service, int64_t deadline) {
    /* Connect to tcpmuxd. */
    char fname[64];
    snprintf(fname, sizeof(fname), "/tmp/tcpmuxd.%d", port);
    unixsock s = unixconnect(fname);
    if(!s)
        return NULL;
    /* Send registration request to tcpmuxd. */
    unixsend(s, service, strlen(service), deadline);
    if(errno != 0)
        goto error;
    unixsend(s, "\r\n", 2, deadline);
    if(errno != 0)
        goto error;
    unixflush(s, deadline);
    if(errno != 0)
        goto error;
    /* Get reply from tcpmuxd. */
    char reply[256];
    size_t sz = unixrecvuntil(s, reply, sizeof(reply), "\n", 1, deadline);
    if(errno != 0 || sz < 3 || reply[sz - 2] != '\r' || reply[sz - 1] != '\n')
        goto error;
    if(reply[0] != '+') {
        unixclose(s);
        errno = EADDRINUSE; /* TODO: There are multiple reasons... */
        return NULL;
    }

    struct tcpmuxsock *res = malloc(sizeof(struct tcpmuxsock));
    if(!res) {
        unixclose(s);
        errno = ENOMEM;
        return NULL;
    }
    res->fd = unixdetach(s);
    assert(res->fd != -1);
    return res;
error:
    unixclose(s);
    errno = ECONNRESET;
    return NULL;
}

tcpsock tcpmuxaccept(tcpmuxsock s, int64_t deadline) {
    int rc = fdwait(s->fd, FDW_IN, deadline);
    if(rc == 0) {
        errno = ETIMEDOUT;
        return NULL;
    }
    if(rc & FDW_ERR)
        goto error;
    assert(rc & FDW_IN);
    /* Get one byte from TCPMUX daemon. A file descriptor should be attached. */
    char buf[1];
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    unsigned char control[1024];
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    rc = recvmsg(s->fd, &msg, 0);
    if(rc != 1 || buf[0] != 0x55)
        goto error;
    /* Loop over the auxiliary data to find the embedded file descriptor. */
    int fd = -1;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    while(cmsg) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type  == SCM_RIGHTS) {
            fd = *(int*)CMSG_DATA(cmsg);
            break;
        }
        cmsg = CMSG_NXTHDR(&msg, cmsg);
    }
    if(fd == -1)
        goto error;
    return tcpattach(fd, 0);
error:
    close(s->fd);
    s->fd = -1;
    errno = ECONNRESET;
    return NULL;
}

tcpsock tcpmuxconnect(ipaddr addr, const char *service, int64_t deadline) {
    tcpsock s = tcpconnect(addr, deadline);
    /* Send the TCPMUX request. */
    if(!s)
        return NULL;
    tcpsend(s, service, strlen(service), deadline);
    if(errno != 0)
        goto error;
    tcpsend(s, "\r\n", 2, deadline);
    if(errno != 0)
        goto error;
    tcpflush(s, deadline);
    if(errno != 0)
        goto error;
    /* Receive the TCPMUX reply. */
    char buf[256];
    size_t sz = tcprecvuntil(s, buf, sizeof(buf), "\n", 1, deadline);
    if(errno != 0)
        goto error;
    if(sz < 3 || buf[sz - 2] != '\r' || buf[sz - 1] != '\n' || buf[0] != '+') {
        errno = ECONNREFUSED;
        goto error;
    }
    return s;
error:;
    int err = errno;
    tcpclose(s);
    errno = err;
    return NULL;
}

void tcpmuxclose(tcpmuxsock s) {
    if(s->fd != -1)
        close(s->fd);
    free(s);
}

