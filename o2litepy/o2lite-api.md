# API for o2litepy 
## Native Python/Micropython O2lite Implementation

**Zekai Shen and Roger B. Dannenberg**

O2lite functions are all methods within the class O2lite.
In this documentation, we assume you have a single instance
of O2lite named `o2lite`. Thus, we write `o2lite.time_get()` to
indicate a call to the `time_get()` method of O2lite. Of course,
you can use a name other than `o2lite`.

## Examples
###Receiving a string from ensemble "test":
```[Python]
from o2litepy import O2lite
o2lite = O2lite()
o2lite.initialize("test", debug_flags="a")
o2lite.set_services("example")
o2lite.method_new("/example/str", "s", True, str_handler, None)
def str_handler(address, types, info):
    print("got a string:", o2lite.get_string())
o2lite.sleep(30)  # polls for 30 seconds
```
## Sending a float to service "sensor", ensemble "test":
```[Python]
from o2litepy import O2lite
o2lite = O2lite()
o2lite.initialize("test", debug_flags="a")
# wait for connect and clock sync with host:
while o2lite.time_get() < 0:
    o2lite.sleep(1)
# send now that we are connected:
o2lite.send_cmd("/test/sensor", "f", 3.14)
# poll for another second - exiting immediately or not polling
# may close the connection before message delivery completes:
o2lite.sleep(1) 
# Normally, a "main loop" will call o2lite.poll() frequently
# so you do not need to explicitly call o2lite.sleep() after
# sending.
```
## Time
- `o2lite.local_now` is the current local time, updated every
  `o2lite.poll()`, which starts from 0

- `o2lite.time_get()` retrieves the global time in seconds,
  but -1 until clock synchronization completes.

- `o2lite.local_time()` retrieves the local time (`local_now`
   is identical within the polling period and should be faster).


## Debugging
- `debug_flags` is a string used to set some debugging options:
  `'b'` -- print actual bytes of messages
  `'s'` -- print O2 messages when sent
  `'r'` -- print O2 messages when received
  `'d'` -- print info about discovery
  `'g'` -- general debugging info
  `'a'` -- all debugging messages except b

Setting any flag automatically enables `'g'`. `debug_flags`
can be set directly, or `debug flags` can be passed as a
parameter to initialize().

## Initialization

You should create *one* instance of the O2lite class, e.g.
```[Python]
    o2lite = O2lite()
```
- `o2lite.initialize(ensemble_name, debug_flags="")` must
  be called before using any other O2lite methods.
  
## O2 Types

This library supports the following data type codes and types in
messages:
- 'i' 32-bit signed integer (Python int)
- 'f' 32-bit IEEE float (Python float)
- 's' string (Python str)
- 't' 64-bit double time-stamp (Python float)

This limited set is intentional to minimize the size of o2lite
implementations, but it is likely to expand with more types.

## Sending Messages

Messages are sent with either `send()`, to send via UDP, or
`send_cmd()`, to send via TCP:

- `o2lite.send(address, time, type_string, data1, data2, data3, ...)`
  - `address` is the destination address (a string)
  - `time` is the delivery time (double in seconds).  All messages are
    delivered immediately and dispatched at the given timestamp. Use
    zero for "as soon as possible."
  - `type_string` is a string of type codes (see above).
  - `data` parameters are actual values for the message.
  - The message is sent via UDP. For example,
    ```o2lite.send("/host/info", "isf", 57, "hello", 3.14)```
    will send a messages to O2 address "/host/info" with
    integer, string and float values as shown.

- `o2lite.send_cmd(address, type_string, data1, data2, data3, ...)`
  is similar to o2lite.send() except the message is sent via TCP.

## Receiving Messages

Messages are directed to services. To receive a message you
first state the services you offer in this process using

- `o2lite.set_services(services)` where services is a string
  of *all* your service names separated by commas, e.g.
  ```o2lite.set_services("self,services")```
  This call can be made even before calling initialize, and
  it can be made again if the set of services changes.

Next, you need to register a handler for each address or
address prefix to be handled using:

- `o2lite.method_new(path, type_string, full, handler, info)`
  where
  - `path` is the complete O2 address including the service
    name,

  - `type_string` is the type string expected for the message
    ("" means no parameters and None means parameter types are
    not checked: the handler decides what to accept),

  - `full` is `True` if the address is the full address, or
    `False` to accept any message to an address that begins
    with `path` (addresses are checked node-by-node, so
    `/serv1/foo` is a prefix of `/serv1/foo/bar`, but
    `/serv1/foo` is *not* a prefix of /serv1/foobar),

  - `handler` is a function to be called. The signature of the
    function is `handler(address, type_string, info)`,

  - `info` is additional data to pass on to the handler, e.g.
    you can attach the same handler to addresses `/serv/1`,
    `/serv/2`, `/serv/3` and quickly distinguish them by
    passing in `info` values of 1, 2, and 3, respectively, or
    `info` could be an object for object-oriented handlers, e.g.
    ```def handler(a, t, info): info.handler(a, t)```

Finally, you need to declare a handler that accepts 3 parameters:
    ```my_handler(address, type_string, info)``` 
The handler will typically begin by extracting parameters from
the message using the following functions:

- `o2lite.get_int32()` -  check for and return a 32-bit integer

- `o2lite.get_float()` - check for and return a 32-bit float

- `o2lite.get_time()` - check for and return a time (double with type 't')

- `o2lite.get_string()` - check for and return a string

Values are returned sequentially from the message and the sequence
of requests must match the sequence of types in the message
(available in the `type_string parameter`, but you can assume the
strings match the type_string specified in `method_new()`).
