// o2oscservice.cpp -- support option for OSC I/O from o2host
//
// Roger B. Dannenberg
// Feb 2024

#include "fieldentry.h"
#include "o2host.h"

void insert_o2_to_osc() {
    // open up a new line of the console - last line is the line of new_o2_to_osc
    int y = new_o2_to_osc.y - 2;
    // insert a line on the interface at y
    delete_or_insert(y, 1);

    // add five fields
    Field_entry *service = new Field_entry(0, O2TOOSC_SERV_X, y,
                    "Fwd Service", O2TOOSC_SERV_W, insert_after);
    service->marker = O2TOOSC_SERV_FIELD;
    set_current_field(service);

    Field_entry *ip = new Field_entry(O2TOOSC_IPLABEL_X, O2TOOSC_IP_X, y,
                                      "to OSC IP", IP_LEN, service);
    ip->set_ip();
    ip->set_content("127.000.000.001");
    
    Field_entry *port = new Field_entry(O2TOOSC_PORTLABEL_X, O2TOOSC_PORT_X, y,
                                        "Port", PORT_LEN, ip);
    port->is_integer = true;

    Field_entry *udp = new Field_entry(O2TOOSC_UDP_X, O2TOOSC_UDP_X, y, "",
                                       O2TOOSC_UDP_W, port);
    udp->set_menu_options(udp_tcp_options);

    Field_entry *delete_me = new Field_entry(O2TOOSC_DELLABEL_X, O2TOOSC_DEL_X,
                                             y, "(X", 1, udp);
    delete_me->is_button = true;
    delete_me->marker = O2TOOSC_DEL_FIELD;
    delete_me->after_field = ")";
    insert_after = delete_me;
    redraw_requested = true;
}


void insert_osc_to_o2() {
    // open up a new line of the console - last line is the line of new_osc_to_o2
    int y = new_o2_to_osc.y - 2;
    // insert a line on the interface at y
    delete_or_insert(y, 1);

    // add four fields
    Field_entry *udp = new Field_entry(0, OSCTOO2_UDP_X, y, "Fwd OSC from",
                                       OSCTOO2_UDP_W, insert_after);
    udp->set_menu_options(udp_tcp_options);
    udp->marker = OSCTOO2_UDP_FIELD;
    set_current_field(udp);

    Field_entry *port = new Field_entry(OSCTOO2_PORTLABEL_X, OSCTOO2_PORT_X, y,
                                        "Port", PORT_LEN, udp);
    port->is_integer = true;

    Field_entry *service = new Field_entry(OSCTOO2_SERVLABEL_X,
            OSCTOO2_SERV_X, y, "to Service", OSCTOO2_SERV_W, port);

    Field_entry *delete_me = new Field_entry(OSCTOO2_DELLABEL_X, OSCTOO2_DEL_X,
                                             y, "(X", 1, service);
    delete_me->is_button = true;
    delete_me->marker = OSCTOO2_DEL_FIELD;
    delete_me->after_field = ")";
    insert_after = delete_me;
    redraw_requested = true;
}

