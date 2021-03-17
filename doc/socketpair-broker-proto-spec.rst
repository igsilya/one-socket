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

==========================
SocketPair Broker Protocol
==========================
:Copyright: 2021 Ilya Maximets <i.maximets@ovn.org>
:Licence: Apache License, Version 2.0

.. contents:: Table of Contents

Introduction
============

``SocketPair Broker Protocol`` implements a communication mechanism between
``SocketPair Broker`` daemon (the "Broker") and a client application
(the "Client").

Broker provides a pairing service by establishing Unix domain socket -based
connections between different clients.  The end goal of the Broker is to
connect two clients so they could communicate directly with each other.
Broker and Client exchanging messages over a Unix domain socket (broker-socket)
to achieve this goal.  Broker listens for incoming client connections.

Message Specification
=====================

A SocketPair Broker message consists of 4 fields:

* ``request`` (``32`` bit field, bits: ``[0-31]``) - specifies the type of a
  message.

* ``flags`` (``32`` bit field, bits: ``[32-63]``) - holds specific parameters
  of a request.  ``4`` least significant bits are reserved for a protocol
  version (currently equals to ``0x1``).  Other bits are not used in this
  version of a protocol and should be zero.

* ``size`` (``32`` bit field, bits: ``[64-95]``) - specifies a size in bytes of
  the following ``payload``.

* ``payload`` - depending on a message type could be:

  * ``u64`` (``64`` bit field, bits: ``[96-159]``).

  * ``sp_broker_get_pair_request`` structure that consists of:

    * ``mode`` (``16`` bit field, bits: ``[160-175]``) - mode in which client
      will operate.

    * ``key_len`` (``16`` bit field, bits: ``[176-191]``) - number of bytes
      from the following ``key`` field that should be used.

    * ``key`` (``1024`` byte array, bits: ``[192-8383]``) - array of bytes
      specific for a client.

In C code this structure could be represented as::

  #define SP_BROKER_MAX_KEY_LENGTH 1024

  struct sp_broker_get_pair_request {
      uint16_t mode;
      uint16_t key_len;
      uint8_t key[SP_BROKER_MAX_KEY_LENGTH];
  } __attribute__((__packed__));

  struct sp_broker_msg {
      uint32_t request;
  #define SP_BROKER_PROTOCOL_VERSION_MASK   0xf
      uint32_t flags;
      uint32_t size;
      union {
          uint64_t u64;
          struct sp_broker_get_pair_request get_pair;
      } payload;
  } __attribute__((__packed__));

Depending on a request type, message also could include up to one file
descriptor passed over Unix domain socket using ``SCM_RIGHTS``.

Client requests
===============

* ``SP_BROKER_GET_PAIR`` (equals to ``0x1`` specified in ``request`` field).

  - Payload type: ``sp_broker_get_pair_request``.

    - ``mode`` should be one of:

      - ``SP_BROKER_PAIR_MODE_NONE`` (equal to ``0x0``)

      - ``SP_BROKER_PAIR_MODE_CLIENT`` (equal to ``0x1``)

      - ``SP_BROKER_PAIR_MODE_SERVER`` (equal to ``0x2``)

      Broker will pair clients with ``SP_BROKER_PAIR_MODE_NONE`` together.
      And it will pair clients that specified ``SP_BROKER_PAIR_MODE_CLIENT``
      with clients that specified ``SP_BROKER_PAIR_MODE_SERVER``.

    - ``key_len`` should be in range ``[1-1024]``.

    - ``key`` should be filled with ``key_len`` bytes of a client key.

  - Number of file descriptors: ``0``.

  - This message should be sent by a Client to Broker after successful
    connection.  ``key`` will be used by the Broker to find a pair for
    this Client.

Broker requests
===============

* ``SP_BROKER_SET_PAIR`` (equals to ``0x2`` specified in ``request`` field).

  - Payload type: ``u64``.

    - Value of a ``u64`` field should be zero.

  - Number of file descriptors: ``1``.

  - After successful processing of ``SP_BROKER_GET_PAIR`` request, Broker
    sends result in a form of ``SP_BROKER_SET_PAIR`` request with one file
    descriptor attached.  Broker normally closes connection with the Client
    after successful send.  File descriptor received by a client is a
    connected Unix domain socket that could be used to directly communicate
    with other Client.

Failure handling
================

Any protocol or internal error while processing SocketPair Broker message
should lead to the connection closure between Broker and Client with
possible subsequent establishment of a new connection and starting the
whole communication process from the beginning.
