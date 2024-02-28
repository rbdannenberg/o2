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
// Polling_rate: <string>
// Debug_flags: <flags>
// Networking: <string>
// WebSockets: <string>
// MQTT_enable: <string>
// MQTT_host: <string>
// MQTT_port: <string>
// O2_to_OSC: <servicename> <IP> <port> UDP
// OSC_to_O2: UDP <port> <servicename>
// MIDI_in: <devicename> <servicename>
// MIDI_out: <servicename> <devicename>
// ----
// ---- <next configuration name>
// ...

#include "assert.h"
//#include "ctype.h"
//#include "string.h"
#include "sys/stat.h"
#include <unistd.h>
#include "fieldentry.h"
#include "o2host.h"
#include "configuration.h"

// reads chars between quotes ("). Stores up to n characters
// in str and zero terminates. Returns true on error, which
// occurs when EOF or newline is encountered before the 2nd quote.
bool read_quoted(FILE *inf, int n, char *str)
{
    int c;
    while ((c = fgetc(inf)) != EOF && c != '\n' && isspace(c)) ;
    if (c != '"') {
        ungetc(c, inf);
        return true;  // expected quote after space before newline
    }
    int i = 0;
    while ((c = fgetc(inf)) != EOF && (c != '\n')) {
        if (i < n) {
            str[i] = c;
        } else if (i == n) {
            str[i] = 0;
        }
        if (c == '"') {
            if (i < n) {
                str[i] = 0;
            } // otherwise EOS written at str[n]
            return false;
        }
        i++;
    }
    ungetc(c, inf);
    return true;
}


// read a quoted field value appearing after optional whitespace,
// return true on error
bool read_field(FILE *inf, const char *prefix, int max_width, char *field,
                bool newline)
{
    int c;
    // scan and match the prefix:
    while (*prefix) {
        if ((c = fgetc(inf)) != *prefix++) {
            ungetc(c, inf);
            return true;
        }
    }
    // get quoted string, skips over whitespace but not newline
    if (read_quoted(inf, max_width, field)) {
        return true;
    }

    // optionally get newline; allow whitespace before newline
    if (newline) {
        while ((c = fgetc(inf)) != EOF) {
            if (!isspace(c)) {
                ungetc(c, inf);
                return true;
            }
            if (c == '\n') {
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}


bool isdir(const char *path)
{
    struct stat buf;
    int s = stat(path, &buf);
    return (s == 0 && S_ISDIR(buf.st_mode));
}


int append_if_directory(char *path, char **home_dirs, int n)
{
    if (isdir(path)) {
        home_dirs[n++] = path;
    }
    return n;
}


// returns 1 if pref file read, 0 if no file found, -1 if error
int read_config()
{
    int pref_read_result = 1;  // assume file will be read
    char *home_dirs[10];
    char n_home_dirs = 0;
    char user_path[64];
    char home_path[64];
    char users_path[64];
    char *cwd = NULL;  // used by macOS and Linux
    pref_path[0] = 0;  // initialize to ""
#ifdef _WIN
#else
    // find Unix home directory possibilities. Start with HOME:
    home_dirs[0] = getenv("HOME");
    if (home_dirs[0]) {
        n_home_dirs++;
    }

    // Try /usr/$USER, /home/$USER, /Users/$USER:
    char *user = getenv("USER");  // warning: this could corrupt home_dirs[0]
    // but chances of env changing so quickly is low, so I'll ignore it
    strcpy(user_path, "/usr/");
    strcat(user_path, user);
    n_home_dirs = append_if_directory(user_path, home_dirs, n_home_dirs);

    strcpy(home_path, "/home/");
    strcat(home_path, user);
    n_home_dirs = append_if_directory(home_path, home_dirs, n_home_dirs);

    strcpy(users_path, "/Users/");
    strcat(users_path, user);
    n_home_dirs = append_if_directory(users_path, home_dirs, n_home_dirs);
     
    // Wild shot/last resort: Get /*/* from current working directory
    cwd = getcwd(NULL, 128);  // 128 is ignored says the man page
    if (cwd && strlen(cwd) > 2) {
        char *slash = strchr(cwd + 1, '/');  // second slash
        slash = (slash ? strchr(slash + 1, '/') : NULL);  // third slash
        if (slash) {
            *slash = 0;  // truncate cwd to 2 parts, e.g. /usr/rbd
            home_dirs[n_home_dirs++] = cwd;
        }
    }
    if (cwd) free(cwd);
#endif
#ifdef __MACH__
    // try to make valid path to preference file o2host.config by trying all possible
    // path in home_dirs looking for Library/Application Support/
    for (int i = 0; i < n_home_dirs; i++) {
        int len = (int) strlen(home_dirs[i]) + 50;
        if (len < 128) {
            strcpy(pref_path, home_dirs[i]);
            strcat(pref_path, "/Library/Application Support/o2host");
            if (!isdir(pref_path)) {
                mkdir(pref_path, 0777);
            }
            if (isdir(pref_path)) {  // success!
                strcat(pref_path, "/o2host.config");
                break;
            }
        }
    }
    // now pref_path is path to preference file
#endif
#ifdef LINUX
#endif
#ifdef _WIN32
#endif
    FILE *inf = fopen(pref_path, "r");
    if (!inf) {
        return 0;
    }
    // use user_path, home_path as temporaries
    if (fscanf(inf, "%63s %63s", user_path, home_path) != 2 ||
        strcmp(user_path, "o2host") != 0 ||
        strcmp(home_path, "v1.0") != 0) {
        return -1;
    }

    if (read_field(inf, "\nConfiguration:", CONF_W, configuration.content,
                   false)) {
        return -1;
    }

    Configuration *conf = NULL;
    char temp[MAX_NAME_LEN + 1];
    char tcp_udp[4];

    // this is a bit tricky: the loop invariant here is that we are at
    // the newline preceding a new configuration that begins with "----"
    // Initially this is true because we did not read the newline after
    // the configuration name (above). After each iteration, this is true
    // because we just read the final "----" but not the newline at the
    // end of the configuration. If read_field fails, it will nevertheless
    // read the final newline, so the next fgetc() will return EOF as
    // expected.
    while (read_field(inf, "\n----", CONF_W, temp, true) == false) {
        // now build the Configuration
        conf = new Configuration();
        strcpy(conf->name, temp);
        if (read_field(inf, "Ensemble_name:", MAX_NAME_LEN,
                       conf->ensemble, true)) {
            goto bad_file;
        }
        if (read_field(inf, "Polling_rate:", POLL_W, temp, true)) {
            goto bad_file;
        }
        conf->polling_rate = atoi(temp);

        if (read_field(inf, "Debug_flags:", MAX_NAME_LEN,
                       conf->debug_flags, true)) {
            goto bad_file;
        }
        if (read_field(inf, "Networking:", MAX_NAME_LEN, temp, true)) {
            goto bad_file;
        }
        conf->networking = string_list_index(net_options, temp, 0);

        if (read_field(inf, "WebSockets:", WEB_W, temp, true)) {
            goto bad_file;
        }
        conf->websockets = string_list_index(enable_options, temp, 0);

        if (read_field(inf, "MQTT_enable:", MQTTENAB_W, temp, true)) {
            goto bad_file;
        }
        conf->mqtt_enable = string_list_index(enable_options, temp, 0);

        if (read_field(inf, "MQTT_host:", MAX_NAME_LEN,
                       conf->mqtt_host, true)) {
            goto bad_file;
        }
        if (read_field(inf, "MQTT_port:", PORT_LEN, temp, true)) {
            goto bad_file;
        }
        conf->mqtt_port = atoi(temp);

        Service_config **ptr_to_next_service_config = &(conf->services);
        while (true) {
            Service_config *sc;
            temp[0] = 0;  // in case we don't read it with fscanf
            if (fscanf(inf, "%15s", temp) != 1) {
                break;  // done with extra services
            }
            if (strcmp(temp, "O2_to_OSC:") == 0) {
                sc = new Service_config(O2TOOSC_MARKER);
                if (read_field(inf, "", O2TOOSC_SERV_W, sc->service_name,
                               false) ||
                    read_field(inf, "", IP_LEN, sc->ip, false) ||
                    read_field(inf, "", PORT_LEN, temp, false) ||
                    read_field(inf, "", 3, tcp_udp, true)) {
                    delete sc;
                    goto bad_file;
                }
                sc->port = atoi(temp);
                sc->tcp_flag = (strcmp(tcp_udp, "TCP") == 0);
            } else if (strcmp(temp, "OSC_to_O2:") == 0) {
                sc = new Service_config(OSCTOO2_MARKER);
                if (read_field(inf, "", 3, tcp_udp, false) ||
                    read_field(inf, "", PORT_LEN, temp, false) ||
                    read_field(inf, "", OSCTOO2_SERV_W,
                               sc->service_name, true)) {
                    delete sc;
                    goto bad_file;
                }
                sc->port = atoi(temp);
                sc->tcp_flag = (strcmp(tcp_udp, "TCP") == 0);
            } else if (strcmp(temp, "MIDI_in:") == 0) {
                sc = new Service_config(MIDIIN_MARKER);
                if (read_field(inf, "", MIDIIN_W, sc->midi_device, false) ||
                    read_field(inf, "", MIDIIN_SERV_W,
                               sc->service_name, true)) {
                    delete sc;
                    goto bad_file;
                }
            } else if (strcmp(temp, "MIDI_out:") == 0) {
                sc = new Service_config(MIDIOUT_MARKER);
                if (read_field(inf, "", MIDIOUT_SERV_W,
                               sc->service_name, false) ||
                    read_field(inf, "", MIDIOUT_W, sc->midi_device, true)) {
                    delete sc;
                    goto bad_file;
                }
            } else if (strcmp(temp, "----") == 0) {
                break;  // found terminating string, done with extra services
            } else {
                goto bad_file;
            }
            *ptr_to_next_service_config = sc;
            ptr_to_next_service_config = &(sc->next);
        }
        // append conf to list (max of CONF_LIST_MAX)
        conf_list[n_conf_list] = conf;
        // make a list of names only for the multiple choice menu:
        configuration_menu_options[n_conf_list++] = conf->name;
        conf = NULL;
    }
    // if all is well, we are at EOF:
    if (fgetc(inf) != EOF) {
        goto bad_file;
    }
  finishup:
    fclose(inf);
    configuration_menu_options[n_conf_list] = NULL;  // terminate list
    return pref_read_result;
  bad_file:
    if (conf) {
        delete conf; // also removes list of Service_config
    }
    pref_read_result = -1;
    goto finishup;
}
