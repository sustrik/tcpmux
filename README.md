TCPMUX
======

Implementation of TCP multiplexer as defined in
[RFC 1078](https://tools.ietf.org/html/rfc1078).

The mutliplexer is a stand-alone daemon process that distributes the connections
accepted on a single TCP port (port 1 by default) to other processes on the
same machine.

The package also contains a libmill-compliant client library. 

To build you have to install [libmill](http://libmill.org) first. Then:

```
./autogen.sh
./configure
make check
sudo make install
```

To start the daemon:

```
sudo tcpmuxd
```

The daemon must be run with sudo because it uses port 1 by default, which
cannot be opened with standard user privilieges.

If you want to run tcpmuxd without extra privileges, choose a port above 1024:

```
tcpmuxd 5555
```

Once the daemon is running, application can listen for incoming tcpmux
connections. Here's an example application implementing service "foo".
It uses tcpmuxd running on port 5555:

```
#include <tcpmux.h>

...

tcpmuxsock ls = tcpmuxlisten(5555, "foo", -1);
while(1) {
    tcpsock s = tcpmuxaccept(ls, -1);
    ...
}
```

Client applications can connect to tcpmux server from anywhere. There's no
requirement to run tcpmuxd on the box:

```
#include <tcpmux.h>

...

ipaddr addr = ipremote("192.168.0.111", 5555, 0, -1);
tcpsock s = tcpmuxconnect(addr, "foo", -1);
```

The software is licensed under MIT/X11 license.

