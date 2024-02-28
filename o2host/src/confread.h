// confread.cpp -- read configurations
//
// Roger B. Dannenberg
// Feb 2024

// configuration file: name is o2host.config or .o2host.config
// format is as follows. All fields are quoted strings to handle spaces
// and empty fields:
//
// o2host v1.0
// Configuration: <configuration> [gives current selection]
// ---- <configuration>
// Ensemble_name: <name>
// Debug_flags: <flags>
// Networking: <string>
// MQTT_host: <string>
// MQTT_port: <string>
// O2_to_OSC: <servicename> <IP> <port> UDP
// OSC_to_O2: UDP <port> <servicename>
// MIDI_in: <devicename> <servicename>
// MIDI_out: <servicename> <devicename>
// ----
// ---- <next configuration name>
// ...


// returns 1 if pref file read, 0 if no file found, -1 if error
int read_config();
