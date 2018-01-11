/* AFE Firmware for smart home devices
  LICENSE: https://github.com/tschaban/AFE-Firmware/blob/master/LICENSE
  DOC: http://smart-house.adrian.czabanowski.com/afe-firmware-pl/ */

#include "AFE-MQTT.h"
#include <AFE-Data-Access.h>
#include <AFE-Data-Structures.h>
#include <AFE-Device.h>
#include <AFE-LED.h>
#include <AFE-PIR.h>
#include <AFE-Relay.h>
#include <AFE-Switch.h>
#include <AFE-Upgrader.h>
#include <AFE-Web-Server.h>
#include <AFE-WiFi.h>
#include <Streaming.h>

AFEDataAccess Data;
AFEDevice Device;
AFEWiFi Network;
AFEMQTT Mqtt;
AFEWebServer WebServer;
AFELED Led;
AFESwitch Switch[5];
AFESwitch ExternalSwitch;
AFERelay Relay[4];
MQTT MQTTConfiguration;
AFEPIR Pir[4];

void setup() {

  Serial.begin(115200);
  delay(10);

  /* Turn off publishing information to Serial */
  //  Serial.swap();

  /* Checking if the device is launched for a first time. If so it sets up
   * the device (EEPROM) */
  if (Device.isFirstTimeLaunch()) {
    Device.setDevice();
  }

  /* Perform post upgrade changes (if any) */
  AFEUpgrader Upgrader;
  if (Upgrader.upgraded()) {
    Upgrader.upgrade();
  }
  Upgrader = {};

  /* Checking if WiFi is onfigured, if not than it runs access point mode */
  if (Device.getMode() != MODE_ACCESS_POINT && !Device.isConfigured()) {
    Device.reboot(MODE_ACCESS_POINT);
  }

  /* Initializing relay and setting it's default state at power on*/
  if (Device.getMode() == MODE_NORMAL) {
    for (uint8_t i = 0; i < 4; i++) {
      if (Device.configuration.isRelay[i]) {
        Relay[i].begin(i);
        Relay[i].setRelayAfterRestoringPower();
      }
    }
  }

  /* Initialzing network */
  Network.begin(Device.getMode());

  /* Initializing LED, checking if LED exists is made on Class level  */
  Led.begin(0);

  /* If device in configuration mode then start LED blinking */
  if (Device.getMode() != MODE_NORMAL) {
    Led.blinkingOn(100);
  }

  /* Initializing Switches */
  for (uint8_t i = 0; i < 5; i++) {
    if (Device.configuration.isSwitch[i]) {
      Switch[i].begin(i);
    }
  }

  /* Initializing PIRs */
  for (uint8_t i = 0; i < 4; i++) {
    if (Device.configuration.isPIR[i]) {
      Pir[i].begin(i);
    }
  }

  /* Initializing MQTT */
  if (Device.getMode() != MODE_ACCESS_POINT && Device.configuration.mqttAPI) {
    MQTTConfiguration = Data.getMQTTConfiguration();
    Mqtt.begin();
  }

  Network.connect();

  /* Initializing HTTP WebServer */
  WebServer.handle("/", handleHTTPRequests);
  WebServer.handle("/favicon.ico", handleFavicon);
  WebServer.begin();
}

void loop() {

  if (Device.getMode() != MODE_ACCESS_POINT) {
    if (Network.connected()) {
      if (Device.getMode() == MODE_NORMAL) {
        /* Connect to MQTT if not connected */
        if (Device.configuration.mqttAPI) {
          Mqtt.connected() ? Mqtt.loop() : Mqtt.connect();
        }
        WebServer.listener();

        /* Checking if there was received HTTP API Command */
        if (Device.configuration.httpAPI) {
          if (WebServer.httpAPIlistener()) {
            Led.on();
            processHTTPAPIRequest(WebServer.getHTTPCommand());
            Led.off();
          }
        }

        /* Switch related code */
        for (uint8_t i = 0; i < 5; i++) {
          if (Device.configuration.isSwitch[i]) {
            /* One of the switches has been shortly pressed */
            if (Switch[i].isPressed() && Switch[i].getFunctionality() != 0) {
              Led.on();
              Relay[Switch[i].getFunctionality() - 11 + i].toggle();
              MQTTPublishRelayState(Switch[i].getFunctionality() - 11 +
                                    i); // MQTT Listener library
              Led.off();
            }
          }
        }

        for (uint8_t i = 0; i < 4; i++) {
          if (Device.configuration.isPIR[i]) {
            if (Pir[i].stateChanged()) {
              Led.on();
              //    MQTTPublishPIRState(i);
              if (Pir[i].Configuration.relayId != 9 &&
                  Pir[i].get() == PIR_OPEN) { // 9 is none
                Serial << endl << "seting time and turning it on";
                Relay[i].setTimer(Pir[i].Configuration.howLongKeepRelayOn);
                Relay[Pir[i].Configuration.relayId].on();
              }
              Led.off();
            }
          }
        }

        for (uint8_t i = 0; i < 4; i++) {
          if (Device.configuration.isRelay[i]) {
            if (Relay[i].autoTurnOff()) {
              Led.on();
              Mqtt.publish(Relay[i].getMQTTTopic(), "state", "off");
              Relay[i].clearTimer();
              Led.off();
            }
          }
        }

      } else { // Configuration Mode
        WebServer.listener();
      }
    } else { // Device not connected to WiFi. Reestablish connection
      Network.connect();
    }
  } else { // Access Point Mode
    Network.APListener();
    WebServer.listener();
  }

  /* Listens for switch events */
  for (uint8_t i = 0; i < 5; i++) {
    if (Device.configuration.isSwitch[i]) {
      Switch[i].listener();
    }
  }

  for (uint8_t i = 0; i < 5; i++) {
    if (Device.configuration.isSwitch[i]) {
      /* One of the Multifunction switches pressed for 10 seconds */
      if (Switch[i].getFunctionality() == SWITCH_MULTI) {
        if (Switch[i].is10s()) {
          Device.getMode() == MODE_NORMAL ? Device.reboot(MODE_ACCESS_POINT)
                                          : Device.reboot(MODE_NORMAL);
        } else if (Switch[i].is5s()) {
          Device.getMode() == MODE_NORMAL ? Device.reboot(MODE_CONFIGURATION)
                                          : Device.reboot(MODE_NORMAL);
        }
      }
    }
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (Device.configuration.isPIR[i]) {
      Pir[i].listener();
    }
  }

  Led.loop();
}
