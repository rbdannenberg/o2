// configuration.h -- a Configuration represents all the information needed to
//                    initialize o2host.
//
// Roger B. Dannenberg
// Feb 2024


#include "assert.h"
#include "string.h"


// Service_config's contain information from a sequence of fields.
// The marker of the first field in the sequence could be used as the
// marker for the Service_config, but this led to an unending series
// of errors and confusion, so now the Service_config markers have
// distinct values (and type), but still related by adding 100 to the
// first field marker value.
enum Service_config_marker {
    O2TOOSC_CONFIG = 101,  // relates to O2TOOSC_SERV_FIELD
    OSCTOO2_CONFIG = 103,  // relates to OSCTOO2_UDP_FIELD
    MIDIOUT_CONFIG = 105,  // relates to MIDIOUT_SERV_FIELD
    MIDIIN_CONFIG = 108    // relates to MIDIIN_NAME_FIELD
};


class Service_config {
  public:
    Service_config_marker marker;  // the type of service:  MIDIOUT_SERV,
                           // MIDIIN_SERV, O2TOOSC_SERV, OSCTOO2_UDP_SERV
    char service_name[MAX_NAME_LEN + 1];  // the associated service name
    char ip[IP_LEN + 1];                  // IP for O2TOOSC_MARKER
    int port;            // port for O2TOOSC_MARKER, OSCTOO2_UDP_MARKER
    bool tcp_flag;       // TCP or UDP for O2TOOSC and OSCTOO2
    char midi_device[MAX_NAME_LEN + 1];  // MIDI device name
    Service_config *next;  // link to next Service_config

    Service_config(Service_config_marker marker_);

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
    int http_port;
    char http_root[MAX_NAME_LEN + 1];  // path to root of HTTP web pages
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

int find_configuration(const char *name);
void do_configuration_load();
void do_configuration_delete();
void do_configuration_save();
void do_configuration_new();
