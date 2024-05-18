// fieldentry.h -- an object to manage one field in a curses form
//
// Roger B. Dannenberg
// Feb 2024

#include "assert.h"
//#include "o2.h"
//#include "portmidi.h"
#include "ctype.h"
#include "curses.h"
#include "string.h"
//#include "sys/stat.h"
//#include <unistd.h>

#define MAX_NAME_LEN 31

// general layout of curses screen is defined here and in o2host.cpp,
// even when these coordinates are specific to some other source file
#define CONF_W 20
#define CONF_LIST_MAX 20
#define POLL_W 4
#define NET_W 19
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


enum Field_marker {
    UNMARKED_FIELD = 0,  // default used for most fields; not all fields need
    // to be identified since fields are in a linked list in a specific order

    // O2 to OSC:
    // Fwd Service ______________ to OSC IP ___.___.___.___ Port _____ UDP (X_)
    O2TOOSC_SERV_FIELD = 1, // marks the first field of an O2_to_OSC line
    O2TOOSC_DEL_FIELD = 2,  // marks the delete_me field of O2_to_OSC line

    // OSC to O2:
    // Fwd OSC from UDP Port _____ to Service ____________________ (X_)
    OSCTOO2_UDP_FIELD = 3,  // marks the first field of an OSC_to_O2 line
    OSCTOO2_DEL_FIELD = 4,  // marks the delete_me field of OSC_to_O2 line

    // O2 TO MIDI:
    // MIDI Out Service ___________________ to ___________________________ (X_)
    MIDIOUT_SERV_FIELD = 5,  // marks the first field of a MIDI_out line
    MIDIOUT_NAME_FIELD = 6, // marks the service name for MIDI_out line
    MIDIOUT_DEL_FIELD = 7,  // marks the delete_me field of MIDI_out line

    // MIDI TO O2:
    // MIDI In _____________________________to Service ___________________ (X_)
    MIDIIN_NAME_FIELD = 8,  // marks the device name field for MIDI_in
    MIDIIN_DEL_FIELD = 9  // marks the delete_me field of MIDI_in line
};


class Field_entry {
  public:
    int label_x;
    int x;
    int y;
    const char *label;
    const char *after_field;  // put this text (if non-NULL) after field
    int max_width;  // does not include null terminator
    int width;      // equals strlen(content); does not include null terminator
    char content[MAX_NAME_LEN + 1];
    const char **options;
    bool is_integer;
    bool is_button;  // display _ and call do_command() if x,X,y,Y typed
    bool is_ip;  // display looks like ___.___.___.___
    bool allow_spaces;  // allow typing spaces into field
    Field_marker marker;
    Field_entry *next;

    // label_x_ is where to put the label
    // x_ is where to put the field
    // if after is non-null, we insert this field after it in the list,
    // otherwise we insert this at the end of the list.
    Field_entry(int label_x_, int x_, int y_, const char *label_,
                int max_width_, Field_entry *after);


    // preserve the current selection if it is in options. Otherwise,
    // select the first option if there are any. Otherwise, set the
    // selection to the empty string.
    void set_field_to_option();

    // set the field to be an option menu using list of options_, which
    // is an array of pointers to strings followed by a NULL pointer.
    void set_menu_options(const char *options_[]);

    // set the field to be an IP address (nnn.nnn.nnn.nnn notation)
    void set_ip();

    // write the content, restore cursor to xpos, ypos
    void show_content();

    // this is the current entry and ch was typed:
    void handle_typing(int ch);
    
    // get the index of the currently selected option
    int current_option(int dflt);
    
    // set content to a string value
    void set_content(const char *s);
    
    // set a numerical field to an integer
    void set_number(int i, const char *if_zero);

    // select option i
    void set_option(int i);

    // select the next option
    void next_option();
    
    // save a field to (preference) file
    Field_entry *save(FILE *outf, const char *prefix, bool newline);

    // select the previous option
    void prev_option();
    
    // cursor is on some text of this field
    bool cursor_in_field_text();

    // cursor is to the right of any existing text (1 to len)
    bool cursor_after_field_text();

    // cursor is within this field
    bool cursor_in_or_after_field();
};

extern Field_entry *fields;
extern Field_entry *current_field;
extern Field_entry *insert_after;

// callout from Field_entry::handle_typing, defined in o2host.cpp
void do_command(Field_entry *field);

// find index of content in options
int string_list_index(const char **options, const char *content, int dflt);

// draw all (empty) fields:
void draw_all_fields();

// make field be the current field to edit
void set_current_field(Field_entry *field);

// delete (inc = -1) or insert (inc = +1) a line, moving all fields
// appropriately
void delete_or_insert(int y, int inc);

// advance to a field on the next (+1) or previous (-1) line
void move_to_line(int direction);

// advance to the next field
void tab_to_field();

// go to the end of the previous field:
void move_to_end_of_previous_field();

