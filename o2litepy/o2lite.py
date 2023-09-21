from dataclasses import dataclass

# define O2_MALLOC malloc
# define O2_CALLOC calloc
# define O2_FREE free

MAX_MSG_LEN = 256
PORT_MAX = 16
O2L_SUCCESS = 0
O2L_FAIL = -1
O2L_ALREADY_RUNNING = -5
O2_UDP_FLAG = 0
O2_TCP_FLAG = 1


# typedef struct o2l_msg {
#     int32_t length; // length of flags, timestamp, address, and the rest
#     int32_t misc;   // flags and ttl (see O2msg_data)
#     double timestamp; // regardless of o2l_time, this is O2time = double
#     char address[4];
# } o2l_msg, *o2l_msg_ptr;

@dataclass
class O2lMsg:
    length: int
    misc: int
    timestamp: float
    message_address: chr[4]


class O2liteObject:
    # TODO: think about how to set up these constants
    def __init__(self, o2_no_o2discovery=None, o2l_no_broadcast=None):
        debug = False

        # if defined(O2_NO_O2DISCOVERY) && !defined(O2L_NO_BROADCAST)
        # define O2L_NO_BROADCAST 1
        # endif
        # if defined(O2_NO_O2DISCOVERY) && defined(O2_NO_ZEROCONF)
        # error O2_NO_O2DISCOVERY and O2_NO_ZEROCONF are both defined - no discovery
        # endif

        if o2_no_o2discovery is not None and o2l_no_broadcast is None:
            o2_no_o2discovery = 1

        if o2_no_o2discovery is not None and o2l_no_broadcast is not None:
            raise ValueError("error O2_NO_O2DISCOVERY and O2_NO_ZEROCONF are both defined - no discovery")

        # default is ZEROCONF, so if necessary, disable O2_NO_O2DISCOVERY:
        # if !defined(O2_NO_O2DISCOVERY) && !defined(O2_NO_ZEROCONF)
        # define O2_NO_O2DISCOVERY
        # if !defined(O2L_NO_BROADCAST)
        # define O2L_NO_BROADCAST
        # endif
        # endif

    def o2l_send_start(self, address, time, types, tcp):
        """
        brief start preparing a message to send by UDP or TCP

        Start every message by calling this function. After adding parameters
        according to the type string, send the message with #o2l_send().

        :param string address: O2 address
        :param o2l_time time: timestamp for message
        :param string types: O2 type string, only s, i, f, t, d supported
        :param bool tcp: true for TCP, false for UDP
        :return:
        """
        pass

    def o2l_send(self):
        """
        Brief Send a fully constructed message
        :rtype: object
        """
        pass

    def o2l_method_new(self, path, typespec, full, h, info):
        pass

    def o2l_local_time(self):
        pass

    def o2l_time_get(self):
        pass

    def o2l_poll(self):
        pass

    # TODO: think of a design choice for these methods that only fitts some versions
    # if esp32
    def connect_to_wifi(self, hostname, ssid, pwd):
        pass

    def print_line(self):
        pass

    # endif

    def o2l_initialize(self, ensemble):
        pass

    def o2l_finish(self):
        pass

    def o2l_finish(self):
        pass

    def o2l_add_string(self, s):
        pass

    def o2l_add_time(self, time):
        pass

    def o2l_add_float(self, x):
        pass

    def o2l_add_int(self, i):
        pass

    def o2l_get_error(self):
        pass

    def o2l_get_timestamp(self):
        pass

    def o2l_get_time(self):
        pass

    def o2l_get_float(self):
        pass

    def o2l_get_int(self):
        pass

    def o2l_get_string(self):
        pass

    def o2l_set_services(self):
        pass

    def o2ldisc_init(self, ensemble):
        pass

    def o2ldisc_poll(self):
        pass

    def o2ldisc_events(self, readset):
        pass

    def o2l_is_valid_proc_name(self, name, port, internal_ip, udp_port):
        pass

    def o2l_parse_version(self, vers, vers_len):
        pass

    def o2l_address_init(self, sa, ip, port_num, tcp):
        pass

    def o2l_network_connect(self, ip, port):
        pass

    def o2l_add_socket(self, s):
        pass

    def o2l_bind_recv_socket(self, sock, port):
        pass
