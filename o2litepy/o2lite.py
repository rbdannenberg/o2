class o2lite_object(debug):

    # TODO: think about how to set up these constants
    def __init__(self, o2_no_o2discovery, o2l_no_broadcast):
        debug = false

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

