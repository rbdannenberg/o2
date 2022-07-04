// o2ws.js -- implementation of o2lite over websockets
//
// Roger B. Dannenberg
// Feb 2021
//
// API:
//
// GENERAL:
//     o2ws_initialize(ensemble) - connect to an O2 host using WSURI
//     o2ws_finish() - disconnect from host and quit clock sync
//     o2ws_local_time() - get the local time in seconds (double)
//     o2ws_time_get() - get the O2 time or -1 if clocks are not
//         synchronized yet.
//     o2ws_clock_synchronized - true when clocks are synchronized
//     o2ws_bridge_id (global variable) is the unique ID assigned by
//         the host to this o2ws client.
// PROPERTIES: These functions are not in the C implementation
//         of o2lite because it requires another data structure to
//         keep track of properties, but for websockets and
//         Javascript, it's trivial to put properties in a
//         dictionary. However, here we only implement setting or
//         changing an entire property string without access to
//         individual attributes and values, and no support for
//         encoding.
//     o2ws_set_properties(service, properties) - sets properties of
//         service and sends them to the host if/when the host is
//         connected. service must be a declared service (see
//         o2ws_set_services()), and properties is a
//         string. Properties have the form:
//         "attr1:value1;attr2:value2;...", where attributes are
//         alphanumeric, and values can be any string with colon
//         represented by "\:", semicolon represented by "\;", and
//         slash represented by "\\". Escape characters are not
//         removed, and the result should not be modified or
//         freed. Properties end in ";".
//     properties of services may be retrieved using /_o2/ws/ls.
// SENDING A MESSAGE:
//     o2ws_send(address, time, types, p1, p2, p3, ...) - send a 
//         message. p1 and must match types in type and number.
//     o2ws_send_cmd(address, time, types, p1, p2, p3, ...) - send a 
//         message. p1 and must match types in type and number.
//     o2ws_send_start(address, time, types, tcp = true) - start a message;
//         this must be followed by o2ws_add_* calls for each parameter
//         mentioned in types, then o2ws_send_finish() to send the message.
//     o2ws_send_finish() - call this after message parameters have been added
//     o2ws_add_string(s) - add a string parameter 
//     o2ws_add_time(time) - add a time parameter (times are doubles)
//     o2ws_add_double(x) - add a double parameter
//     o2ws_add_float(x) - add a float parameter
//     o2ws_add_int(i) - add an integer parameter 
//     o2ws_add_int32(i) - add an integer parameter 
// RECEIVING A MESSAGE:
//     o2ws_set_services(services) - services is a comma-separated list.
//         These services become services of the O2 host (the web
//         server) unless there is a conflict, and the O2 host will
//         forward messages addressed to any of these services to this
//         Javascript process (see o2ws_method_new). There is no way to
//         end a service other than closing the connection with
//         o2ws_finish(). 
//     o2ws_method_new(path, typespec, full, handler, info) - register
//         a message handler. If typespec is non-null, then incoming
//         message types must be an exact match to typespec before
//         handler is invoked. If full, then the address must fully
//         match; otherwise, this handler will be invoked if path is
//         either an exact match or the path followed by '/' is a
//         prefix of the incoming message address. The handler is any
//         function f(timestamp, address, typespec, info) where
//         timestamp, address and typespec are extracted from the
//         message. The info is simply passed onto the handler (it
//         could be an object).  In all cases, the initial '!' or
//         '/' of the typespec and incoming message address are
//         ignored. The handler can call o2ws_get_int() and other
//         o2ws_get_*() functions in the order of types in typespec
//         to retrieve parameters from the message.
//     o2ws_tap(taps) - set all taps. taps is a string of the form
//         tapper:tappee:mode,tapper:tappee:mode,...
//         where tappee is any service to be tapped by tapper, which
//         must be a service offered by this websocket client. mode is 
//         one of "K" for TAP_KEEP, "R" for TAP_RELIABLE, or "B" for 
//         TAP_BEST_EFFORT. Note: this is designed so that taps can be
//         simply saved until a connection is open, but for websockets,
//         we have to have a non-blocking send that can hold onto 
//         messages, so we just send the messages immediately (putting
//         them into a send queue) even if the websocket is not open yet.
//     o2ws_get_time() - in a handler, message parameters are unpacked
//         by retrieving them in order with o2ws_get_* messages like
//         this one that gets a time parameter.
//     o2ws_get_double() - in a handler, get a double parameter. 
//     o2ws_get_float() - in a handler, get a float parameter. 
//     o2ws_get_int32() - in a handler, get an int32 parameter. 
//     o2ws_get_int() - in a handler, get an int parameter (because the
//         message is ASCII, this will also get an int64 parameter.)
//     o2ws_get_string() - in a handler, get a string parameter.
// APPLICATION RESPONSIBILITIES:
//     o2ws_on_error(msg) (externally defined) - error messages
//         (strings) are passed to this function in addition to being
//         written to the console. This function is not defined here,
//         but rather intended for the application to define.
//     o2ws_status_msg(msg) (externally defined) - status messages
//         (strings) are passed to this function if it is defined.
//     Appication may get a list of all services and their info by
//         sending a message to "/_o2/ws/ls" with no parameters.
//         Replies will come to /_o2/ls with typespec "siss" and
//         parameters service_name service_type, process_name,
//         properties (unless service_type is O2_TAP, in which case
//         the tapper name replaces properties.) service_type is 
//         an integer from O2_LOCAL_NOTIME through O2_TAP (see code).
//     Initialization: something like
//         window.addEventListener("load", o2wsInit, false); 
//         function o2wsInit() { o2ws_initialize("myensemblename");
//                               o2ws_status_msgs_enable = true; }
//     o2ws_status_msgs_enable (initial value is false) can be set true
//         to enable calls to o2ws_status_msg()
//     o2ws_console_enable (initial value is true) can be set false to
//         disable output to console

// O2 service status values:
var O2_UNKNOWN = -1;
// these values indicate services without clock synchronization:
var O2_LOCAL_NOTIME = 0;
var O2_REMOTE_NOTIME = 1;
var O2_BRIDGE_NOTIME = 2;
var O2_TO_OSC_NOTIME = 3;
// these values indicate services with clock synchronization to the host
// You also need to have o2ws_clock_synchronized==true before you can send 
// non-zero timestamps to these services or receive non-zero timestamps:
var O2_LOCAL = 4;
var O2_REMOTE = 5;
var O2_BRIDGE = 6;
var O2_TO_OSC = 7;
// This value is not a status but a service type obtained from 
// o2_services_list and reported in a message to /_o2/ls:
var O2_TAP = 8;

var TAP_KEEP = 0;
var TAP_RELIABLE = 1;
var TAP_BEST_EFFORT = 2;

var ETX = String.fromCharCode(3);   // ASCII end-of-text delimiter
// when served by an O2 server, the following address will be replaced
// by the actual server address where this script can open a websocket:
var WSURI = "ws://THE.LOC.ALH.OST:PORTNO/o2ws";  // Websocket address
var o2ws_websocket = false;       // the websocket, if open
var o2ws_ensemble = null;  // the ensemble name, if o2ws_initialize'd
var o2ws_console_enable = true;          // enables console logging
var o2ws_status_msgs_enable = false;

// clock sync:
var CLOCK_SYNC_HISTORY_LEN = 5;
var o2ws_clock_sync_id = 1;
var o2ws_rtts = [];
var o2ws_ref_minus_local = [];
var o2ws_clock_synchronized = false;


// message sending: We can't pause, even to send a websocket message, so
// message sending has to use a queue. We don't expect it to be large, so
// there is no attempt to use an efficient queue structure:
var o2ws_msg_out_queue = [];  // messages to send, push new ones here
var o2ws_msg_out_pending = false;  // have we scheduled a future send?
function o2ws_pending_send() {
    if (o2ws_msg_out_pending) return; // this function will be called later
    while (o2ws_msg_out_queue.length != 0  &&
           o2ws_websocket && o2ws_websocket.readyState != 0) {
        var msg = o2ws_msg_out_queue.shift();
        o2ws_websocket.send(msg);
        if (o2ws_status_msgs_enable) {
            o2ws_status_msg("SENT: " + msg.length + " bytes, " + 
                            o2ws_printable(msg));
        }
    }
    if (o2ws_msg_out_queue.length != 0) {
        o2ws_msg_out_pending = true;
        setTimeout(o2ws_send_callback, 10);
    }
}

function o2ws_send_callback() {
    o2ws_msg_out_pending = false;
    o2ws_pending_send();
}


function o2ws_id_handler(timestamp, typespec, info) {
    // expecting one parameter
    o2ws_bridge_id = o2ws_get_int32();
    o2ws_status_msg("Our ID is " + o2ws_bridge_id + 
                    "; starting clock sync.");
    o2ws_clock_ping_send_time = o2ws_local_time() + 0.05;
    o2ws_start_sync_time = o2ws_clock_ping_send_time;
    setTimeout(o2ws_clock_callback, 50);
}


function o2ws_csput_handler(timestamp, typespec, info) {
    var now = o2ws_local_time();
    var id = o2ws_get_int32();
    var rtt = now - o2ws_clock_ping_send_time;
    var ref_time = o2ws_get_time() + rtt * 0.5;
    o2ws_ping_reply_count++;
    var i = o2ws_ping_reply_count % CLOCK_SYNC_HISTORY_LEN;
    o2ws_rtts[i] = rtt;
    o2ws_ref_minus_local[i] = ref_time - now;
    if (o2ws_ping_reply_count >= CLOCK_SYNC_HISTORY_LEN) {
        var min_rtt = o2ws_rtts[0];
        var best_i = 0;
        for (i = 1; i < CLOCK_SYNC_HISTORY_LEN; i++) {
            if (o2ws_rtts[i] < min_rtt) {
                min_rtt = o2ws_rtts[i];
                best_i = i;
            }
        }
        var new_gml = o2ws_ref_minus_local[best_i];
        if (!o2ws_clock_synchronized) { // set global clock to our best estimate
            o2ws_clock_synchronized = true;
            o2ws_send_start("!_o2/ws/cs/cs", 0, "", true);
            o2ws_send_finish(); // notify O2
            o2ws_global_minus_local = new_gml;
            console.log("Clock sync obtained. global - local = " + 
                        o2ws_global_minus_local);
        } else {   // avoid big jumps when error is small. Set clock if error
            // is greater than min_rtt. Otherwise, bump by 2ms toward estimate.
            var bump = 0.0;
            var upper = new_gml + min_rtt;
            var lower = new_gml - min_rtt;
            // clip to [lower, upper] if outside range
            if (o2ws_global_minus_local < lower) {
                o2ws_global_minus_local = lower;
            } else if (o2ws_global_minus_local < upper) {
                o2ws_global_minus_local = upper;
            } else if (o2ws_global_minus_local < new_gml - 0.002) {
                bump = 0.002; // increase by 2ms if too low by more than 2ms
            } else if (o2ws_global_minus_local > new_gml + 0.002) {
                bump = -0.002; // decrease by 2ms is too high by more then 2ms
            } else {  // set exactly to estimate
                bump = new_gml - o2ws_global_minus_local;
            }
            o2ws_global_minus_local += bump;
        }
    }
}

var o2ws_disable_clock_ping = false;  // for debugging 

// this gets called when it's time for a clock ping
function o2ws_clock_callback() {
    if (o2ws_disable_clock_ping) {
        return;
    }

    if (!o2ws_websocket) {
        return;
    }
    o2ws_clock_ping_send_time = o2ws_local_time();
    o2ws_clock_sync_id++;
    o2ws_send_start("/_o2/ws/cs/get", 0, "iis", false);
    o2ws_add_int32(o2ws_bridge_id);
    o2ws_add_int32(o2ws_clock_sync_id);
    o2ws_add_string("!_o2/cs/put");
    o2ws_send_finish();
    var next_time = o2ws_clock_ping_send_time + 0.1;
    if (o2ws_clock_ping_send_time - o2ws_start_sync_time > 1)
        next_time += 0.4;
    if (o2ws_clock_ping_send_time - o2ws_start_sync_time > 5)
        next_time += 9.5;
    setTimeout(o2ws_clock_callback,
               Math.round((next_time - o2ws_clock_ping_send_time) * 1000));
}


function o2ws_initialize(ensemble) {
    if (!o2ws_websocket) {
        o2ws_ensemble = ensemble;
        o2ws_ping_reply_count = 0;
        o2ws_websocket = new WebSocket(WSURI);
        o2ws_websocket.onopen = function(evt) { o2ws_open_handler(evt); };
        o2ws_websocket.onclose = function(evt) { o2ws_close_handler(evt) };
        o2ws_websocket.onmessage = function(evt) { o2ws_message_handler(evt) };
        o2ws_websocket.onerror = function(evt) { 
            if (typeof o2ws_error === 'function')
                o2ws_error("Websocket to O2 host was closed " + 
                           "abnormally by the host") };
        o2ws_method_new("/_o2/id", "i", true, o2ws_id_handler, null);
        o2ws_method_new("/_o2/cs/put", "it", true, o2ws_csput_handler, null);
        o2ws_send_start("/_o2/ws/dy", 0.0, "s", true);
        o2ws_add_string(o2ws_ensemble);
        o2ws_send_finish();
    }
}


function o2ws_finish() {
    if (o2ws_websocket) {
        o2ws_websocket.close();
    }
}

function o2ws_open_handler(evt) {
    if (o2ws_status_msgs_enable) {
        o2ws_status_msg("CONNECTED");
    }
}

function o2ws_close_handler(evt) {
    if (o2ws_status_msgs_enable) {
        o2ws_status_msg("DISCONNECTED");
    }
    o2Application = null;  // it's possible the server is still 
         // running O2, but we don't know that
    o2ws_websocket = null;
}


// convert websocket message to viewable representation
function o2wsMessageToString(msg) {
    return o2wsMsgFieldsToString(msg.split(ETX));
}


function o2wsMsgFieldsToString(fields) {
    var msg = fields[0] + "@" + fields[2] + ",";
    msg += (fields[3] === 'T' ? "TCP," : "UDP,") + fields[1];
    var i = 4;
    while (i < fields.length) {
        msg += "," + fields[i];
        i++;
    }
    return msg;
}


function dropMessage(fields) {
    var msg = o2wsMsgFieldsToString(fields);
    o2ws_status_msg("Dropped incoming O2 message: " + msg);
}

var o2ws_method_dict = {};
var o2ws_method_array = [];

function o2ws_schedule_handler(handler, timestamp, address, typespec, info) {
    if (o2ws_clock_synchronized) {
        var now = o2ws_time_get();
        if (timestamp > now) {
            setTimeout(handler.bind(timestamp, address, typespec, info),
                       Math.round(timestamp - now) * 1000);
        }
    }
    if (timestamp <= 0) {  // O2 does not allow negative timestamps, but if 
        // we get one, we treat it as if it is zero
        handler(timestamp, address, typespec, info);
    } else {
        // otherwise, the timestamp is >0 but the clock is not synchronized.
        // the message should have been dropped by the sender. We drop it here.
        console.log("o2ws_message_handler timestamp >0, but not synced yet.")
        dropMessage(o2ws_message_fields);
    }
}


function o2ws_printable(s) {
    return s.replace(/\003/g, " ¦ ");
}


function o2ws_message_handler(evt) {
    if (o2ws_status_msgs_enable) {
        o2ws_status_msg("RECEIVED: " + o2ws_printable(evt.data));
    }
    o2ws_message_fields = evt.data.split(ETX);    // parse the message
    // sanity check: message has address, typespec, timestamp, flag and
    // ends with ETX
    if (o2ws_message_fields.length < 5 ||
        o2ws_message_fields[o2ws_message_fields.length - 1] != "") {
        dropMessage(o2ws_message_fields);
        return;
    }
    // split() adds an empty string at the end. Remove it.
    o2ws_message_fields.pop();
    // find a handler
    var address = o2ws_message_fields[0].substring(1);  // remove first ! or /
    var typespec = o2ws_message_fields[1];
    var timestamp = parseFloat(o2ws_message_fields[2]);
    // sanity check: number of parameters must match length of typespec
    if (typespec.length != o2ws_message_fields.length - 4) {
        dropMessage(o2ws_message_fields);
        return;
    }
    // o2ws_message_fields[3] is TCP flag -- ignored
    // fast lookup with dictionary
    if (o2ws_method_dict.hasOwnProperty(address)) {
        var h = o2ws_method_dict[address];
        if (h.typespec !== null && h.typespec != typespec) {
            dropMessage(o2ws_message_fields);
            return;
        }
        o2ws_message_fields.splice(0, 4);  // remove address,timestamp,typespec
        o2ws_schedule_handler(h.handler, timestamp, h.address,
                              typespec, h.info);
    } else {  // search for a partial match
        for (h of o2ws_method_array) {
            if (address.startsWith(h.address) && 
                (address.length === h.address.length ||
                 address[h.address.length] == '/')) {  // address match
                if (h.typespec === null || h.typespec == typespec) {
                    o2ws_message_fields.splice(0, 4);  // leaves only parameters
                    o2ws_schedule_handler(h.handler, timestamp, h.address,
                                          typespec, h.info);
                    h.handler(timestamp, typespec, h.info);
                    return;
                }
            }
        }
        console.log("o2ws_message_handler did not find " + address);
        dropMessage(o2ws_message_fields);
    }
}


var o2ws_properties = {};

function o2ws_set_properties(service, properties) {
    o2ws_properties[service] = properties;
    if (o2ws_websocket) {
        o2ws_send_service(service, properties);
    }  // otherwise, we'll send properties when we are connected
}


function o2ws_send_service(name, prop) {
    o2ws_send_start("!_o2/ws/sv", 0, "siisi", true);
    o2ws_add_string(name);
    o2ws_add_int32(1); // exists
    o2ws_add_int32(1); // this is a service
    o2ws_add_string(";" + prop);
    o2ws_add_int32(0); // send_mode is ignored for services
    o2ws_send_finish();
}


function o2ws_set_services(services) {
    var ss = services.split(",");
    for (const name of ss) {
        if (name.length > 0) {
            var prop = "";
            if (o2ws_properties.hasOwnProperty(name)) {
                prop = o2ws_properties[name];
            }
            o2ws_send_service(name, prop);
        }
    }
}


function o2ws_send_tap(tappee, tapper, mode) {
    o2ws_send_start("!_o2/ws/sv", 0, "siisi", true);
    o2ws_add_string(tappee);
    o2ws_add_int32(1); // exists
    o2ws_add_int32(0); // this is a tap
    o2ws_add_string(tapper);
    if (mode == "K") mode = TAP_KEEP;
    else if (mode == "R") mode = TAP_RELIABLE;
    else if (mode == "B") mode = TAP_BEST_EFFORT;
    else {
        o2ws_status("Bad tap mode: '" + mode + "'");
        mode = TAP_KEEP;
    }
    o2ws_add_int32(mode);
    o2ws_send_finish();
}


function o2ws_tap(taps) {
    taps = taps.split(",");
    for (tap of taps) {
        var fields = tap.split(":");
        if (fields.length == 3) {
            o2ws_send_tap(fields[0], fields[1], fields[2]);
        } else {
            o2ws_status_msg("Bad tap description: " + taps);
        }
    }
}


var startDate = new Date();
var startTime = startDate.getTime();
var o2ws_global_minus_local = null;

function o2ws_local_time() {
    var now = new Date().getTime();
    return (now - startTime) / 1000;
}

function o2ws_time_get() {
    if (o2ws_clock_synchronized) {
        return o2ws_local_time() + o2ws_global_minus_local;
    } else {
        return -1;
    }
}

var o2ws_bridge_id = null;

var o2ws_message_string = null;

function o2ws_send(address, time, types) {  // optional parameters too
    o2ws_send_start(address, time, types, false);
    o2ws_send_args(types, arguments);
}

function o2ws_send_cmd(address, time, types) {  // optional parameters too
    o2ws_send_start(address, time, types, true);
    o2ws_send_args(types, arguments);
}

function o2ws_send_args(types, args) {
    var i = 3;
    if (types.length != args.length - 3) {
        o2ws_on_error("o2ws_send[_cmd] called with mismatched arguments.");
        return;
    }
    for (typecode of types) {
        switch (typecode) {
          case "i":
          case "h":
            if (typeof args[i] != "number") {
                o2ws_on_error("o2ws_send[_cmd] called with bad arg " +
                              args[i] + ".");
                return;
            }
            o2ws_add_int(args[i]);
            break;
          case "f":
          case "d":
          case "t":
            if (typeof args[i] != "number") {
                o2ws_on_error("o2ws_send[_cmd] called with bad arg " +
                              args[i] + ".");
                return;
            }
            o2ws_add_double(args[i]);
            break;
          case "s":
          case "S":
            if (typeof args[i] != "string") {
                // note Javascript symbols not allowed here
                o2ws_on_error("o2ws_send[_cmd] called with bad arg " +
                              args[i] + ".");
                return;
            }
            o2ws_add_string(args[i]);
            break;
          default:
            o2ws_on_error("o2ws_send[_cmd] called with bad type '" +
                          typecode + "'.");
            return;
        }
        i++;
    }
    o2ws_send_finish();
}


function o2ws_send_start(address, time, types, tcp = true) {
    if (typeof address !== "string" || typeof time !== "number" ||
        typeof types !== "string" || typeof tcp !== "boolean") {
        o2ws_on_error("Bad parameter to o2ws_send_start");
    }
    o2ws_message_string = address + ETX + types + ETX + time + ETX;
    o2ws_message_string += (tcp ? "T" : "F") + ETX;
}

function o2ws_send_finish() {
    if (o2ws_message_string) {
        o2ws_msg_out_queue.push(o2ws_message_string);
        o2ws_pending_send();
    }
    o2ws_message_string = null;
}

function o2ws_add_string(s) {
    o2ws_message_string += s + ETX; 
}

function o2ws_add_time(time) {
    o2ws_message_string += time + ETX; 
}

function o2ws_add_double(x) {
    o2ws_message_string += x + ETX; 
}

function o2ws_add_float(x) {
    o2ws_message_string += x + ETX; 
}

function o2ws_add_int(i) {
    o2ws_message_string += i + ETX; 
}

function o2ws_add_int32(i) {
    o2ws_message_string += i + ETX; 
}

function o2ws_get_float() {
    var s = o2ws_message_fields.shift();  // get the next parameter
    return parseFloat(s);
}

// no distinction between float/time/double except for typespecs f, t, d
function o2ws_get_time() { return o2ws_get_float(); }

function o2ws_get_double() { return o2ws_get_double(); }

function o2ws_get_int32() {
    var s = o2ws_message_fields.shift();  // get the next parameter
    return parseInt(s);
}

// no distinction between int32/int64 except typespecs i, h
// also, if message has a float but you ask for an int32 or int64, you
// will get an integer which is the *truncated* float value, not an error.  

function o2ws_get_int() { return o2ws_get_int32(); }

function o2ws_get_int64() { return o2ws_get_int32(); }


//         message is ASCII, this will also get an int64 parameter.)
function o2ws_get_string() {
    return o2ws_message_fields.shift();  // get the next parameter
}


function o2ws_method_new(path, typespec, full, handler, info) {
    var path1 = path.substring(1)
    var method = {address: path1,
                  typespec: typespec,
                  full: full,
                  handler: handler,
                  info: info};
    o2ws_method_dict[path1] = method;
    if (!full) {  // non "full" handlers are searched for partial match:
        o2ws_method_array.push(method);
    }
}

