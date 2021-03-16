..
    Copyright (c) 2021 Ilya Maximets <i.maximets@ovn.org>

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

==========
One Socket
==========

.. parsed-literal::

   One Socket to rule them all,
   One Socket to find them ...

What is One Socket?
-------------------

``One Socket`` is a reference implementation of a ``SocketPair Broker Daemon``,
``SocketPair Broker Protocol`` and a helper library for client applications
(``libspbroker``).

``SocketPair Broker`` is a daemon that provides an infrastructure service that
mediates establishment of direct socket-based connections between clients.
For more details on how it works see the `workflow overview
<doc/socketpair-broker.rst>`__.

``SocketPair Broker Protocol`` specification available
`here <doc/socketpair-broker-proto-spec.rst>`__.

Why One Socket?
---------------

How many socket files is too many?  In a virtualization or cloud environments
it's common to use Unix domain sockets to communicate between processes on
the same host.  These could be gRPC sockets, vhost-user, memif and so on.
Although design of a Unix socket implies client-server communication with a
single server and many clients, in many cases it is used for one-to-one
communication.

  Example:

  The vhost-user protocol is used as a control plane for device
  emulation/virtualization and there should be only one virtio device
  connected to a single device-backend (vhost).  For vhost-user network
  interfaces this means that there is a separate socket file for each virtual
  port in a software switch (e.g. Open vSwitch, VPP) that corresponds to
  virtio network interface in QEMU or virtio-user port in DPDK application.
  Same for storage and other virtio-based devices.

It gets harder to manage if communicating processes should be started in
different containers/pods/namespaces.  All these sockets are typically passed
from the host to container.  When restarted, server application usually
re-creates the socket, so it needs to be passed again, or the entire directory
must be shared between the host and the container.  The same directory sharing
is required if the server side is running inside a container, because the file
does not exist before the container is started.  Some software systems create
a separate directory for each socket file for these purpose.  And it gets
even worse if containers need to communicate not only with host applications,
but also with each other.

``One Socket`` provides an *infrastructure service* designed to eliminate most
of the inconveniences.  There is only *one socket* (broker socket) that must
be available in order to establish a direct connection to any other application
that has access to the same broker socket and is able to use the
``SocketPair Broker Protocol``.

How to build
------------

Build requirements
++++++++++++++++++

* ``meson``
* ``ninja-build``
* compiler: ``gcc`` or ``clang``

On Fedora could be installed by::

  # dnf install meson ninja-build gcc

On Ubuntu/Debian::

  # apt install meson ninja-build gcc

Configuration and build
+++++++++++++++++++++++

::

  $ export INSTALL_PATH=$(pwd)/build
  $ meson build --prefix=$INSTALL_PATH
  $ ninja -C build install

After that ``one-socket`` binary, ``libspbroker`` static library and
development headers will be installed in ``$INSTALL_PATH``.

How to run
----------

To start ``one-socket`` broker::

  $ export ONE_SOCKET_PATH=./one.socket
  $ export ONE_SOCKET_CTL_PATH=./one-socket.ctl
  $ ./one-socket

* ``ONE_SOCKET_PATH`` environment variable contains a path for a socket
  that will be used for clients.  Default value is ``/var/run/one.socket``.

* ``ONE_SOCKET_CTL_PATH`` environment variable contains a path for a control
  socket that will allow to manage the broker itself.  (TODO:  not used yet)
  Default value is ``/var/run/one-socket.ctl``.

libspbroker
-----------

``libspbroker`` is a static library that contains implementation of typical
functions for client application that could be used to abstract ``SocketPair
Broker Protocol`` details.

``One Socket`` provides library (``libspbroker.a``) and 2 header files:

* ``socketpair-broker/proto.h`` - Definitions for
  ``SocketPair Broker Protocol``.

* ``socketpair-broker/helper.h`` - Helper functions.

``spbroker.pc`` file for ``pkg-config`` also generated for easier linking.

E.g., if client application is based on ``meson`` too, it may use something
like this::

  $ meson build --pkg-config-path $INSTALL_PATH/lib64/pkgconfig

to find and link with ``libspbroker`` while having ``dependency('spbroker')``
in a ``meson.build`` file.

There is a very simple `test-client <test/test-client.c>`__ example
that implements echo-like client-server application using ``libspbroker``.

todo
----

* Document ``libspbroker`` API.  Write man pages.

* Integrate CI.

* Add tests.

* Make it a real daemon, i.e. daemonize, create pidfile, etc.

* Graceful shutdown, correct handling of signals.

* Replace environment variables with cmdline arguments.

* Implement control commands and control tool.

* Basic systemd service file.

* systemd socket-based service activation.

* RPM/DEB packaging.

* Python and Go implementations of libspbroker.

* Allow work over dbus instead of sockets?
