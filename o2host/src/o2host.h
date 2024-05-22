// o2host.h -- a host process for o2lite, run in a shell/cmd prompt/terminal
//
// Roger B. Dannenberg
// Feb 2024

extern const char *y_or_n_options[];
extern const char *net_options[];
extern const char *enable_options[];
extern const char *udp_tcp_options[];
extern const char *configuration_list[];
extern void moveyx(int y, int x);
extern char pref_path[128];
extern bool redraw_requested;

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
extern Field_entry http_port;
extern Field_entry http_root;
extern Field_entry mqtt_host;
extern Field_entry mqtt_port;

extern int required_height;  // initial value

void print_error(const char *msg);
void draw_screen();
const char *heapify(const char *s);
