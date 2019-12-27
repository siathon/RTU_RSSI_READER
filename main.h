#include "mbed.h"
#include <string>
#include <list>
#include <math.h>
#include <algorithm>
#include "SerialHandler.h"
#include "PacketHandler.h"
#include "SIM800.h"
#include "GSM_MQTT.h"

using namespace std;
std::string reset_reason_to_string(const reset_reason_t reason){
    switch (reason) {
        case RESET_REASON_POWER_ON:
            return "Power On";
        case RESET_REASON_PIN_RESET:
            return "Hardware Pin";
        case RESET_REASON_SOFTWARE:
            return "Software Reset";
        case RESET_REASON_WATCHDOG:
            return "Watchdog";
        default:
            return "Other Reason";
    }
}

char* user = (char*)"DqpEEVYSdXFXdGXjtr0E";
char* pass = NULL;
char* clID = NULL;
char* host = (char*)"things.saymantechcloud.ir";
char* port = (char*)"1883";

Watchdog &watchdog = Watchdog::get_instance();
EventQueue ev_queue(100 * EVENTS_EVENT_SIZE);
RawSerial serial(PA_9, PA_10, 9600);
RawSerial pc(PA_2, PA_3, 9600);
RawSerial nrf(PC_10, PC_11, 9600);
SerialHandler ser(0);
GSM_MQTT MQTT(user, pass, clID, host, port, 60);
PacketHandler tcp(0);
SIM800 sim800(PC_9, PB_7);
DigitalOut led(PC_8, 0);
DigitalOut led_net(PC_4, 0);
DigitalOut led_scan(PC_6, 0);

char nrf_in_data[100];
uint8_t serial_index = 0;

const int authenticate_low_rssi = -56;
const int authenticate_high_rssi = -50;
const int presence_low_rssi = -80;
const int presence_high_rssi = -70;

struct device_t {
    string name;
    string mac;
    int rssi = 0;
    bool new_data;
    bool authenticate;
    bool presence;
    bool state_changed;
    bool in_range;
    int no_data = 0;
};
const int known_device_count = 1;
string known_devices[known_device_count] = {"HRM1"};

list<device_t> devices;

int stringToInt(string str){
    int indx = str.find('.');
    if (indx != -1) {
        str = str.substr(0, indx);
    }
    int neg = 1;
    if (str.find('-') != -1) {
        neg = -1;
        str = str.substr(1);
    }

    double t = 0;
    int l = str.length();
    for(int i = l-1; i >= 0; i--)
        t += (str[i] - '0') * pow(10.0, l - i - 1);
    return (int)(neg * t);
}
void print_device(device_t device){
    pc.printf("name: %s\tmac: %s\trssi: %d\n", device.name.c_str(), device.mac.c_str(), device.rssi);
}

void parse_data(char* data){
    string s = string(data);
    int idx1 = s.find(",");
    string mac = s.substr(0, idx1);
    int idx2 = s.find(",", idx1 + 1);
    string name = s.substr(idx1+1, idx2-idx1-1);
    bool device_found = false;
    for (size_t i = 0; i < known_device_count; i++) {
        if (name.compare(known_devices[i]) == 0) {
            device_found = true;
            break;
        }
    }
    if (!device_found) {
        return;
    }
    int rssi = stringToInt(s.substr(idx2+1));
    list<device_t>::iterator it;
    device_found = false;
    for (it=devices.begin(); it != devices.end(); it++) {
        if (it->name.compare(name) == 0) {
            it->rssi = rssi;
            it->new_data = true;
            print_device(*it);
            device_found = true;
            led_scan = 1;
            wait_us(50000);
            led_scan = 0;
        }
    }
    if (!device_found) {
        struct device_t device;
        device.name = name;
        device.mac = mac;
        device.rssi = rssi;
        device.authenticate = false;
        device.presence = false;
        device.in_range = false;
        device.new_data = true;
        // print_device(device);
        devices.push_back(device);

    }
    Watchdog::get_instance().kick();
}

void nrf_rx(){
    char c = nrf.getc();
    if (c == '\n') {
        nrf_in_data[serial_index] = '\0';
        char data[100];
        sprintf(data, "%s", nrf_in_data);
        ev_queue.call(parse_data, data);
        serial_index = 0;
        return;
    }
    nrf_in_data[serial_index] = c;
    serial_index++;
}

string bool_to_str(bool b){
    if (b) {
        return "true";
    } else {
        return "false";
    }
}

void pub(device_t d){
    MQTT.publishFailCount++;
    sprintf(MQTT.Topic, "v1/devices/me/attributes");
    sprintf(MQTT.Message, "{\"authenticate\":%s, \"presence\":%s, \"rssi\":%d}", bool_to_str(d.authenticate).c_str(),
                                                                                   bool_to_str(d.presence).c_str(),
                                                                                   d.rssi);
    pc.printf("publish: %s\n%s\n", MQTT.Topic, MQTT.Message);
    MQTT.publish(0, 1, 0, MQTT._generateMessageID(), MQTT.Topic, MQTT.Message);
}

void update(){
    if (MQTT.MQTT_Flag) {
        led_net = 1;
    }
    else{
        led_net = 0;
    }
    Watchdog::get_instance().kick();
    list<device_t>::iterator it;
    for (it=devices.begin(); it != devices.end(); it++) {
        if(it->new_data){

            it->no_data=0;
            it->in_range = true;
            int rssi = it->rssi;
            if(rssi > authenticate_high_rssi) {
                if (!it->authenticate) {
                    it->state_changed = true;
                }
                it->authenticate = true;
            }
            else if(rssi > authenticate_low_rssi){

            }
            else{
                if (it->authenticate) {
                    it->state_changed = true;
                }
                it->authenticate = false;
            }
            if(rssi > presence_high_rssi) {
                if (!it->presence) {
                    it->state_changed = true;
                }
                it->presence = true;
            }
            else if(rssi > presence_low_rssi){

            }
            else{
                if (it->presence) {
                    it->state_changed = true;
                }
                it->presence = false;
            }
            it->new_data = false;
        }
        else{
            it->no_data++;
            if (it->no_data > 15) {
                it->in_range = false;
                it->rssi = 99;
                it->no_data = 0;
                if (it->authenticate || it->presence) {
                    it->presence = false;
                    it->authenticate = false;
                    it->state_changed = true;
                }
            }
        }
        if (it->state_changed) {
            it->state_changed = false;
            pub(*it);
        }
    }
}
