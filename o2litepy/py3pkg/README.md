# `o2litepy` - Python3 implementation of O2lite
### Roger B. Dannenberg and Zekai Shen

## Introducion
**O2** is a communication system for music, artists, and many other applications. It extends Open Sound Control with internet-wide discovery and communication, clock synchronization, named-services, and many other features. 

**O2lite** is a simplified protocol supported by O2 that allows a client to connect to O2 through an "O2 host," which could be any process running the full O2 protocol. The advantage of O2lite is that it is small and simple, able to run more easily in a native-Python implementation, on a microcontroller in C++, or even in MicroPython on a microcontroller.

- See [o2 on gitub](https://github.com/rbdannenberg/o2) for more information about O2.

- Note that there is a cross-platform `o2host` application that can serve as a bridget from o2lite to OSC, MIDI, and O2.

## O2lite in Python

### Workflow and Lifecycle
#### Initialization

The O2lite class begins its lifecycle with the initialize method. During initialization, it sets up necessary configurations based on the provided ensemble name and debug flags. This step includes preparing the discovery system (O2lite_discovery or Py3discovery), initializing network sockets, and configuring other essential attributes like internal_ip and udp_recv_port.

#### Discovery Process
Once initialized, O2lite engages in service discovery using its discovery mechanism (Py3discovery for Python 3). This process involves browsing the local network to find available services, parsing service information (including IP addresses and port numbers), and storing discovered services for subsequent communication.

#### Polling
The poll method is a crucial part of O2lite’s operation. It's typically called in a loop and is responsible for several ongoing tasks:

- Checking and handling incoming network messages.
- Managing clock synchronization, if enabled, to ensure time consistency across devices.
- Invoking the discovery process, if needed, to maintain an updated view of the network services.

#### Clock Synchronization
O2lite includes a sophisticated mechanism for clock synchronization. This feature is essential in distributed systems where time-sensitive operations are crucial. The synchronization process involves sending and receiving time-related messages to calculate the time difference between devices, thereby achieving a synchronized state.

#### Overall Operation
The O2lite class encapsulates the complexities of network communication, service discovery, and clock synchronization. Its design allows for efficient handling of network messages while maintaining a simple interface for the user. By continuously polling and handling new messages or events, O2lite remains responsive and adaptive to the dynamic nature of network environments.



### Key Features

- **Dual Environment Support**: Compatible with both MicroPython and Python 3.
- **Network Communication**: Supports both UDP and TCP protocols.
- **Clock Synchronization**: Maintains time synchronization across different devices.
- **Service Discovery**: Discovers and manages network services.
- **Message Handling**: Parses and dispatches network messages efficiently.

### Important Attributes

- **udp_send_address**: Address for sending UDP messages.
- **socket_list**: List of active sockets.
- **internal_ip**: IP address of the device.
- **udp_recv_port**: Port number for receiving UDP messages.
- **clock_sync_id**: Identifier for clock synchronization.
- **handlers**: List of message handlers.
- **services**: Manages the services provided or required by the instance, facilitating service discovery and coordination within the network.
- **outbuf**: A bytearray buffer for preparing outgoing messages, ensuring efficient memory usage.
- **parse_msg**: Temporarily stores the incoming message to be parsed.
- **clock_synchronized**: A flag indicating whether the clock has been successfully synchronized with the network.

### Message Parsing
O2lite employs a robust and flexible message parsing system, which is integral to its functionality. This system allows the class to interpret and respond to a wide range of message formats and types.

#### General Process
When a message is received, it's stored in parse_msg. The parsing process then extracts and interprets different components of the message based on predefined formats and types.

#### Key Parsing Methods
- **get_int32()**: Extracts a 32-bit integer from the message.
- **get_time()**: Retrieves a timestamp, represented as a double, from the message.
- **get_float()**: Extracts a floating-point number from the message.
- **get_string()**: Retrieves a string from the message. It handles variable-length strings and ensures proper termination and alignment.

#### Error Handling in Parsing
The parsing system includes error checks to handle cases like message truncation or type mismatches. This ensures robustness in communication, particularly important in diverse network environments.

### Service Discovery

#### Overview
Service discovery in **O2lite** is handled by the **O2lite_disc** class and its subclass **Py3discovery**, which is specifically designed for Python 3 environments. These classes automate the process of discovering network services, making it easier to establish connections without manual configuration.

#### O2lite_disc Class
This base class provides the fundamental structure for service discovery. It maintains a list of discovered services and has methods to restart and run the discovery process.

**Key Attributes**
- **services**: A dictionary to store services provided by the instance.
- **discovered_services**: A list to keep track of services discovered on the network.
- **browse_timeout**: The time period for which the discovery process runs.

**Key Methods**
- **get_host()**: Retrieves a discovered service from the list.
- **restart()**: Restarts the discovery process if no services are found.

#### Py3discovery Class (Subclass of O2lite_disc)

Specific to Python 3, this class uses Zeroconf for service discovery. It can handle new service announcements, service removals, and updates.

**Initialization**

The constructor initializes Zeroconf and a threading lock for thread-safe operations.

**Service Handling Methods**

- **add_service()**: Called by Zeroconf when a new service is found. It resolves the service and adds it to the discovered services list.
- **remove_service()**: Handles the removal of a service from the network.
- **handle_new_service()**: Processes new service information and validates UDP port.
- **pop_a_service()**: Removes and returns the first service from the discovered services list.
- **update_service()**: Placeholder for handling service updates.
- **close()**: Closes the Zeroconf service browser.

**Discovery Process**

- **run_discovery()**: Starts the service browsing process using ServiceBrowser from Zeroconf and runs for a specified timeout period.

**Usage**

This discovery mechanism allows O2lite to detect and interact with services on a local network automatically. It's especially useful in dynamic network environments where services can appear and disappear frequently.



