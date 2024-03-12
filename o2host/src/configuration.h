// configuration.h -- a Configuration represents all the information needed to
//                    initialize o2host.
//
// Roger B. Dannenberg
// Feb 2024


#include "assert.h"
#include "string.h"


class Service_config {
  public:
    int marker;  // the type of service:  MIDIOUT_MARKER,
                 // MIDIIN_MARKER, O2TOOSC_MARKER, OSCTOO2_MARKER
    char service_name[MAX_NAME_LEN + 1];  // the associated service name
    char ip[IP_LEN + 1];            // IP for O2TOOSC_MARKER
    int port;            // port for O2TOOSC_MARKER, OSCTOO2_MARKER
    bool tcp_flag;       // TCP or UDP for O2TOOSC and OSCTOO2
    char midi_device[MAX_NAME_LEN + 1];  // MIDI device name
    Service_config *next;  // link to next Service_config

    Service_config(int marker_);

    ~Service_config();
};


class Configuration {
  public:
    char name[CONF_W + 1];          // the configuration name
    char ensemble[MAX_NAME_LEN + 1];   // the O2 ensembl name
    int polling_rate;
    char debug_flags[MAX_NAME_LEN + 1];  // the O2 debug flags string
    char reference_clock[4];  // "Y" or "N"
    int networking;   // the networking option index
    bool websockets;
    bool mqtt_enable;
    char mqtt_host[MAX_NAME_LEN + 1];  // MQTT host (broker) name or IP or NULL
    int mqtt_port;    // MQTT broker port number or 0
    Service_config *services;  // added service descriptors

    Configuration();

    void free_storage();

    ~Configuration();

    void save_to_pref(FILE *outf);
};

extern Configuration *conf_list[CONF_LIST_MAX];
extern const char *configuration_menu_options[CONF_LIST_MAX + 1];
extern int n_conf_list;

void do_configuration_load();
void do_configuration_delete();
void do_configuration_save();
