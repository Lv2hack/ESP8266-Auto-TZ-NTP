//---------------------------------------------------------------------------------
//
//   NTP-Based Clock based on code from  https://steve.fi/Hardware/
//
//   This NTP clock connects to timezoneapi.io to retrieve the local timezone
//   Todo:
//
//      - Provide capability to override TZ if timezonapi.io is wrong using button
//
//   Alex T.

//
// WiFi & over the air updates
//
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

//
// For dealing with NTP & the clock.
//
#include "NTPClient.h"

//
// The display-interface
//
#include "TM1637.h"

//
// WiFi setup.
//
#include "WiFiManager.h"

//
// Debug messages over the serial console.
//
#include "debug.h"

//
// The name of this project.
//
// Used for:
//   Access-Point name, in config-mode
//   OTA name.
//
#define PROJECT_NAME "NTP-CLOCK"

const char* host = "timezoneapi.io";
const int httpsPort = 443;
//
// SHA1 fingerprint of the certificate at timezoneapi.io
const char* fingerprint = "86 57 E6 8B 53 7F 2C 9E B1 86 0E FC 3D C4 30 B7 AE 1A 4C 0C";

// Set the intensity - valid choices include:
//
//   BRIGHT_DARKEST   = 0
//   BRIGHT_TYPICAL   = 2
//   BRIGHT_BRIGHTEST = 7
    
// Brightness variables
// We're setting the brightness with a button connected to the BRT digital pin
//
int int_brightness = 2;
int brtLevel = 1;  // This will be 0, 1, 2 for low med high brightness
int brtButtonState = LOW;
int lastButtonState = LOW;

unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

//
// NTP client, and UDP socket it uses.
//
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

//
// Pin definitions for TM1637 and can be changed to other ports
//
#define CLK D3
#define DIO D2
#define BRT D1

TM1637 tm1637(CLK, DIO);

//
// Called just before the date/time is updated via NTP
//
void on_before_ntp()
{
    DEBUG_LOG("Updating date & time");
}

//
// Called just after the date/time is updated via NTP
//
void on_after_ntp()
{
    DEBUG_LOG("Updated NTP client");
}

//
// This function is called when the device is powered-on.
//
void setup()
{

    // Enable our serial port.
    Serial.begin(115200);
    
    tm1637.set(BRIGHT_TYPICAL);

    //
    // Handle WiFi setup
    //
    WiFiManager wifiManager;
    wifiManager.autoConnect(PROJECT_NAME);

    int timeZone = getInternetTimezone();
    
    Serial.print("Initial tz: ");
    Serial.println(timeZone);

    // initialize the display
    tm1637.init();

    // We want to see ":" between the digits.
    tm1637.point(true);

    //
    // Initialize pushbutton pin as an input
    pinMode(BRT, INPUT);
    
    //
    // Ensure our NTP-client is ready.
    //
    timeClient.begin();

    //
    // Configure the callbacks.
    //
    timeClient.on_before_update(on_before_ntp);
    timeClient.on_after_update(on_after_ntp);

    //
    // Setup the timezone & update-interval.
    //
    Serial.print("Setting time zone offset to timeZone: ");
    Serial.println(timeZone);
    timeClient.setTimeOffset(timeZone * (60 * 60));
    timeClient.setUpdateInterval(600 * 1000);  // This is the number of milliseconds so 300 * 1000 = 300 sec

    //
    // The final step is to allow over the air updates
    //
    // This is documented here:
    //     https://randomnerdtutorials.com/esp8266-ota-updates-with-arduino-ide-over-the-air/
    //
    // Hostname defaults to esp8266-[ChipID]
    //
    ArduinoOTA.setHostname(PROJECT_NAME);

    ArduinoOTA.onStart([]()
    {
        DEBUG_LOG("OTA Start");
    });
    ArduinoOTA.onEnd([]()
    {
        DEBUG_LOG("OTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        char buf[32];
        memset(buf, '\0', sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "Upgrade - %02u%%", (progress / (total / 100)));
        DEBUG_LOG(buf);
    });
    ArduinoOTA.onError([](ota_error_t error)
    {
        DEBUG_LOG("Error - ");

        if (error == OTA_AUTH_ERROR)
            DEBUG_LOG("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            DEBUG_LOG("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            DEBUG_LOG("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            DEBUG_LOG("Receive Failed");
        else if (error == OTA_END_ERROR)
            DEBUG_LOG("End Failed");
    });

    //
    // Ensure the OTA process is running & listening.
    //
    ArduinoOTA.begin();

}

//
// This function is called continously, and is responsible
// for flashing the ":", and otherwise updating the display.
//
// We rely on the background NTP-updates to actually make sure
// that that works.
//
void loop()
{
    static char prev[10] = { '\0' };
    static char buf[10] = { '\0' };
    static long last_read = 0;
    static bool flash = true;

    //
    // Resync the clock?
    //
    timeClient.update();

    //
    // Handle any pending over the air updates.
    //
    ArduinoOTA.handle();

    //
    // Get the current hour/min
    //
    int cur_hour = timeClient.getHours();
    int cur_min  = timeClient.getMinutes();
    int cur_sec  = timeClient.getSeconds();

    //
    // Format them in a useful way.
    //
    sprintf(buf, "%02d%02d", cur_hour, cur_min);

    //
    // If the current "hourmin" is different to
    // that we displayed last loop ..
    //
    if (strcmp(buf, prev) != 0)
    {           
        // Update the display
        tm1637.display(0, buf[0] - '0');
        tm1637.display(1, buf[1] - '0');
        tm1637.display(2, buf[2] - '0');
        tm1637.display(3, buf[3] - '0');

        // And cache it
        strcpy(prev , buf);
    }

    // Button debounce routine
    int btnReading = digitalRead(BRT);
    if (btnReading != lastButtonState) { lastDebounceTime = millis();  }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
       if (btnReading != brtButtonState) {
          brtButtonState = btnReading;

          // We've now detected a button state change (High or Low)
          // If HIGH then change the brightness 
          Serial.println("Button pushed");
          if (brtButtonState == HIGH) {
             brtLevel++;  // increment the brightness 
             Serial.print("brtLevel = ");
             Serial.println(brtLevel);
             if (brtLevel > 2) { brtLevel = 0; }  // if brightness goes past 2 then reset it to zero 
          }
          switch (brtLevel) {
          case 0:
             int_brightness = BRIGHT_DARKEST;
             break;
          case 1:
             int_brightness = BRIGHT_TYPICAL;
             break;
          case 2:
             int_brightness = BRIGHT_BRIGHTEST;
             break;
          }
          tm1637.set(int_brightness);   
          
          // Update the display
          tm1637.display(0, buf[0] - '0');
          tm1637.display(1, buf[1] - '0');
          tm1637.display(2, buf[2] - '0');
          tm1637.display(3, buf[3] - '0');
       }
    }
       
    lastButtonState = btnReading;

    
    // This is the flash the colon code
    long now = millis();
    if ((last_read == 0) || (abs(now - last_read) > 500))
    {
        // Invert the "show :" flag
        flash = !flash;

        // Apply it.
        tm1637.point(flash);

        //
        // Note that the ":" won't redraw unless/until you update.
        // So we'll force that to happen by removing the cached
        // value here.
        //
        memset(prev, '\0', sizeof(prev));
        last_read = now;
    }
    

}


int getInternetTimezone () {

  DynamicJsonBuffer jsonBuffer;
    
  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  Serial.print("connecting to ");
  Serial.println(host);
  if (!client.connect(host, httpsPort)) {
    Serial.println("connection failed");
    return -99;
  }

  if (client.verify(fingerprint, host)) {
    Serial.println("certificate matches");
  } else {
    Serial.println("certificate doesn't match");
  }

  String url = "/api/ip";
  Serial.print("requesting URL: ");
  Serial.println(url);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: NTP-Clock-ESP8266\r\n" +
               "Connection: close\r\n\r\n");

  Serial.println("request sent");
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }
  String line = client.readStringUntil('\n');
  
  // Parse the response "line" with JSON
  JsonObject& root = jsonBuffer.parseObject(line);

  //int timeZone = root[String("[\"meta\"][\"code\"]")];
  int timeZone = root["data"]["datetime"]["offset_hours"];
  Serial.print("offset_hours = ");
  Serial.println(timeZone);

  return timeZone;
}
