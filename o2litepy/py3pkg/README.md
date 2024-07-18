# `o2litepy` - Python3 implementation of O2lite
### Roger B. Dannenberg and Zekai Shen

## Introducion

**O2** is a communication system for music, artists, and many others.
It extends Open Sound Control with internet-wide discovery and
communication, clock synchronization, named-services, and many other
features.

**O2lite** is a simplified protocol supported by O2 that allows a
client to connect to O2 through an "O2 host," which could be any
process running the full O2 protocol. The advantage of O2lite is that
it is small and simple, able to run more easily in a native-Python
implementation, on a microcontroller in C++, or even in MicroPython on
a microcontroller.

- See [o2 on gitub](https://github.com/rbdannenberg/o2) for more
  information about O2.

- Note that there is a cross-platform `o2host` application that can
  serve as a bridge from o2lite to OSC, MIDI, and over the internet
  to other O2 processes. (o2lite can only discover an O2 process on
  the local network).

## O2lite in Python

See
[o2litepy-api.md](https://github.com/rbdannenberg/o2/blob/master/o2litepy/o2lite-api.md)
for details on programming with O2lite in Python.

### Workflow and Lifecycle

#### Initialization

The O2lite class begins its lifecycle with the initialize method,
which establishes the ensemble name and debug flags. Initialization
prepares the discovery system and initializes network sockets.

#### Discovery Process

Once initialized, O2lite engages in service discovery using its
discovery mechanism (in `py3discovery.py` or `upydiscovery.py`. This
process involves browsing the local network to find available
services, parsing information (including IP addresses and port
numbers), and storing discovered services for subsequent
communication.

#### Polling

The poll method *must* be called frequently because O2lite does not
run in its own thread. This prevents unexpected asychronous message
receipt and processing. You typically call poll in a loop that might
also check for other events or user actions. The poll method:

- Checks for and handles incoming network messages.

- Performs clock synchronization, if enabled, to ensure consistent
  time across the O2 ensemble.

- Invokes the discovery process, if needed, to maintain an up-to-date
  view of the network services.

#### Clock Synchronization

O2lite includes a sophisticated mechanism for clock synchronization.
The synchronization process involves sending and receiving messages to
estimate the time of the O2 reference clock. A local virtual clock is
maintained, allowing fast an accurate access to a globally consistent
clock.

#### Overall Operation

The O2lite class encapsulates all O2 operations including messaging,
service discovery, and clock synchronization. Its design allows for
efficient handling of network messages while maintaining a simple
interface for the user.

The user defines *services* which have *methods*. When a message
arrives addressed to a service, O2lite searches for and invokes a
handler (function). The function receives the raw message, the address
string, the type string (declaring the number and types of data items
in the message, and a user-specified value. The handler can call
O2lite methods to help extract data items from the (binary) message.

### Key Features

- **Dual Environment Support**: Compatible with both MicroPython and
  Python 3.

- **Network Communication**: Supports both UDP and TCP protocols.

- **Clock Synchronization**: Maintains time synchronization across
  different devices.

- **Service Discovery**: Discovers and manages network services.

- **Message Handling**: Parses and dispatches network messages
  efficiently.

### Message Parsing

O2 messages contain data items of different types in a binary format
similar to OSC. When a message is received, it is stored in
`parse_msg`. The message handler then extracts data items from the
message by calling built-in parsing methods. The message type string
specifies the types of items in the message.

#### Key Parsing Methods

- **get_int32()**: Extracts a 32-bit integer from the message.

- **get_time()**: Retrieves a timestamp, represented as a double, from
  the message.

- **get_float()**: Extracts a floating-point number from the message.

- **get_string()**: Retrieves a variable-length string from the
  message.

#### Error Handling in Parsing

The parsing system includes error checks to handle cases like message
truncation or type mismatches such as requesting an integer when a
float is provided.

### Important Attributes

These O2lite attributes are used internally, but described here as an
aid to understanding the implementation:

- **udp_send_address**: Address for sending UDP messages.

- **socket_list**: List of active sockets.

- **internal_ip**: IP address of the device.

- **udp_recv_port**: Port number for receiving UDP messages.

- **clock_sync_id**: Identifier for clock synchronization.

- **handlers**: List of message handlers.

- **services**: Manages the services provided or required by the
  instance, facilitating service discovery and coordination within the
  network.

- **outbuf**: A bytearray buffer for preparing outgoing messages.

- **parse_msg**: Temporarily stores the incoming message to be parsed.

- **clock_synchronized**: A flag indicating whether the clock has been
  successfully synchronized with the O2 host.

### Service Discovery

Service discovery is handled automatically. To aid in understanding
the implementation, an overview is included here.

#### Overview

Service discovery in **O2lite** is handled by the **O2lite_disc**
class and its subclass **Py3discovery** for Python 3
and **Upydiscovery** for MicroPython. These classes automate the
process of discovering network services through Bonjour, aka Zeroconf
and Avahi, making it easier to establish connections without manual
configuration.

#### O2lite_disc Class

This base class provides the fundamental structure for service
discovery. It maintains a list of discovered services and has methods
to restart and run the discovery process.

**Key Attributes**

- **services**: A dictionary to store services provided by the
  instance.

- **discovered_services**: A list to keep track of services discovered
  on the network.

- **browse_timeout**: The time period for which the discovery process
  runs.

**Key Methods**

- **get_host()**: Retrieves a discovered service from the list.

- **restart()**: Restarts the discovery process if no services are
  found.

#### Py3discovery Class (Subclass of O2lite_disc)

Specific to Python 3, this class uses Zeroconf for service discovery.
It can handle new service announcements, service removals, and
updates. Note that the entire goal of discovery is to find *one*
service that is an O2 host with a matching ensemble name. O2lite then
uses that host as a proxie to reach every other O2 process in the
ensemble. (In the simplest case, the O2 host is the only process we
want to connect to.)

Here, "service" refers to Zeroconf and O2 process (host) discovery.
The term "service" is also O2's term for top-level message
destinations. (An O2 process can offer one or more O2 services.)

Since discovery is only needed to find *one* host, and was assume the
application cannot function without a connection, discovery is
synchronous, blocking the caller until a connection is made. Discovery
will be invoked again to find a new host if the connection is broken.

**Initialization**

The constructor initializes Zeroconf.

**Service Handling Methods**

- **add_service()**: Called by Zeroconf when a new service is found.
  It resolves the service and adds it to the discovered services list.

- **remove_service()**: Handles the removal of a service from the network.

- **handle_new_service()**: Processes new service information and
  validates UDP port.

- **pop_a_service()**: Removes and returns the first service from the
  discovered services list.

- **update_service()**: Placeholder for handling service updates.

- **close()**: Closes the Zeroconf service browser.

**Discovery Process**

- **run_discovery()**: Starts the service browsing process using
  ServiceBrowser from Zeroconf and runs for a specified timeout period.

