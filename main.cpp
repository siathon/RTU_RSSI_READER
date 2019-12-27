#include "main.h"

void connect(){
    serial.attach(callback(&ser, &SerialHandler::rx));
    while (true) {
        Watchdog::get_instance().kick();
        if (sim800.checkSim() != 0) {
            sim800.start();
            continue;
        }
        Watchdog::get_instance().kick();
        if (sim800.setGPRSSettings() != 0) {
            continue;
        }
        MQTT.beginTCPConnection();
        serial.attach(callback(&tcp, &PacketHandler::rx));
        Watchdog::get_instance().kick();
        MQTT.connect(1, 0, 0, 0, (char*)"", (char*)"");
        Watchdog::get_instance().kick();
        break;
    }
}

void onPacket(char* packet) {
    MQTT.parsePacket(packet);
}

void onMessage(int channel, string msgID, string value){
    pc.printf("%d - %s - %s\n", channel, msgID.c_str(), value.c_str());
}

void sub(){
    sprintf(MQTT.Topic, "v1/devices/me/attributes/response/+");
    MQTT.subscribe(0, MQTT._generateMessageID(), MQTT.Topic, 0);
}

void check(){
    Watchdog::get_instance().kick();
    if (MQTT.publishFailCount > 2 || MQTT.pingFailCount > 1) {
        connect();
    }
}

int main() {
    const reset_reason_t reason = ResetReason::get();
    pc.printf("Last system reset reason: %s\r\n", reset_reason_to_string(reason).c_str());
    watchdog.start();
    connect();
    Watchdog::get_instance().kick();
    nrf.attach(&nrf_rx);
    ev_queue.call_in(3000, sub);
    ev_queue.call_every(10, callback(&tcp, &PacketHandler::checkForPacket));
    ev_queue.call_every(1000, callback(&MQTT, &GSM_MQTT::keepAlive));
    ev_queue.call_every(1000, update);
    // ev_queue.call_every(1000, pub);
    ev_queue.call_every(20000, check);
    ev_queue.dispatch_forever();
    return 0;
}
