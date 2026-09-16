## o2ensemble

### initialize

```o2ensemble [ensemble-name] [network-level] [o2lite-enable] [http-enable] [http-root] [-d flags]```

```ensemble-name```: If no ensemble-name is provided, O2 will remain uninitialized until a join message.

```network-level```: Defaults to 2 (connect to local area network and Internet if available), other option: 1 (local area network only), 3 (Internet connectivity plus open a connection to the default MQTT broker for wide-area discovery). Instead of 3, a URL (symbol) means wide-area connectivity using an MQTT connection to the designated broker. The URL may contain a suffix of the form ```:1883``` to also provide the broker's port number (in decimal).

```o2lite-enable```: Defaults to 1 (enable), can set to 0 (disable).

```http-enable```: Defaults to 0, can set to 1 (provide an HTTP service and o2lite over WebSockets). Instead of 1, a port number (of the form ```:8080```) can be used to specify a port number.

```http-root```: Defaults to ```web```, but may contain an absolute or relative path to the root of the web server's static pages.

```-d flags```: passes debugging flags to O2.

### messages

#### join

```join ensemble-name [network-level] [o2lite-enable] [http-enable] [http-root] [-d flags] [-c clock]```

The same with initialization. If you leave an ensemble first and join another, you need to provide these parameters again.

```-c clock```: Default to 1 (enable becoming the global time reference), can be set to 0. Note that if two O2 processes claim to be the time reference, the process with the highest IP address and port number is the reference and lower processes provide a backup clock.

#### leave

```leave```

Shut down all O2 services and connections. After O2 is joined by an o2ensemble object, that object becomes active and leave messages are ignored by all other o2ensemble objects. If the active o2ensemble object is deleted, **any** other o2ensemble object will respond to a leave message and shut down the O2 connections until O2 is joined again.

#### version

```version```

Output a list ```version <version>```, which is the version of O2.

#### address

```address```

Output a list of the public ip, internal ip and TCP connection port for O2, in 128.2.3.4 format, and in decimal format for port number.

#### tap

```tap tappee tapper [-r] [-b]```

Tap a service. Messages received by tappee will be copied and received by tapper.

```-r```: Use reliable (TCP) transport.

```-b```: Use best effort (UDP) transport.

```-k```(default): Keep the same delivery mode.

#### untap

```untap tappee tapper```

Untap a service.

#### status

```status service```

Output a list 	```status <service> <code>```, which is the status of the service as an integer.

#### time

```time```

Output a message ```time <time>```, which is the current O2 time in ms.

#### clock

```clock reference```

Make this host be the reference clock if reference >0; otherwise, we do not provide the reference. If no message is sent before join, this process will **not** provide the reference clock.

Note that timestamps cannot be used without a reference clock. Note also that if a second reference clock (which starts at t=0) joins the ensemble at time T, then all other processes will experienced stopped clocks until the new reference clock catches up to the rest of the ensemble, which will "stop time" for T seconds. Therefore, timed messages and clock synchronization works best with *one* reference clock whose process starts first and is not restarted for any reason. (A slightly more robust scheme could be: Start two reference clocks at about the same time. If one crashes, do not restart it, and the other will take over. Clocks will continue to run smoothly in synchrony.)

#### clockjump

```clockjump localms globalms adjust```

This should only be sent directly in response to a timejump message (see above). localms should be the ```<localms>``` value and globalms should be the ```<newms>``` value. If the adjust parameter is non-zero, pending timestamped messages will have their delivery times adjusted so that they remain scheduled at the same wall time, e.g., if time jumps back 60 seconds, timestamps will be decremented by 60 seconds to compensate. Otherwise, timestamps remain and are interpreted according to the newly set clock.

#### oscport

```oscport service port [-r] [-b]```

Serve OSC messages to the given port by forwarding to service.

```-b```(Default): Use UDP.

```-r```: Use TCP.

#### oscdelegate

```oscdelegate service address port [-r] [-b]```

Forward O2 messages to the service to OSC server at address (in the format 128.2.9.3) and port (decimal).

```-b```(Default): Use UDP.

```-r```: Use TCP.

## o2receive

### initialize

```o2receive [node]* [-t types] [-w]```

This object receives O2 messages. Incoming messages are coerced into the types given (if specified).

o2receive objects with the same address can coexist, but o2receives with address ```/a/b/c``` and ```/a/b``` will conflict.

```[node]*```: The path of the node. For example, ```n1 n2 n3``` represents the path ```/n1/n2/n3```, where ```n1``` is the service name.

```[-t types]```: Specify the message type the object receives. ```types``` is a string containing character ```ifhdtsSc```, represents type of each message element. ```i``` for int32, ```h``` for int64, ```f``` for float, ```d``` for double, ```t``` for time, ```s``` and ```S``` for symbol, ```c``` for char. It can also be ```none``` or ```any``` (default).

```[-w]```: Wait when initialize and activate later. Since Max doesn't guarantee initialize order, the o2receive object may be initialized before O2. This option makes o2receive wait when initialize.

### messages

#### address

```address [node]+ [-t types]```

(Re)set the address and types for the object.

#### bang

Activate the object with the address and types it stored.

o2receive object may be inactive due to ```-w``` option when initialize, address conflict or ```disable``` message. Use this message to activate it.

#### disabe

```disable```

Stop this object from handling any more messages. Disabled objects will not considered conflict with activated ones.

#### types

```types [typestring]```

(Re)set the type of the object. This doesn't apply to disabled objects.

## o2send

### initialize

```o2send [node]* [-t types] [-r] [-b]```

Send O2 message.

```[node]*, [-t types]```: Similar to o2receive

```-b```(Default): Use UDP.

```-r```: Use TCP.

### messages

#### address

```address [node]+```

Similar to o2receive.

#### types

```types [typestring]```

Similar to o2receive.

#### time

```time ms```

Send the next message at the given time in ms. Default time is 0 ("now").

#### delay

```delay ms```

Send the next message after a delay of ms from now.

#### list

A list of values is coerced to the current typestring (if any) and sent.

If there is no typestring, floats are sent as floats and symbols are sent as symbols.

#### status

```status```

Output a message ```status <servicename> <status>```, where ```<status>``` is a integer representing the status of the service.

## o2property

### initialize

```o2property [service] [attribute]```

This object is used for setting and getting service properties, which are pairs of attribute and value.

When initialized, o2property expects 0 or 2 arguments. If 2, initial service and attribute are set.

### messages

#### bang

According to the previous service and attribute, find the value of the attribute of the service.

If the attribute exists, output the value of this attribute of the service from left outlet. Otherwise, output a bang from right outlet.

#### get

```get service attribute```

Set the object's service and attribute. Then acts as receiving a bang message.

#### put

```put service attribute [value]```

Set the object's service and attribute.

If value exists, set the attribute of the service to the value. Otherwise, remove the attribute from the service.

#### search

```search attribute value```

Output a list of service names where the service's attribute is matched by value.

To match a prefix, use ":prefix". To match a suffix, use "suffix;". To exactly match, use ":value;".