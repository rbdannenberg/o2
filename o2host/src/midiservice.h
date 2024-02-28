// midiservice.h -- support option for MIDI I/O from o2host
//
// Roger B. Dannenberg
// Feb 2024

void get_midi_device_options();

void free_midi_device_names();

// reconstruct menus
void midi_devices_refresh();

int midi_input_initialize(Field_entry *field);

int midi_output_initialize(Field_entry *field);

void midi_poll();
