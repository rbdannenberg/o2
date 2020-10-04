// o2litedisc.ino -- this sketch gets as far as clock sync
// with o2litehost.c running on another computer.
//
// Roger B. Dannenberg
// Aug 2020

#include "src/o2lite.h"

// WiFi network name and password:
#define NETWORK_NAME "Aylesboro"
#define NETWORK_PSWD "4125214147"
#define HOSTNAME "esp32"

void setup() {
    Serial.begin(115200);
    printf("This is o2litedisc\n");
    connect_to_wifi(HOSTNAME, NETWORK_NAME, NETWORK_PSWD);
    o2l_initialize("test");
}

void loop() {
    // put your main code here, to run repeatedly:
    o2l_poll();
}
