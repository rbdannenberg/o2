// o2host.cpp -- a host process for o2lite, run in a shell/cmd prompt/terminal
//
// Roger B. Dannenberg
// Feb 2024

// This application enables o2lite (e.g. in python) to connect
// to an O2 network by running this o2host program locally.
//
// Initially, a curses interface appears to configure.
// Screen layout:
// 
// O2 Host / O2 Server for O2lite
// Configuration: ____________________ Load_
//     Rename to: ____________________ Save_
// 
// Ensemble name:     _______________________________   Polling rate: ____
// Debug flags:       _______________________________
// Networking (up/down to select): ___________________  WebSockets: Enable
// MQTT Enable  MQTT Host: ________________________________ MQTT Port: _____
// Fwd Service ____________________ to OSC IP ___.___.___.___ Port _____ UDP (X_)
// Fwd OSC from UDP Port _____ to Service ____________________ (X_)
// MIDI In _____________________________ to Service ____________________ (X_)
// MIDI Out Service ____________________ to ____________________________ (X_)
// ...
// New forward O2 to OSC: _        New receive OSC as O2: _
// New MIDI In to O2: _    New MIDI Out from O2: _    MIDI Refresh: _
//
// Type ESC to start.

// configuration file: name is o2host.config or .o2host.config
// format is as follows. All fields are quoted strings to handle spaces
// and empty fields:
//
// o2host v1.0
// Configuration: <configuration> [gives current selection]
// ---- <configuration>
// Ensemble_name: <name>
// Polling_rate: <string>
// Debug_flags: <flags>
// Networking: <string>
// WebSockets: <string>
// MQTT_enable: <string>
// MQTT_host: <string>
// MQTT_port: <string>
// O2_to_OSC: <servicename> <IP> <port> UDP
// OSC_to_O2: UDP <port> <servicename>
// MIDI_in: <devicename> <servicename>
// MIDI_out: <servicename> <devicename>
// ----
// ---- <next configuration name>
// ...

#include "assert.h"
#include "o2.h"

#include "ctype.h"
#include "curses.h"
#include "string.h"
#include "sys/stat.h"
#include <unistd.h>
#include "fieldentry.h"
#include "o2host.h"
#include "configuration.h"
#include "confread.h"
#include "o2oscservice.h"
#include "midiservice.h"


#define ESC_CHAR 0x1b
#define DEL_CHAR 0x7f

static int host_rate = 500;

// configure -- handle keyboard input to set O2 configuration

int xpos = 0;
int ypos = 0;

#define CNORMAL 1
#define CTITLE 2
#define CRED 3
#define CSEP 4
#define CHELP 5
#define BRIGHT(c) ((c) + (COLORS > 15) * 8)
#define BRIGHT_WHITE BRIGHT(COLOR_WHITE)

#define REQUIRED_WIDTH 78
#define CONF_X 15
#define CONF_Y 2
#define CONFLOAD_LABELX 37
#define CONFLOAD_X 41
#define CONFDELETE_LABELX 44
#define CONFDELETE_X 50
#define CONFRENAME_LABELX 4
#define CONFRENAME_X 15
#define CONFRENAME_Y 3
#define CONFRENAME_W 20
#define CONFSAVE_LABELX 37
#define CONFSAVE_X 41

#define ENS_X 19
#define ENS_Y 5
#define POLL_LABELX 53
#define POLL_X 67
#define POLL_Y 5
#define DBG_X 19
#define DBG_Y 6
#define NET_X 32
#define NET_Y 7
#define WEB_LABELX 53
#define WEB_X 65
#define WEB_Y 7
#define MQTTENAB_X 5
#define MQTT_LABELX 13
#define MQTT_X 24
#define MQTT_Y 8
#define MQTTPORT_LABELX 57
#define MQTTPORT_X 68

#define O2TOOSC_X 23
#define O2TOOSC_Y 11
#define OSCTOO2_LABELX 32
#define OSCTOO2_X 55
#define OSCTOO2_Y 11

#define MIDITOO2_X 19
#define MIDITOO2_Y 12
#define O2TOMIDI_LABELX 24
#define O2TOMIDI_X 46
#define O2TOMIDI_Y 12
#define MIDIREF_LABELX 51
#define MIDIREF_X 65
#define MIDIREF_Y 12


const char *net_options[] = {"localhost only", "local network", "internet",
                             "wide-area discovery", NULL};
const char *enable_options[] = {"Disable", "Enable", NULL};
const char *udp_tcp_options[] = {"UDP", "TCP", NULL};
const char *configuration_list[] = {NULL};

// configurations are stored here:
char pref_path[128];

int required_height = 14;  // initial value
// if this is set, we just wait for a bigger screen
bool need_bigger_screen = false;
bool help_mode = false;


const char *heapify(const char *s)
{
    char *news = (char *) malloc(strlen(s) + 1);
    strcpy(news, s);
    return news;
}


void moveyx(int y, int x)
{
    move(y, x);
    xpos = x;
    ypos = y;
}


Field_entry configuration(0, CONF_X, CONF_Y, "Configuration:", CONF_W, NULL);
Field_entry configuration_load(CONFLOAD_LABELX, CONFLOAD_X, CONF_Y,
                               "Load", 1, NULL);
Field_entry configuration_delete(CONFDELETE_LABELX, CONFDELETE_X, CONF_Y,
                               "Delete", 1, NULL);
Field_entry configuration_rename(CONFRENAME_LABELX, CONFRENAME_X,
                    CONFRENAME_Y, "Rename to:", CONFRENAME_W, NULL);
Field_entry configuration_save(CONFSAVE_LABELX, CONFSAVE_X, CONFRENAME_Y,
                               "Save", 1, NULL);
Field_entry ensemble_name(0, ENS_X, ENS_Y, "Ensemble name:",
                          MAX_NAME_LEN, NULL);
Field_entry polling_rate(POLL_LABELX, POLL_X, POLL_Y, "Polling rate:",
                         POLL_W, NULL);
Field_entry debug_flags(0, DBG_X, DBG_Y, "Debug flags:", MAX_NAME_LEN, NULL);
Field_entry networking(0, NET_X, NET_Y, "Networking (up/down to select):",
                       NET_W, NULL);
Field_entry websockets(WEB_LABELX, WEB_X, WEB_Y, "WebSockets:",
                       WEB_W, NULL);
Field_entry mqtt_enable(0, MQTTENAB_X, MQTT_Y, "MQTT", MQTTENAB_W, NULL);
Field_entry mqtt_host(MQTT_LABELX, MQTT_X, MQTT_Y, "MQTT Host:",
                      MAX_NAME_LEN, NULL);
Field_entry mqtt_port(MQTTPORT_LABELX, MQTTPORT_X, MQTT_Y, "MQTT Port:",
                      PORT_LEN, NULL);
Field_entry new_o2_to_osc(0, O2TOOSC_X, O2TOOSC_Y, "New forward O2 to OSC:",
                          1, NULL);
Field_entry new_osc_to_o2(OSCTOO2_LABELX, OSCTOO2_X, OSCTOO2_Y,
                          "New receive OSC as O2:", 1, NULL);
Field_entry new_midi_to_o2(0, MIDITOO2_X, MIDITOO2_Y, "New MIDI In to O2:",
                           1, NULL);
Field_entry new_o2_to_midi(O2TOMIDI_LABELX, O2TOMIDI_X, O2TOMIDI_Y,
                           "New MIDI Out from O2:", 1, NULL);
Field_entry midi_refresh(MIDIREF_LABELX, MIDIREF_X, MIDIREF_Y,
                         "MIDI Refresh:", 1, NULL);


void print_help()
{
    for (int i = 0; i < (COLS - 30) / 2; i++) {
        addch(' ');
    }
    attron(A_BOLD);
    addstr("HELP - ESC to exit help mode\n");
    attroff(A_BOLD);
    addstr("Use TAB, RETURN, LEFT, RIGHT, UP, DOWN to navigate fields.\n");
    addstr("In fields with options, UP and DOWN arrows change selection.\n");
    addstr("Single-character fields (\"_\") are ACTIONS: type one of xXyY "
           "to activate.\n\n");
    addstr("All field values together form a Configuration, which you can\n");
    addstr("    load, edit, save, and delete.\n");
    addstr("To save a configuration under a new name, fill in "
           "\"Rename to:\"\n");
    addstr("    and type \"x\" in the \"Save_\" field\n");
    addstr("To load or delete a saved configuration, select \"Config"
           "uration:\"\n");
    addstr("    and type \"x\" in the \"Load_\" or \"Delete_\" field.\n");
    addstr("Ensemble name: - you must specify the O2 ensemble to join.\n");
    addstr("Debug flags: - enable debug output, see O2 documentation.\n");
    addstr("Networking: - limits range of discovery to this local host,\n");
    addstr("    local area (e.g. Wi-Fi hub only), or whole internet.\n");
    addstr("    Probably you need \"wide-area discovery\" instead of "
           "\"whole internet.\"\n");
    addstr("MQTT Host/Port: - fill in to use custom MQTT broker instead "
           "of default.\n");
    addstr("With actions near the bottom of the screen, you can:\n");
    addstr("  - create an O2 service that forwards by UDP or TCP to OSC,\n");
    addstr("  - receive OSC by UDP or TCP and forward to a designated O2"
           " service,\n");
    addstr("  - create services that send to selected MIDI output devices,"
           " and\n");
    addstr("  - read from MIDI input devices and send to a designated O2"
           " service.\n");
    addstr("\"(X_)\" are actions to delete the preceding "
           "specification.\n");
}   


void draw_screen()
{
    wbkgd(stdscr, help_mode ? COLOR_PAIR(CHELP) : COLOR_PAIR(CNORMAL));
    erase();
    move(0, 0);
    attron(A_BOLD);
    attron(COLOR_PAIR(CTITLE));
    // center the title by padding on left:
    for (int i = 0; i < (COLS - 31) / 2; i++) {
        addch(' ');
    }
    addstr("O2 Host / O2 Server for O2lite");
    int x, y;
    // now fill with blanks until cursor wraps
    getyx(stdscr, y, x);
    while (x != 0) {
        addch(' ');
        getyx(stdscr, y, x);
    }
    attroff(A_BOLD);
    if (help_mode) {
        attron(COLOR_PAIR(CHELP));
        print_help();
        return;
    } else {
        attron(COLOR_PAIR(CNORMAL));
    }

    need_bigger_screen = false;
    if (COLS < REQUIRED_WIDTH) {
        attron(COLOR_PAIR(CRED));
        printw("Window must be wider (min %d cols)\n", REQUIRED_WIDTH);
        need_bigger_screen = true;
    }
    if (LINES < required_height) {
        attron(COLOR_PAIR(CRED));
        printw("Window must be taller (min %d cols)\n", required_height);
        need_bigger_screen = true;
    }
    if (need_bigger_screen) {
        return;
    }

    move(required_height - 4, 0);
    attron(COLOR_PAIR(CSEP));
    hline(ACS_BULLET, COLS);
    attron(COLOR_PAIR(CNORMAL));

    move(required_height - 1, 0);
    addstr("Type ESC to start, Control-H for Help.");
    draw_all_fields();
    set_current_field(fields);
}


void message(char *msg)
{
    move(7, 0);
    addstr(msg);
    move(xpos, ypos);
}


// ch is visible ascii -- enter into field if cursor is in a field
void handle_typing(int ch)
{
    if (current_field) {
        current_field->handle_typing(ch);
    }
}


// handle left/right/up/down arrow keys
void handle_move(int ch)
{
    if (!current_field) {
        return;
    }
    if (current_field->options) {
        if (ch == KEY_DOWN) {
            current_field->next_option();
        } else if (ch == KEY_UP) {
            current_field->prev_option();
        } else if (ch == KEY_RIGHT) {
            tab_to_field();
        } else if (ch == KEY_LEFT) {
            move_to_end_of_previous_field();
        }
    } else {
        if (ch == KEY_RIGHT) {
            if (current_field->cursor_in_field_text()) {
                moveyx(ypos, xpos + 1);
            } else {
                tab_to_field();  // go to next field
            }
        } else if (ch == KEY_LEFT) {
            if (current_field->cursor_after_field_text()) {
                moveyx(ypos, xpos - 1);
            } else {
                move_to_end_of_previous_field();
            }
        } else if (ch == KEY_UP) {
            move_to_line(-1);
        } else if (ch == KEY_DOWN) {
            move_to_line(1);
        }
    }
}


bool configure()
{
    int ch = getch();
/*
//  get key codes and print them for debugging/development:
    printw("Character %d\n", ch);
    return true;
 */
    if (ch == KEY_RESIZE) {
        draw_screen();
    } else if (help_mode && ch == ESC_CHAR) {
        help_mode = false;
        draw_screen();
    } else if (need_bigger_screen) {
        ;  // wait for bigger screen before processing
    } else if (ch == '\t' || ch == '\n') {
        tab_to_field();
    } else if (isgraph(ch) || ch == ' ') {
        handle_typing(ch);
    } else if (ch == KEY_LEFT || ch == KEY_RIGHT ||
               ch == KEY_UP || ch == KEY_DOWN) {
        handle_move(ch);
    } else if (ch == DEL_CHAR) {
        handle_typing(ch);
    } else if (ch == ESC_CHAR) {
        return false; // done
    } else if (ch == KEY_BACKSPACE) {
        help_mode = true;
        draw_screen();
    }
    return true;
}


void remove_info_line(int n_fields)
{
    Field_entry *remove[6];
    Field_entry *new_current_field = current_field->next;
    for (int i = 0; i < n_fields; i++) {
        remove[i] = current_field;
        move_to_end_of_previous_field();
    }

    int y = remove[0]->y;
    // remove line y from display
    delete_or_insert(y, -1);
    current_field->next = new_current_field;  // unlink info
    // we might be deleting the list element "insert_after". If so, move it.
    if (insert_after == remove[0]) {
        insert_after = current_field;  // where to insert an addition
    }
    for (int i = 0; i < n_fields; i++) {
        delete remove[i];
    }

    if (!new_current_field) {
        new_current_field = fields;
    }
    set_current_field(new_current_field);
}



void do_command(Field_entry *field)
{
    if (field == &configuration_load) {
        do_configuration_load();
    } else if (field == &configuration_delete) {
        do_configuration_delete();
    } else if (field == &configuration_save) {
        do_configuration_save();
    } else if (field == &new_o2_to_osc) {
        insert_o2_to_osc();
    } else if (field == &new_osc_to_o2) {
        insert_osc_to_o2();
    } else if (field == &new_o2_to_midi) {
        insert_o2_to_midi();
    } else if (field == &new_midi_to_o2) {
        insert_midi_to_o2();
    } else if (field->marker == O2TOOSC_MARKER) {
        remove_info_line(5);
    } else if (field->marker == OSCTOO2_MARKER) {
        remove_info_line(4);
    } else if (field->marker == MIDIOUT_MARKER) {
        remove_info_line(3);
    } else if (field->marker == MIDIIN_MARKER) {
        remove_info_line(3);
    } else if (field == &midi_refresh) {
        midi_devices_refresh();
    }
}


int main(int argc, char **argv)
{
    int rslt = read_config();
    if (rslt == 0) {
        printf("WARNING: preference file not found\n");
    } else if (rslt == -1) {
        printf("ERROR: preference file could not be parsed\n");
    }
    // start up curses
    initscr();
    start_color();
    init_pair(CNORMAL, COLOR_BLACK, BRIGHT_WHITE);
    init_pair(CTITLE, BRIGHT(COLOR_YELLOW), COLOR_BLUE);
    init_pair(CRED, COLOR_RED, BRIGHT_WHITE);
    init_pair(CSEP, BRIGHT(COLOR_BLUE), BRIGHT_WHITE);
    init_pair(CHELP, BRIGHT_WHITE, COLOR_BLUE);
    attron(COLOR_PAIR(CNORMAL));
    cbreak();
    noecho();
    keypad(stdscr, true);
    configuration.set_menu_options(configuration_menu_options);
    configuration_load.is_button = true;
    configuration_delete.is_button = true;
    configuration_save.is_button = true;
    polling_rate.is_integer = true;
    networking.set_menu_options(net_options);
    websockets.set_menu_options(enable_options);
    mqtt_enable.set_menu_options(enable_options);
    mqtt_port.is_integer = true;
    insert_after = &mqtt_port;
    new_o2_to_osc.is_button = true;
    new_osc_to_o2.is_button = true;
    new_midi_to_o2.is_button = true;
    new_o2_to_midi.is_button = true;
    midi_refresh.is_button = true;
    // now we have the fields in place to load the last-current configuration:
    if (rslt == 1) {
        do_configuration_load();
    }
    draw_screen();
    while (configure()) ;
    endwin();  // restore terminal settings
    // terminal becomes output only

    printf("You have defined %d of 20 maximum configurations\n", n_conf_list);
    printf("Starting O2 process for ensemble %s\n", ensemble_name.content);
    printf("Polling rate %d\n", atoi(polling_rate.content));
    printf("Debug flags: %s\n", debug_flags.content);
    printf("Network option: %s\n", networking.content);
    printf("WebSockets: %s\n", websockets.content);
    printf("MQTT: %s\n", mqtt_enable.content);
    if (strcmp(mqtt_enable.content, "Enable") == 0) {
        printf("MQTT Host %s Port %s\n", mqtt_host.content, mqtt_port.content);
    }

    int networking_option = networking.current_option(0);
    o2_network_enable(networking_option != 0);
    o2_internet_enable(networking_option > 1);
    if (networking_option == 2) {
        o2_mqtt_enable(NULL, 0);
    }

    if (!ensemble_name.content[0]) {
        printf("Configuration error: O2 cannot start without "
               "an ensemble name\n");
        return 1;
    }

    o2_debug_flags(debug_flags.content);
    printf("Type Ctrl-C to exit.\nInitialize: ");

    O2err err = o2_initialize(ensemble_name.content);
    if (err) {
        printf("%s, exiting now.\n", o2_error_to_string(err));
        return 1;
    } else {
        printf("success.\n");
    }

    for (Field_entry *field = fields; field; field = field->next) {
        if (field->marker == O2TOOSC_SERV_MARKER) {
            const char *service = field->content;
            const char *ip = field->next->content;
            int port = atoi(field->next->next->content);
            field = field->next->next->next;
            bool tcp_flag = (strcmp(field->content, "TCP") == 0);
            printf("O2 to OSC Service %s via %s to IP %s Port %d\n",
                   service, field->content, ip, port);
            if (!service[0]) {
                printf("WARNING: Service name is missing;"
                       " ignoring this option\n");
            } else if (ip[0] == ' ' && ip[1] == ' ' && ip[2] == ' ') {
                printf("WARNING: IP address is incomplete;"
                       " ignoring this option\n");
            } else if (port == 0) {
                printf("WARNING: Port number is missing;"
                       " ignoring this option\n");
            } else {
                O2err err = o2_osc_delegate(service, ip, port, tcp_flag);
                if (err) {
                    printf("WARNING: O2 error %s\n", o2_error_to_string(err));
                }
            }
        } else if (field->marker == OSCTOO2_UDP_MARKER) {
            bool tcp_flag = (strcmp(field->content, "TCP") == 0);
            int port = atoi(field->next->content);
            const char *service = field->next->next->content;
            printf("OSC from %s Port %d to O2 Service %s\n", field->content,
                   port, service);
            if (port == 0) {
                printf("WARNING: Port number is missing;"
                       " ignoring this option\n");
            } else if (!service[0]) {
                printf("WARNING: Service name is missing;"
                       " ignoring this option\n");
            } else {
                O2err err = o2_osc_port_new(service, port, tcp_flag);
                if (err) {
                    printf("WARNING: O2 error %s\n", o2_error_to_string(err));
                }
            }
        } else if (field->marker == MIDIIN_NAME_MARKER) {
            midi_input_initialize(field);
        } else if (field->marker == MIDIOUT_SERV_MARKER) {
            midi_output_initialize(field);
        }
    }

    // configure services
    for (Field_entry *field = fields; field; field = field->next) {
        if (field->marker == O2TOOSC_SERV_MARKER) {
            O2err err = o2_osc_delegate(field->content, field->next->content,
                                atoi(field->next->next->content),
                                field->next->next->next->content[0] == 'T');
            if (err) {
                printf("Error: failed to delegate service %s to OSC, code %d\n",
                       field->content, err);
            }
        } else if (field->marker == OSCTOO2_UDP_MARKER) {
            O2err err = o2_osc_port_new(field->next->next->content,
                    atoi(field->next->content), field->content[0] = 'T');
            if (err) {
                printf("Error: failed to open OSC port %s, code %d\n",
                       field->next->content, err);
            }
        } else if (field->marker == MIDIIN_NAME_MARKER) {
            if (midi_input_initialize(field)) {
                continue;
            }
        } else if (field->marker == MIDIOUT_SERV_MARKER) {
            if (midi_output_initialize(field)) {
                continue;
            }
        }
    }

    fflush(stdout);

    // run
    if (host_rate <= 0) host_rate = 1000;
    int sleep_ms = 1000 / host_rate;
    if (sleep_ms < 1) sleep_ms = 1;
    while (true) {
        o2_poll();
        midi_poll();
    }
}
