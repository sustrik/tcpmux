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

#if 0

struct registration {
    struct list_item item;
    const char *service;
    chan ch;
};

struct list registrations = {0};

void registration(unixsock us) {
    char service[256];
    int rc = handshake(NULL, us, service, sizeof(service));
    if(rc != 0)
        return;
    /* Check whether the service is already registered. */
    struct list_item *it;
    for(it = list_begin(&registrations); it; it = list_next(it)) {
        struct registration *r = cont(it, struct registration, item);
        if(strcmp(service, r->service) == 0)
            break;
    }
    if(it) {
         unixsend(us, "-Service already exists\r\n", 25, -1);
         if(errno != 0) goto error;
         unixflush(us, -1);
    error:
         unixclose(us);
         return;
    }
    /* Register the service. */
    struct registration r;
    r.service = service;
    r.ch = chmake(tcpsock, 0);
    list_insert(&registrations, &r.item, NULL);
    /* Wait for incoming TCP connections. */
    while(1) {
        tcpsock ts = chr(r.ch, tcpsock);
        /* Send the file descriptor to the registered process. */
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
        cmsg->cmsg_len = CMSG_LEN(sizeof(ts->fd));
        *((int*)CMSG_DATA(cmsg)) = ts->fd;
        msg.msg_controllen = cmsg->cmsg_len;
        rc = sendmsg(us->fd, &msg, 0);
        if (rc == -1) {
            assert(0); /* TODO */
        }
        assert(rc == 1);
        tcpclose(ts);
    }
}

#endif

struct sockmodel {
    struct sock_ {enum type_ {TYPE} type;} sock;
    int fd;
};

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
    const char *errmsg = NULL;
    /* Hacky-hack: Extract the underlying file descriptor! */
    int fd = ((struct sockmodel*)s)->fd;
    /* Get the first line (the service name) from the client. */
    char service[256];
    size_t sz = recvoneline(fd, service, sizeof(service));
    if(errno == ENOBUFS) {
        const char *errmsg = "-Service name too long\r\n";
        goto reply;
    }
    assert(errno == 0);
    size_t i;
    for(i = 0; i != sz; ++i) {
        if(service[i] < 32 || service[i] > 127) {
            errmsg = "-Service name contains invalid character\r\n";
            goto reply;
        }
        service[i] = tolower(service[i]);
    }
    /* TODO: Find the registration... */
    errmsg = "+\r\n";
reply:
    /* Reply to the TCP peer. */
    tcpsend(s, errmsg, strlen(errmsg), -1);
    if(errno != 0) {
        tcpclose(s);
        return;
    }
    tcpflush(s, -1);
    if(errno != 0) {
        tcpclose(s);
        return;
    }
    if(errmsg[0] == '-')
       return;
    /* Pass the socket to the registered service. */
    assert(0);
}

void tcplistener(tcpsock ls) {
    while(1) {
        tcpsock s = tcpaccept(ls, -1);
        go(tcphandler(s));
    }
}

void unixhandler(unixsock s) {
    const char *errmsg = NULL;
    /* Hacky-hack: Extract the underlying file descriptor! */
    int fd = ((struct sockmodel*)s)->fd;
    /* Get the first line (the service name) from the peer. */
    char service[256];
    size_t sz = recvoneline(fd, service, sizeof(service));
    if(errno == ENOBUFS) {
        const char *errmsg = "-Service name too long\r\n";
        goto reply;
    }
    assert(errno == 0);
    size_t i;
    for(i = 0; i != sz; ++i) {
        if(service[i] < 32 || service[i] > 127) {
            errmsg = "-Service name contains invalid character\r\n";
            goto reply;
        }
        service[i] = tolower(service[i]);
    }
    /* TODO: Check whether the reistration already exists. */
    errmsg = "+\r\n";
reply:
    /* Reply to the service. */
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
    /* TODO */
    assert(0);
}

int main(int argc, char *argv[]) {
    /*  Deal with the command line. */
    if(argc > 2) {
        fprintf(stderr, "usage: tcpmuxd [port]\n");
        return 1;
    }
    int port = 1;
    if(argc == 2) port = atoi(argv[1]);
    /* Start listening for the incoming connections. */
    ipaddr addr = iplocal(NULL, port, 0);
    if(errno != 0) {
        perror("Cannot resolve local network address");
        return 1;
    }
    tcpsock ls = tcplisten(addr, 10);
    if(!ls) {
        perror("Cannot bind to local network interface");
        return 1;
    }
    go(tcplistener(ls));
    /* Start listening for registrations from local processes. */
    char fname[64];
    snprintf(fname, sizeof(fname), "/tmp/tcpmuxd.%d", port);
    unixsock us = unixlisten(fname, 10);
    if(!us) {
        perror("Cannot bind to file");
        return 1;
    }
    /* Process new registrations as they arrive. */
    while(1) {
        unixsock s = unixaccept(us, -1);
        go(unixhandler(s));
    }
}

