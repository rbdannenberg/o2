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
#include <stdlib.h>
#include "fieldentry.h"
#include "configuration.h"
#include "o2host.h"
#include "o2oscservice.h"


Service_config::Service_config(Service_config_marker marker_) {
    marker = marker_;
    service_name[0] = 0;
    ip[0] = 0;
    port = 0;
    tcp_flag = false;
    midi_device[0] = 0;
    next = NULL;
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
    http_port = 0;
    http_root[0] = 0;
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
    fprintf(outf, "HTTP_port: \"%s\"\n", port_as_string(http_port));
    fprintf(outf, "HTTP_root: \"%s\"\n", http_root);
    fprintf(outf, "MQTT_host: \"%s\"\n", mqtt_host);
    fprintf(outf, "MQTT_port: \"%s\"\n", port_as_string(mqtt_port));
    Service_config *sc = services;
    while (sc) {
        if (sc->marker == O2TOOSC_CONFIG) {
            fprintf(outf, "O2_to_OSC: \"%s\" \"%s\" \"%s\" \"%s\"\n",
                    sc->service_name, sc->ip, port_as_string(sc->port),
                    sc->tcp_flag ? "TCP" : "UDP");
        } else if (sc->marker == OSCTOO2_CONFIG) {
            fprintf(outf, "OSC_to_O2: \"%s\" \"%s\" \"%s\"\n",
                    sc->tcp_flag ? "TCP" : "UDP", port_as_string(sc->port),
                    sc->service_name);
        } else if (sc->marker == MIDIOUT_CONFIG) {
            fprintf(outf, "MIDI_out: \"%s\" \"%s\"\n",
                    sc->service_name, sc->midi_device);
        } else if (sc->marker == MIDIIN_CONFIG) {
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


void remove_service_descriptors()
{
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
    // if we had used delete_or_insert, then lines would be adjusted,
    // but since we wanted to delete all service fields, it seems simpler
    // to just set all the fields to fixed initial positions:
    reset_lower_field_positions();
}


void do_configuration_load()
{
    // find the configuration
    int i = find_configuration(configuration.content);
    if (i < 0) {
        print_error("Configuration does not exist.");
        return;  // configuration does not exist!
    }
    Configuration *conf = conf_list[i];

    remove_service_descriptors();

    // transfer conf data to fields
    ensemble_name.set_content(conf->ensemble);
    polling_rate.set_number(conf->polling_rate, "");
    debug_flags.set_content(conf->debug_flags);
    reference_clock.set_content(conf->reference_clock);
    networking.set_content(net_options[conf->networking]);
    http_port.set_number(conf->http_port, "");
    http_root.set_content(conf->http_root ? conf->http_root : "");
    mqtt_host.set_content(conf->mqtt_host ? conf->mqtt_host : "");
    mqtt_port.set_number(conf->mqtt_port, "");
    // create fields for added service descriptors
    for (Service_config *sc = conf->services; sc; sc = sc->next) {
        if (sc->marker == O2TOOSC_CONFIG) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_o2_to_osc();
            fe = fe->next;  // service name
            fe->set_content(sc->service_name);
            fe = fe->next;  // ip
            fe->set_content(sc->ip);
            fe = fe->next;  // port
            fe->set_number(sc->port, "");
            fe = fe->next;  // udp/tcp
            fe->set_content(sc->tcp_flag ? "TCP" : "UDP");
        } else if (sc->marker == OSCTOO2_CONFIG) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_osc_to_o2();
            fe = fe->next;  // udp/tcp
            fe->set_content(sc->tcp_flag ? "TCP" : "UDP");
            fe = fe->next;  // port
            fe->set_number(sc->port, "");
            fe = fe->next;  // service name
            fe->set_content(sc->service_name);
        } else if (sc->marker == MIDIOUT_CONFIG) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_o2_to_midi();
            fe = fe->next;  // service name
            fe->set_content(sc->service_name);
            fe = fe->next;  // device name
            fe->set_content(sc->midi_device);
        } else if (sc->marker == MIDIIN_CONFIG) {
            Field_entry *fe = insert_after;  // remember link to new fields
            insert_midi_to_o2();
            fe = fe->next;  // device name
            fe->set_content(sc->midi_device);
            fe = fe->next;  // service name
            fe->set_content(sc->service_name);
        } else {
            print_error("Unexpected Service_config tag.");
        }
    }
    redraw_requested = true;
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
        print_error("Configuration does not exist.");
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
            print_error("No more space for configurations.");
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
    conf->http_port = (http_port.content[0] ? atoi(http_port.content) : 0);
    strcpy(conf->http_root, http_root.content);
    strcpy(conf->mqtt_host, mqtt_host.content);
    conf->mqtt_port = (mqtt_port.content[0] ? atoi(mqtt_port.content) : 0);

    // for each set of fields representing an added service, create a
    // Service_config and append it to the services list.
    Service_config **sc_ptr = &conf->services;
    Field_entry *fe = mqtt_port.next;
    while (fe != &new_o2_to_osc) {  // new_o2_to_osc is first field after
                                    // the added services
        Service_config_marker m = (Service_config_marker)
                ((int) fe->marker + 100);
        Service_config *sc = new Service_config(m);
        *sc_ptr = sc;
        if (m == O2TOOSC_CONFIG) {
            strcpy(sc->service_name, fe->content);
            strcpy(sc->ip, (fe = fe->next)->content);
            fe = fe->next;  // port
            sc->port = (fe->content[0] ? atoi(fe->content) : 0);
            fe = fe->next;  // TCP flag
            sc->tcp_flag = (strcmp(fe->content, "TCP") == 0);
        } else if (m == OSCTOO2_CONFIG) {
            sc->tcp_flag = (strcmp(fe->content, "TCP") == 0);
            fe = fe->next;  // port
            sc->port = (fe->content[0] ? atoi(fe->content) : 0);
            fe = fe->next;  // service name
            strcpy(sc->service_name, fe->content);
        } else if (m == MIDIOUT_CONFIG) {
            strcpy(sc->service_name, fe->content);
            fe = fe->next;
            strcpy(sc->midi_device, fe->content);
        } else if (m == MIDIIN_CONFIG) {
            strcpy(sc->midi_device, fe->content);
            fe = fe->next; // to service name
            strcpy(sc->service_name, fe->content);
        } else {
            assert(false);
        }
        fe = fe->next->next;  // skip delete_me and go to next service line
        sc_ptr = &sc->next;
    }

    // conf_name is what we saved, so we are now selecting conf_name
    strcpy(configuration.content, conf_name);
    configuration_rename.set_content("");
    redraw_requested = true;

    // write all configurations to the preference file

    char temp_path[128];
    strcpy(temp_path, pref_path);
    strcat(temp_path, ".tmp");  // in case of failure
    FILE *outf = fopen(temp_path, "w");
    if (!outf) {
        print_error("Could not open preference file to save configurations.");
        return;
    }
    configuration.save(outf, "o2host v1.0\nConfiguration:", true);
    for (int i = 0; i < n_conf_list; i++) {
        conf_list[i]->save_to_pref(outf);
    }
    fclose(outf);
#ifdef WIN32
    _unlink(pref_path);  // this will fail if file does not exist, but that's ok
#endif
    if (rename(temp_path, pref_path)) {
        print_error("Could not rename temp file to preference file.");
    };
}


// transfer from fields to Configuration object
// write Configuration objects to preference file
void do_configuration_new()
{
    char conf_name[MAX_NAME_LEN + 1];
    if (n_conf_list >= CONF_LIST_MAX) {
        print_error("No more space for configurations.");
        return;  // no more space for configurations
    }
    if (configuration_rename.width == 0) {
        print_error("Must have a name for the new configuration.");
        return;
    }
    // remove extra fields from list to restore initial emptiness:
    remove_service_descriptors();

    configuration.set_content(configuration_rename.content);
    ensemble_name.set_content("");
    polling_rate.set_content("");
    debug_flags.set_content("");
    reference_clock.set_content("");
    networking.set_option(0);
    http_port.set_content("");
    http_root.set_content("");
    mqtt_host.set_content("");
    mqtt_port.set_content("");
    redraw_requested = true;
}


