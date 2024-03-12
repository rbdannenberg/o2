// configuration.cpp -- a Configuration represents all the information needed to
//                      initialize o2host.
//
// Roger B. Dannenberg
// Feb 2024


#include "assert.h"
//#include "ctype.h"
//#include "curses.h"
#include "string.h"
//#include "sys/stat.h"
//#include <unistd.h>
#include "fieldentry.h"
#include "o2host.h"
#include "configuration.h"
#include "o2oscservice.h"


Service_config::Service_config(int marker_) {
    marker = marker_;
    service_name[0] = 0;
    ip[0] = 0;
    midi_device[0] = 0;
}

Service_config::~Service_config() {
    ;
}



char *port_as_string(int port)
{
    static char port_string[8];
    if (port == 0) {
        port_string[0] = 0;
    } else {
        snprintf(port_string, 7, "%d", port);
    }
    return port_string;
}


Configuration::Configuration() {
    name[0] = 0;
    ensemble[0] = 0;
    polling_rate = 0;
    debug_flags[0] = 0;
    reference_clock[0] = 'N';
    reference_clock[1] = 0;
    networking = 0;
    websockets = false;
    mqtt_enable = false;
    mqtt_host[0] = 0;
    mqtt_port = 0;
    services = NULL;
};


void Configuration::free_storage() {
    while (services) {
        Service_config *next = services->next;
        delete services;
        services = next;
    }
}


Configuration::~Configuration() {
    free_storage();
}


void Configuration::save_to_pref(FILE *outf) {
    fprintf(outf, "---- \"%s\"\n", name);
    fprintf(outf, "Ensemble_name: \"%s\"\n", ensemble);
    fprintf(outf, "Polling_rate: \"%d\"\n", polling_rate);
    fprintf(outf, "Debug_flags: \"%s\"\n", debug_flags);
    fprintf(outf, "Reference_clock: \"%s\"\n", reference_clock);
    fprintf(outf, "Networking: \"%s\"\n", net_options[networking]);
    fprintf(outf, "WebSockets: \"%s\"\n", websockets ? "Enable" : "Disable");
    fprintf(outf, "MQTT_enable: \"%s\"\n", mqtt_enable ? "Enable" : "Disable");
    fprintf(outf, "MQTT_host: \"%s\"\n", mqtt_host);
    fprintf(outf, "MQTT_port: \"%s\"\n", port_as_string(mqtt_port));
    Service_config *sc = services;
    while (sc) {
        if (sc->marker == O2TOOSC_SERV_MARKER) {
            fprintf(outf, "O2_to_OSC: \"%s\" \"%s\" \"%s\" \"%s\"\n",
                    sc->service_name, sc->ip, port_as_string(sc->port),
                    sc->tcp_flag ? "TCP" : "UDP");
        } else if (sc->marker == OSCTOO2_UDP_MARKER) {
            fprintf(outf, "OSC_to_O2: \"%s\" \"%s\" \"%s\"\n",
                    sc->tcp_flag ? "TCP" : "UDP", port_as_string(sc->port),
                    sc->service_name);
        } else if (sc->marker == MIDIOUT_MARKER) {
            fprintf(outf, "MIDI_out: \"%s\" \"%s\"\n",
                    sc->service_name, sc->midi_device);
        } else if (sc->marker == MIDIIN_MARKER) {
            fprintf(outf, "MIDI_in: \"%s\" \"%s\"\n", sc->midi_device,
                    sc->service_name);
        } else {
            assert(false);
        }
        sc = sc->next;
    }
    fprintf(outf, "----\n");
}


// configurations are stored here:
Configuration *conf_list[CONF_LIST_MAX];
const char *configuration_menu_options[CONF_LIST_MAX + 1];
int n_conf_list = 0;
char current_configuration[MAX_NAME_LEN + 1];


int find_configuration(const char *name)
{
    for (int i = 0; i < n_conf_list; i++) {
        if (strcmp(name, conf_list[i]->name) == 0) {
            return i;
        }
    }
    return -1;
}


void do_configuration_load()
{
    // find the configuration
    int i = find_configuration(configuration.content);
    if (i < 0) {
        return;  // configuration does not exist!
    }
    Configuration *conf = conf_list[i];

    // remove all service descriptors from list of fields
    // list starts after mqtt_port and ends at insert_after
    Field_entry *stop_at = insert_after->next;
    while (mqtt_port.next && mqtt_port.next != stop_at) {
        // splice Field_entry to be deleted out of the list
        Field_entry *next = mqtt_port.next;
        mqtt_port.next = next->next;
        // delete the Field_entry
        delete next;
    }
    insert_after = &mqtt_port;  // fix dangling pointer

    // transfer conf data to fields
    strcpy(ensemble_name.content, conf->ensemble);
    polling_rate.set_number(conf->polling_rate, "");
    strcpy(debug_flags.content, conf->debug_flags);
    strcpy(reference_clock.content, conf->reference_clock);
    strcpy(networking.content, net_options[conf->networking]);
    strcpy(websockets.content, conf->websockets ? "Enable" : "Disable");
    strcpy(mqtt_enable.content, conf->mqtt_enable ? "Enable" : "Disable");
    strcpy(mqtt_host.content, conf->mqtt_host ? conf->mqtt_host : "");
    mqtt_port.set_number(conf->mqtt_port, "");
    // create fields for added service descriptors
    for (Service_config *sc = conf->services; sc; sc = sc->next) {
        if (sc->marker == O2TOOSC_MARKER) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_o2_to_osc();
            fe = fe->next;  // service name
            strcpy(fe->content, sc->service_name);
            fe = fe->next;  // ip
            strcpy(fe->content, sc->ip);
            fe = fe->next;  // port
            if (sc->port == 0) {
                fe->content[0] = 0;
            } else {
                snprintf(fe->content, MAX_NAME_LEN, "%d", sc->port);
            }
            fe = fe->next;  // udp/tcp
            strcpy(fe->content, sc->tcp_flag ? "TCP" : "UDP");
        } else if (sc->marker == OSCTOO2_MARKER) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_osc_to_o2();
            fe = fe->next;  // udp/tcp
            strcpy(fe->content, sc->tcp_flag ? "TCP" : "UDP");
            fe = fe->next;  // port
            if (sc->port == 0) {
                fe->content[0] = 0;
            } else {
                snprintf(fe->content, MAX_NAME_LEN, "%d", sc->port);
            }
            fe = fe->next;  // service name
            strcpy(fe->content, sc->service_name);
        } else if (sc->marker == MIDIOUT_MARKER) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_o2_to_midi();
            fe = fe->next;  // service name
            strcpy(fe->content, sc->service_name);
            fe = fe->next;  // device name
            strcpy(fe->content, sc->midi_device);
        } else if (sc->marker == MIDIIN_MARKER) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_o2_to_midi();
            fe = fe->next;  // device name
            strcpy(fe->content, sc->midi_device);
            fe = fe->next;  // service name
            strcpy(fe->content, sc->service_name);
        }
    }
    draw_screen();
    // after a load, we'll set the current field to Ensemble name: since
    // positioning at the top (done by draw_screen) just puts us in the
    // Configuration: menu that we just used to choose what to Load. It
    // seems more useful to suggest editing the configuration by moving
    // to the first "content" field of the configuration form.
    set_current_field(&ensemble_name);
}


void do_configuration_delete()
{
    // find the configuration
    int i = find_configuration(configuration.content);
    if (i < 0) {
        return;  // configuration does not exist!
    }
    delete conf_list[i];
    // configuration_menu_options share strings with conf_list names
    configuration_menu_options[i] = NULL;
    // length of configuration_menu_options is n_conf_list + 1
    memmove(configuration_menu_options + i, configuration_menu_options + i + 1,
            sizeof(char *) * (n_conf_list - i));
    // length of conf_list is n_conf_list
    n_conf_list--;
    memmove(conf_list + i, conf_list + i + 1,
            sizeof(Configuration *) * (n_conf_list - i));
    // set configuration Field_entry to a valid name
    if (n_conf_list > 0) {
        strcpy(configuration.content, conf_list[0]->name);
    } else {
        configuration.content[0] = 0;
    }
    // fill in the fields and redisplay
    do_configuration_load();
}


// transfer from fields to Configuration object
// write Configuration objects to preference file
void do_configuration_save()
{
    char conf_name[MAX_NAME_LEN + 1];
    // are we saving to a new name?
    if (configuration_rename.content[0]) {  // yes
        if (n_conf_list >= CONF_LIST_MAX) {
            return;  // no more space for configurations
        }
        strcpy(conf_name, configuration_rename.content);
    } else if (configuration.content[0]) {  // name exists?
        strcpy(conf_name, configuration.content);
    }
    // find Configuration object if it exists
    int i = find_configuration(conf_name);
    Configuration *conf;
    if (i < 0) {  // configuration does not exist! make one
        i = n_conf_list++;
        conf = new Configuration();
        strcpy(conf->name, conf_name);
        conf_list[i] = conf;
        // update the menu
        configuration_menu_options[i] = conf->name;
        configuration_menu_options[n_conf_list] = NULL;
    } else {
        conf = conf_list[i];
        conf->free_storage();  // simpler to reconstruct it
    }
    strcpy(conf->ensemble, ensemble_name.content);
    conf->polling_rate = atoi(polling_rate.content);
    strcpy(conf->debug_flags, debug_flags.content);
    strcpy(conf->reference_clock, reference_clock.content);
    conf->networking = networking.current_option(0);
    conf->websockets = websockets.current_option(0);
    conf->mqtt_enable = mqtt_enable.current_option(0);
    strcpy(conf->mqtt_host, mqtt_host.content);
    conf->mqtt_port = (mqtt_port.content[0] ? atoi(mqtt_port.content) : 0);

    // for each set of fields representing an added service, create a
    // Service_config and append it to the services list.
    Service_config **sc_ptr = &conf->services;
    Field_entry *fe = mqtt_port.next;
    while (fe != &new_o2_to_osc) {
        Service_config *sc = new Service_config(fe->marker);
        *sc_ptr = sc;
        if (sc->marker == O2TOOSC_SERV_MARKER) {
            strcpy(sc->service_name, fe->content);
            strcpy(sc->ip, (fe = fe->next)->content);
            fe = fe->next;  // port
            sc->port = (fe->content[0] ? atoi(fe->content) : 0);
            sc->tcp_flag = (strcmp((fe = fe->next)->content, "TCP") == 0);
        } else if (sc->marker == OSCTOO2_UDP_MARKER) {
            sc->tcp_flag = (strcmp(fe->content, "TCP") == 0);
            fe = fe->next;  // port
            sc->port = (fe->content[0] ? atoi(fe->content) : 0);
            strcpy(sc->service_name, (fe = fe->next)->content);
        } else if (sc->marker == MIDIOUT_SERV_MARKER) {
            strcpy(sc->service_name, fe->content);
            strcpy(sc->midi_device, (fe = fe->next)->content);
        } else if (sc->marker == MIDIIN_NAME_MARKER) {
            strcpy(sc->midi_device, fe->content);
            strcpy(sc->service_name, (fe->next)->content);
        } else {
            assert(false);
        }
        fe = fe->next->next;  // skip delete_me and go to next service line
        sc_ptr = &sc->next;
    }

    // conf_name is what we saved, so we are now selecting conf_name
    strcpy(configuration.content, conf_name);
    configuration_rename.content[0] = 0;
    draw_screen();

    // write all configurations to the preference file
    FILE *outf = fopen(pref_path, "w");
    if (!outf) {
        return;
    }
    configuration.save(outf, "o2host v1.0\nConfiguration:", true);
    for (int i = 0; i < n_conf_list; i++) {
        conf_list[i]->save_to_pref(outf);
    }
    fclose(outf);
}


