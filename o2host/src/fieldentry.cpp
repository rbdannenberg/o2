// fieldentry.cpp -- an object to manage one field in a curses form
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
#include "fieldentry.h"
#include "configuration.h"
#include "o2host.h"

static int host_period = 2;
static int host_rate = 500;

// configure -- handle keyboard input to set O2 configuration

#define DEL_CHAR 0x7f

// find index of content in array of strings (options), return dflt if none
// are found
int string_list_index(const char **options, const char *content, int dflt)
{
    int i = 0;
    while (options[i]) {
        if (strcmp(options[i], content) == 0) {
            return i;
        }
        i++;
    }
    return dflt;
}


Field_entry *fields = NULL;
Field_entry *insert_after = NULL;
Field_entry *last_field = NULL;
Field_entry *current_field = NULL;

Field_entry::Field_entry(int label_x_, int x_, int y_, const char *label_,
                         int max_width_, Field_entry *after)
{
    label_x = label_x_;
    label = label_;
    after_field = NULL;
    x = x_;
    y = y_;
    max_width = max_width_;
    assert(max_width <= MAX_NAME_LEN);
    width = 0;
    content[0] = 0;
    options = NULL;
    is_integer = false;
    is_button = false;
    is_ip = false;
    allow_spaces = false;
    marker = UNMARKED_FIELD;
    next = NULL;
    // insert this into list of fields
    if (after) {
        next = after->next;
        after->next = this;
    } else {
        if (!fields) {
            fields = this;
        } else {
            last_field->next = this;
        }
        last_field = this;
    }
    if (!current_field) {
        set_current_field(this);
    }
}


// preserve the current selection if it is in options. Otherwise,
// select the first option if there are any. Otherwise, set the
// selection to the empty string.
void Field_entry::set_field_to_option()
{
    int i = current_option(0);  // find if content matches an option
    set_option(i);
}


void Field_entry::set_menu_options(const char *options_[])
{
    options = options_;
    set_field_to_option();
}


void Field_entry::set_ip()
{
    is_ip = true;
    is_integer = true;
    strcpy(content, "   .   .   .   ");
    width = 15;
}


// write the content, restore cursor to xpos, ypos
void Field_entry::show_content()
{
    move(y, label_x);
    addstr(label);
    int pad_to = (is_ip ? x + IP_LEN : x);
    // pad with blanks after label to field's start x:
    for (int i = label_x + (int) strlen(label); i < pad_to; i++) {
        addstr(" ");
    }
    move(y, x);
    attron(A_UNDERLINE);
    addstr(content);
    for (int i = x + width; i < x + max_width; i++) {
        addstr(" ");  // pad with blanks to erase previous text
    }
    attroff(A_UNDERLINE);
    if (after_field) {
        addstr(after_field);
    }
    moveyx(ypos, xpos);  // restore cursor
}

void Field_entry::handle_typing(int ch)
{
    // do not allow typing if this is multiple choice field
    if (!options && cursor_in_or_after_field()) {
        if (ch == DEL_CHAR) {
            if (xpos <= x) {
                ;  // ignore DEL if you are at the beginning of field
            } else if (is_ip) {
                // delete in IP address edits a single byte
                int loc = xpos - x - 1;  // location to delete
                if (content[loc] == '.') {
                    ;  // if delete is after '.', then ignore
                } else {  // shift from right; pad with blank
                    xpos = x + loc;
                    if (loc % 4 == 0) {
                        content[loc] = content[loc + 1];
                        loc++;
                    }
                    if (loc % 4 == 1) {
                        content[loc] = content[loc + 1];
                        loc++;
                    }
                    if (loc % 4 == 2) {
                        content[loc] = ' ';
                    }
                    show_content();
                }
            } else if (width > 0) {
                strcpy(content + xpos - x - 1, content + xpos - x);
                width--;
                xpos--;
                show_content();
            } // else ignore DEL
        } else if (is_integer && !isdigit(ch) &&
                   (!allow_spaces || ch != ' ')) {
            ;  // ignore non-digits if field is an integer (also for is_ip)
        } else if (is_button) {
            if (ch == 'y' || ch == 'Y' || ch == 'x' || ch == 'X') {
                do_command(this);
            }
        } else if ((xpos - x <= max_width) &&
                   (allow_spaces || ch != ' ')) {
            content[xpos - x] = ch;
            content[xpos + 1 - x] = 0;  // EOS
            xpos++;
            width = (int) strlen(content);
            int loc = xpos - x;
            if (is_ip && (loc == 3 || loc == 7 || loc == 11)) {
                moveyx(ypos, xpos + 1);    // skip over '.'s
            }
            show_content();
        }
    }
}


int Field_entry::current_option(int dflt)
{
    return string_list_index(options, content, dflt);
}


void Field_entry::set_content(const char *s)
{
    strncpy(content, s, max_width);
    width = (int) strlen(content);
}


// used to transfer configuration data to the display when
// the stored data is an int rather than a string. Zero
// value is a special case that is displayed as if_zero,
// which is normally either "" or "0", e.g. we encode
// unspecified port numbers as 0, so we want to display
// "unspecified" with the empty string "".
void Field_entry::set_number(int i, const char *if_zero)
{
    if (i == 0) {
        strncpy(content, if_zero, max_width);
    } else {
        snprintf(content, max_width, "%d", i);
    }
    width = (int) strlen(content);
}


void Field_entry::set_option(int i)
{
    if (!options[i]) {  // NULL option -> empty string
        content[0] = 0;
    } else {
        strncpy(content, options[i], max_width);
        content[max_width] = 0;
    }
    width = (int) strlen(content);        
}


void Field_entry::next_option()
{
    if (!options) {
        return;  // ignore command if not an option
    }
    int i = current_option(-1) + 1;
    // note that if content did not match any option, then
    // i will not be zero, the first option, which is a good choice
    if (!options[i]) {
        i = 0;  // wrap to first option
    }
    set_option(i);
    show_content();
}

Field_entry *Field_entry::save(FILE *outf, const char *prefix, bool newline)
{
    fprintf(outf, "%s \"%s\"%s", prefix, content, newline ? "\n" : "");
    return next;
}

void Field_entry::prev_option()
{
    int i = current_option(-1) - 1;
    if (i == -1) {  // wrap to last
        while (options[i + 1]) i++;
    } else if (i == -2) {  // content did not match any option
        i = 0;  // set to first option
    }
    set_option(i);
    show_content();
}

// cursor is on some text of this field
bool Field_entry::cursor_in_field_text()
{
    return xpos >= x && xpos < x + width && ypos == y;
}

// cursor is to the right of any existing text (1 to len)
bool Field_entry::cursor_after_field_text()
{
    return xpos > x && xpos <= x + width && ypos == y;
}

// cursor is within this field
bool Field_entry::cursor_in_or_after_field()
{
    return xpos >= x && xpos <= x + max_width && ypos == y;
}


// draw all (empty) fields:
void draw_all_fields()
{
    for (Field_entry *field = fields; field; field = field->next) {
        field->show_content();
    }
}


void set_current_field(Field_entry *field)
{
    current_field = field;
    // current_field could be NULL, either because it started that way or
    // we reached the end of the fields list: either way, move to first field:
    if (!current_field) {
        current_field = fields;  // first one
    }
    if (current_field) {
        moveyx(current_field->y, current_field->x);
        wrefresh(stdscr);
    }
}


void delete_or_insert(int y, int inc)
{
    move(y, 0);
    if (inc == 1) {
        required_height++;
        insertln();
    } else {
        required_height--;
        deleteln();
    }
    // adjust all fields that changed lines
    for (Field_entry *field = fields; field; field = field->next) {
        if (inc == -1 && field->y > y) {
            field->y--;
        } else if (inc == 1 && field->y >= y) {
            field->y++;
        }
    }
}

void move_to_line(int direction)
{
    if (!current_field) {
        current_field = fields;
    }
    if (!current_field) {
        return;
    }
    int y = current_field->y + direction;
    while (true) {  // if no other line has a field, this will wrap
                    // around to the first field in the current_field's line
        for (Field_entry *field = fields; field; field = field->next) {
            if (field->y == y) {
                set_current_field(field);
                return;
            }
        }
        y = (y + direction + LINES) % LINES;
    }
}


void tab_to_field()
 {
    // find field after this one (wraps around at end)
    if (current_field) {
        set_current_field(current_field->next);  // handles wrapping
    }
}


void move_to_end_of_previous_field()
{
    Field_entry *field;
    for (field = fields; field->next && field->next != current_field;
         field = field->next) {
        ;
    }
    set_current_field(field);
}
