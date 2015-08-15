TCPMUX

Implementation of TCP multiplexer (RFC 1078).

The mutliplexer is a stand-alone daemon process that distributes the connections
accepted on a single TCP port (port 1 by default) to other processes on the
same machine.

The package also contains a libmill-compliant client library. 
