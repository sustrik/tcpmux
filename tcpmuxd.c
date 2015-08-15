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
#include <errno.h>
#include <libmill.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

/******************************************************************************/
/* Doubly linked list                                                         */
/******************************************************************************/

#define cont(ptr, type, member) \
    (ptr ? ((type*) (((char*) ptr) - offsetof(type, member))) : NULL)

struct list_item {
    struct list_item *next;
    struct list_item *prev;
};

struct list {
    struct list_item *first;
    struct list_item *last;
};

struct list_item *list_begin(struct list *self) {
    return self->first;
}

struct list_item *list_next(struct list_item *it) {
    return it->next;
}

void list_insert(struct list *self, struct list_item *item,
      struct list_item *it) {
    assert(0);
}

struct list_item *list_erase(struct list *self, struct list_item *item) {
    assert(0);
}

/******************************************************************************/
/* tcpmuxd                                                                    */
/******************************************************************************/

int handshake(tcpsock ts, unixsock us, char *service, size_t len) {
    const char *errmsg = NULL;
    size_t sz;
    if(ts)
        sz = tcprecvuntil(ts, service, sizeof(service), "\r", 1, -1);
    else
        sz = unixrecvuntil(us, service, sizeof(service), "\r", 1, -1);
    if(errno == ENOBUFS) {
        errmsg = "Service name too long";
        goto error1;
    }
    if(errno != 0) {
        assert(0); /* TODO: Retry... */
    }
    service[sz - 1] = 0;
    size_t i;
    for(i = 0; i != sz - 1; ++i) {
        if(service[i] < 32 || service[i] > 127) {
            errmsg = "Service name contains invalid character";
            goto error1;
        }
    }
    char c;
    if(ts)
        tcprecv(ts, &c, 1, -1);
    else
        unixrecv(us, &c, 1, -1);
    if(errno != 0 || c != '\n') {
        errmsg = "Service name contains invalid character";
        goto error1;
    }
    return 0;

error1:
    if(ts)
        tcpsend(ts, "-", 1, -1);
    else
        unixsend(us, "-", 1, -1);
    if(errno != 0) goto error2;
    if(ts)
        tcpsend(ts, errmsg, strlen(errmsg), -1);
    else
        unixsend(us, errmsg, strlen(errmsg), -1);
    if(errno != 0) goto error2;
    if(ts)
        tcpsend(ts, "\r\n", 2, -1);
    else
        unixsend(us, "\r\n", 2, -1);
    if(errno != 0) goto error2;
    if(ts)
        tcpflush(ts, -1);
    else
        unixflush(us, -1);
error2:
    if(ts)
        tcpclose(ts);
    else
        unixclose(us);
    return -1;
}

struct registration {
    struct list_item item;
    const char *service;
    chan ch;
};

struct list registrations = {0};

void tcphandler(tcpsock ts) {
    char service[256];
    int rc = handshake(ts, NULL, service, sizeof(service));
    if(rc != 0)
        return;
    /* Find the service in the registration list. */
    struct list_item *it;
    chan ch;
    for(it = list_begin(&registrations); it; it = list_next(it)) {
        struct registration *r = cont(it, struct registration, item);
        if(strcmp(service, r->service) == 0) {
            ch = r->ch;
            break;
        }
    }
    if(!it) {
         tcpsend(ts, "-Unknown serice\r\n", 17, -1);
         if(errno != 0) goto error;
         tcpflush(ts, -1);
    error:
         tcpclose(ts);
         return;
    }
    /* Pass the TCP socket to the service handler. */
    chs(ch, tcpsock, ts);
}

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

void tcplistener(tcpsock ls) {
    while(1) {
        tcpsock s = tcpaccept(ls, -1);
        go(tcphandler(s));
    }
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
    /* Start listening for registrations. */
    char fname[64];
    snprintf(fname, sizeof(fname), "/tmp/tcpmuxd.%d", port);
    unixsock rs = unixlisten(fname, 10);
    if(!rs) {
        perror("Cannot bind to file");
        return 1;
    }
    /* Process new registrations as they arrive. */
    while(1) {
        unixsock s = unixaccept(rs, -1);
        go(registration(s));
    }
}
