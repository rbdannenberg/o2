// o2host.h -- a host process for o2lite, run in a shell/cmd prompt/terminal
//
// Roger B. Dannenberg
// Feb 2024


// general layout of curses screen is defined here and in o2host.cpp,
// even when these coordinates are specific to some other source file
#define CONF_W 20
#define CONF_LIST_MAX 20
#define POLL_W 4
#define NET_W 19
#define WEB_W 7
#define MQTTENAB_W 7

#define O2TOOSC_SERV_X 12
#define O2TOOSC_SERV_W 20
#define O2TOOSC_IPLABEL_X 33
#define O2TOOSC_IP_X 43
#define O2TOOSC_PORTLABEL_X 59
#define O2TOOSC_PORT_X 64
#define O2TOOSC_UDP_X 70
#define O2TOOSC_UDP_W 3
#define O2TOOSC_DELLABEL_X 74
#define O2TOOSC_DEL_X 76
#define OSCTOO2_UDP_X 13
#define OSCTOO2_UDP_W 3
#define OSCTOO2_PORTLABEL_X 17
#define OSCTOO2_PORT_X 22
#define OSCTOO2_SERVLABEL_X 28
#define OSCTOO2_SERV_X 39
#define OSCTOO2_SERV_W 20
#define OSCTOO2_DELLABEL_X 60
#define OSCTOO2_DEL_X 62
#define IP_LEN 15
#define PORT_LEN 5

#define MIDIIN_X 8
#define MIDIIN_W 29
#define MIDIIN_SERVLABEL_X 38
#define MIDIIN_SERV_X 49
#define MIDIIN_SERV_W 20
#define MIDIIN_DELLABEL_X 70
#define MIDIIN_DEL_X 72

#define MIDIOUT_SERV_X 17
#define MIDIOUT_SERV_W 20
#define MIDIOUT_LABEL_X 38
#define MIDIOUT_X 41
#define MIDIOUT_W 28
#define MIDIOUT_DELLABEL_X 70
#define MIDIOUT_DEL_X 72


// marks the delete_me field of O2_to_OSC line:
#define O2TOOSC_MARKER 1
// marks the delete_me field of OSC_to_O2 line:
#define OSCTOO2_MARKER 2
// marks the delete_me field of MIDI_out line:
#define MIDIOUT_MARKER 3
// marks the delete_me field of MIDI_in line:
#define MIDIIN_MARKER 4
// marks the first field of a MIDI_out line:
// marks the first field of an O2_to_OSC line:
#define O2TOOSC_SERV_MARKER 7
// marks the first field of an OSC_to_O2 line:
#define OSCTOO2_UDP_MARKER 8
// marks the service name for MIDI_out line; also marks a Service_config:
#define MIDIOUT_SERV_MARKER 5
// marks the first field of a MIDI_in line; also marks a Service_config:
#define MIDIIN_NAME_MARKER 6
// marks the device name field for MIDI_out:
#define MIDIOUT_NAME_MARKER 9


extern const char *y_or_n_options[];
extern const char *net_options[];
extern const char *enable_options[];
extern const char *udp_tcp_options[];
extern const char *configuration_list[];
extern void moveyx(int y, int x);
extern char pref_path[128];

// cursor position:
extern int xpos;
extern int ypos;

void reset_lower_field_positions();
void insert_o2_to_midi();
void insert_midi_to_o2();

extern Field_entry configuration;
extern Field_entry configuration_rename;
extern Field_entry new_o2_to_osc;
extern Field_entry ensemble_name;
extern Field_entry polling_rate;
extern Field_entry debug_flags;
extern Field_entry reference_clock;
extern Field_entry networking;
extern Field_entry websockets;
extern Field_entry mqtt_host;
extern Field_entry mqtt_port;

extern int required_height;  // initial value
// if this is set, we just wait for a bigger screen
//extern bool need_bigger_screen = false;
//extern bool help_mode = false;

void print_error(const char *msg);
void draw_screen();
const char *heapify(const char *s);
