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

#include "../tcpmux.h"

void daemon(void) {
    tcpmuxd(iplocal(NULL, 5557, 0));
    assert(0);
}

void doconnect(void) {
    ipaddr addr = ipremote("127.0.0.1", 5557, 0, -1);
    tcpsock s = tcpmuxconnect(addr, "foo", -1);
    assert(s);
    tcpsend(s, "abc", 3, -1);
    assert(errno == 0);
    tcpflush(s, -1);
    assert(errno == 0);
}

int main(void) {
    go(daemon());
    msleep(now() + 500);
    tcpmuxsock ls = tcpmuxlisten(5557, "foo", -1);
    assert(ls);
    go(doconnect());
    tcpsock s = tcpmuxaccept(ls, -1);
    assert(s);
    char buf[3];
    tcprecv(s, buf, sizeof(buf), -1);
    assert(errno == 0);
    assert(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c');
    tcpclose(s);
    tcpmuxclose(ls);

    return 0;
}
