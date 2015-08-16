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
#include <ctype.h>
#include <errno.h>
#include <libmill.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "list.h"
#include "tcpmux.h"

#define cont(ptr, type, member) \
    (ptr ? ((type*) (((char*) ptr) - offsetof(type, member))) : NULL)

struct service {
    struct tcpmux_list_item item;
    const char *name;
    chan ch;
};

struct tcpmux_list services = {0};

/* The function does no buffering. Any characters past the <CRLF> will
   remain in socket's rx buffer. */
size_t recvoneline(int fd, char *buf, size_t len) {
    size_t i;
    for(i = 0; i != len; ++i) {
        int rc = fdwait(fd, FDW_IN, -1);
        assert(rc == FDW_IN);
        ssize_t sz = recv(fd, &buf[i], 1, 0);
        assert(sz == 1);
        if(i > 0 && buf[i - 1] == '\r' && buf[i] == '\n') {
            buf[i - 1] = 0;
            errno = 0;
            return i - 1;
        }
    }
    errno = ENOBUFS;
    return len;
}

void tcphandler(tcpsock s) {
    int success = 0;
    /* Get the first line (the service name) from the client. */
    char service[256];
    int fd = tcpdetach(s);
    size_t sz = recvoneline(fd, service, sizeof(service));
    if(errno == ENOBUFS)
        goto reply;
    assert(errno == 0);
    size_t i;
    for(i = 0; i != sz; ++i) {
        if(service[i] < 32 || service[i] > 127)
            goto reply;
        service[i] = tolower(service[i]);
    }
    /* Find the registered service. */
    struct tcpmux_list_item *it;
    struct service *srvc;
    for(it = tcpmux_list_begin(&services); it; it = tcpmux_list_next(it)) {
        srvc = cont(it, struct service, item);
        if(strcmp(service, srvc->name) == 0)
            break;
    }
    if(!it)
        goto reply;
    success = 1;
reply:
    /* Reply to the TCP peer. */
    s = tcpattach(fd);
    const char *msg = success ? "+\r\n" : "-Service not found\r\n";
    tcpsend(s, msg, strlen(msg), -1);
    if(errno != 0) {
        tcpclose(s);
        return;
    }
    tcpflush(s, -1);
    if(errno != 0) {
        tcpclose(s);
        return;
    }
    if(!success) {
        tcpclose(s);
        return;
    }
    /* Send the fd to the unixhandler connected to the service in question. */
    chs(srvc->ch, int, tcpdetach(s));
}

void tcplistener(tcpsock ls) {
    while(1) {
        tcpsock s = tcpaccept(ls, -1);
        go(tcphandler(s));
    }
}

void unixhandler(unixsock s) {
    const char *errmsg = NULL;
    /* Get the first line (the service name) from the peer. */
    char service[256];
    int fd = unixdetach(s);
    size_t sz = recvoneline(fd, service, sizeof(service));
    if(errno == ENOBUFS) {
        const char *errmsg = "-1: Service name too long\r\n";
        goto reply;
    }
    assert(errno == 0);
    size_t i;
    for(i = 0; i != sz; ++i) {
        if(service[i] < 32 || service[i] > 127) {
            errmsg = "-2: Service name contains invalid character\r\n";
            goto reply;
        }
        service[i] = tolower(service[i]);
    }
    /* Check whether the service is already registered. */
    struct tcpmux_list_item *it;
    for(it = tcpmux_list_begin(&services); it; it = tcpmux_list_next(it)) {
        struct service *srvc = cont(it, struct service, item);
        if(strcmp(service, srvc->name) == 0)
            break;
    }
    if(it) {
        errmsg = "-3: Service already exists\r\n";
        goto reply;
    }
    struct service self;
    self.name = service;
    self.ch = chmake(int, 0);
    assert(self.ch);
    tcpmux_list_insert(&services, &self.item, NULL);
    errmsg = "+\r\n";
reply:
    /* Reply to the service. */
    s = unixattach(fd);
    if(!s) {
        unixclose(s);
        return;
    }
    unixsend(s, errmsg, strlen(errmsg), -1);
    if(errno != 0) {
        unixclose(s);
        return;
    }
    unixflush(s, -1);
    if(errno != 0) {
        unixclose(s);
        return;
    }
    if(errmsg[0] == '-')
       return;
    /* Wait for new incoming connections. Send them to the service. */
    fd = unixdetach(s);
    while(1) {
        int tcpfd = chr(self.ch, int);
        /* Send the fd to the serivce via UNIX connection. */
        struct iovec iov;
        unsigned char buf[] = {0x55};
        iov.iov_base = buf;
        iov.iov_len = 1;
        struct msghdr msg;
        memset(&msg, 0, sizeof (msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        char control [sizeof(struct cmsghdr) + 10];
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        struct cmsghdr *cmsg;
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(tcpfd));
        *((int*)CMSG_DATA(cmsg)) = tcpfd;
        msg.msg_controllen = cmsg->cmsg_len;
        int rc = sendmsg(fd, &msg, 0);
        if (rc != 1)
            close(tcpfd);
    }
}

int tcpmuxd(ipaddr addr) {
    tcpsock ls = tcplisten(addr, 10);
    if(!ls)
        return -1;
    /* Start listening for registrations from local services. */
    char fname[64];
    snprintf(fname, sizeof(fname), "/tmp/tcpmuxd.%d", tcpport(ls));
    /* This will kick the file from underneath a different instance of
       tcpmuxd using the same port. Unfortunately, the need for this behaviour
       is caused by a bug in POSIX and there's no real workaround.
       TODO: On Linux we may get around it by using abstract namespace. */
    unlink(fname);
    unixsock us = unixlisten(fname, 10);
    if(!us) {
        tcpclose(ls);
        return -1;
    }
    /* Start accepting TCP connections from clients. */
    go(tcplistener(ls));
    /* Process new registrations as they arrive. */
    while(1) {
        unixsock s = unixaccept(us, -1);
        go(unixhandler(s));
    }
}

