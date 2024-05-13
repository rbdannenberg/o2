// midiservice.cpp -- support option for MIDI I/O from o2host
//
// Roger B. Dannenberg
// Feb 2024

#include "o2.h"
#include "fieldentry.h"
#include "o2host.h"
#include "portmidi.h"


// these are non-null after get_midi_device_options is called
const char **midi_in_devices = NULL;
const char **midi_out_devices = NULL;

int num_midi_in = 0;
int num_midi_out = 0;
PmStream *midi_input_streams[10];
const char *midi_input_addresses[10];
PmStream *midi_output_streams[10];


void get_midi_device_options()
{
    if (midi_in_devices) {
        return;
    }
    Pm_Initialize();
    // need to construct options
    int n = Pm_CountDevices();
    int num_in = 0;
    int num_out = 0;
    for (int i = 0; i < n; i++) {
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        num_in += (info->input != 0);
        num_out += (info->output != 0);
    }
    midi_in_devices = new const char*[num_in + 1];
    midi_out_devices = new const char* [num_in + 1];

    num_in = 0;
    num_out = 0;
    for (int i = 0; i < n; i++) {
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (info->input) {
            midi_in_devices[num_in++] = info->name;
        } else {
            midi_out_devices[num_out++] = info->name;
        }
    }
    midi_in_devices[num_in] = NULL;
    midi_out_devices[num_out] = NULL;
    // now we have option lists for all midi devices
}


void free_midi_device_names()
{
    if (midi_in_devices) {
        delete midi_in_devices;
        midi_in_devices = NULL;
    }
    if (midi_out_devices) {
        delete midi_out_devices;
        midi_out_devices = NULL;
    }
}


// reconstruct menus
void midi_devices_refresh()
{
    if (midi_in_devices) {
        free_midi_device_names();
        Pm_Terminate();
    }
    get_midi_device_options();

    // restore all fields with valid midi device names
    for (Field_entry *field = fields; field; field = field->next) {
        if (field->marker == MIDIOUT_NAME_MARKER) {
            field->set_menu_options(midi_out_devices);
            field->show_content();
        } else if (field->marker == MIDIIN_NAME_MARKER) {
            field->set_menu_options(midi_in_devices);
            field->show_content();
        }
    }
}


void insert_o2_to_midi() {
    get_midi_device_options();

    if (!midi_out_devices[0]) {  // not a single output device
        print_error("There are no MIDI output devices.");
        return;
    }

    // open up a new line of the console - last line is the line of
    // new_osc_to_o2
    int y = new_o2_to_osc.y - 2;
    // insert a line on the interface at y
    delete_or_insert(y, 1);

    Field_entry *service = new Field_entry(0, MIDIOUT_SERV_X, y,
               "MIDI Out Service", MIDIOUT_SERV_W, insert_after);
    service->marker = MIDIOUT_SERV_MARKER;
    service->show_content();
    set_current_field(service);

    // add three fields
    Field_entry *name = new Field_entry(MIDIOUT_LABEL_X, MIDIOUT_X, y,
                                        "to", MIDIOUT_W, service);
    name->set_menu_options(midi_out_devices);
    name->marker = MIDIOUT_NAME_MARKER;
    name->show_content();

    Field_entry *delete_me = new Field_entry(MIDIOUT_DELLABEL_X, MIDIOUT_DEL_X,
                                             y, "(X", 1, name);
    delete_me->is_button = true;
    delete_me->marker = MIDIOUT_MARKER;
    delete_me->after_field = ")";
    delete_me->show_content();
    insert_after = delete_me;
    if (required_height > LINES) {
        draw_screen();
    }
}


void insert_midi_to_o2() {
    get_midi_device_options();

    if (!midi_in_devices[0]) {  // not a single input device
        print_error("There are no MIDI input devices.");
        return;
    }

    // open up a new line of the console - last line is the line of
    // new_osc_to_o2
    int y = new_o2_to_osc.y - 2;
    // insert a line on the interface at y
    delete_or_insert(y, 1);

    // add three fields
    Field_entry *name = new Field_entry(0, MIDIIN_X, y,
                                        "MIDI In", MIDIIN_W, insert_after);
    name->set_menu_options(midi_in_devices);
    name->marker = MIDIIN_NAME_MARKER;
    name->show_content();
    set_current_field(name);

    Field_entry *service = new Field_entry(MIDIIN_SERVLABEL_X,
                MIDIIN_SERV_X, y, "to Service", MIDIIN_SERV_W, name);
    service->show_content();

    Field_entry *delete_me = new Field_entry(MIDIIN_DELLABEL_X, MIDIIN_DEL_X,
                                             y, "(X", 1, service);
    delete_me->is_button = true;
    delete_me->marker = MIDIIN_MARKER;
    delete_me->after_field = ")";
    delete_me->show_content();
    insert_after = delete_me;
    if (required_height > LINES) {
        draw_screen();
    }
}


void print_pmerror(PmError pm_err)
{
    const char *msg;
    if (pm_err != pmHostError) {
        msg = Pm_GetErrorText(pm_err);
    } else {
        char message[80];
        Pm_GetHostErrorText(message, 80);
        msg = message;
    }
    printf("%s\n", msg);
}


void midi_input_initialize(Field_entry *field)
{
    const char *device = field->content;
    const char *service = field->next->content;
    printf("MIDI input %s to O2 service %s\n", device, service);
    int dev_id = string_list_index(midi_in_devices, device, -1);
    if (dev_id < 0) {
        printf("WARNING: MIDI input %s is not (no longer) available\n",
               device);
    } else {
        PmError pmerr = Pm_OpenInput(&midi_input_streams[num_midi_in],
                                     dev_id, NULL, 100, NULL, NULL);
        if (pmerr) {
            printf("WARNING: Could not open %s: ", device);
            print_pmerror(pmerr);
        } else {
            char address[MAX_NAME_LEN * 2];
            address[0] = '/';
            strcpy(address + 1, service);
            strcpy(address + strlen(address), "/midi");
            midi_input_addresses[num_midi_in++] = heapify(address);
        }
    }
}


void midi_message_handler(O2_HANDLER_ARGS)
{
    intptr_t output_index = *((intptr_t *) user_data);
    if ((types[0] == 'i' || types[0] == 'm') && types[1] == 0) {
        int midi_msg = *((int32_t *)o2_msg_data_params(types));
        printf("got midi message from o2, address %s: %x\n",
               msg->address, midi_msg);
        Pm_WriteShort(midi_output_streams[output_index], 0, midi_msg);
    }
}


// look for the pattern "\n---- "
//
bool skip_to_config(FILE *inf)
{
    int c;
    int count = 0;
    while ((c = getc(inf)) != EOF) {
        if (count == 0 && c == '\n') count = 1;
        else if (count > 0 && c == '-') count++;
        else if (count == 5 && c == ' ') return true;
        else count = 0; // look for newline
    }
    return false;  // not found
}


void midi_output_initialize(Field_entry *field)
{
    const char *service = field->content;
    const char *device = field->next->content;
    printf("MIDI Output from O2 service %s to %s\n", service, device);
    int dev_id = string_list_index(midi_out_devices, device, -1);
    if (dev_id < 0) {
        printf("WARNING: MIDI output %s is not (no longer) available\n",
               device);
    } else {
        PmError pmerr = Pm_OpenOutput(&midi_input_streams[num_midi_in],
                                      dev_id, NULL, 100, NULL, NULL, 0);
        if (pmerr) {
            char msg[128];
            snprintf(msg, 128, "WARNING: Could not open %s: ", device);
            printf("%s", msg);
            print_pmerror(pmerr);
            print_error(msg);
            return;
        }
    }
    o2_service_new(field->content);
    char address[MAX_NAME_LEN * 2];
    address[0] = '/';
    strcpy(address + 1, field->content);
    strcpy(address + strlen(address), "/midi");
    O2err o2err = o2_method_new(address, NULL, midi_message_handler,
                                (void *) (intptr_t) num_midi_out, false, false);
    if (o2err) {
        char msg[128];
        snprintf(msg, 128, "Error: could not create handler for %s\n", address);
        print_error(msg);
        return;
    }
}


void midi_poll()
{
    PmEvent buffer[10];
    for (int i = 0; i < num_midi_in; i++) {
        int n = Pm_Read(midi_input_streams[i], buffer, 10);
        // ignore errors (n < 0)
        for (int j = 0; j < n; j++) {
            o2_send_cmd(midi_input_addresses[i], 0, "i", buffer[j].message);
        }
    }
}
