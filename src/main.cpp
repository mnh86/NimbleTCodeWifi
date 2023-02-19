#include <Arduino.h>
#include <BfButton.h>
#include <SPIFFS.h>
#include "nimbleConModule.h"

// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _WIFIMGR_LOGLEVEL_    4

#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiMulti.h>
WiFiMulti wifiMulti;

#define FileFS        SPIFFS
#define FS_Name       "SPIFFS"

// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM       true
#define FORMAT_FILESYSTEM         false

#define MIN_AP_PASSWORD_SIZE    8

#define SSID_MAX_LEN            32
//WPA2 passwords can be up to 63 characters long.
#define PASS_MAX_LEN            64

typedef struct
{
    char wifi_ssid[SSID_MAX_LEN];
    char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

typedef struct
{
    String wifi_ssid;
    String wifi_pw;
}  WiFi_Credentials_String;

#define NUM_WIFI_CREDENTIALS 2

typedef struct
{
    WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
    uint16_t checksum;
} WM_Config;

WM_Config WM_config;

#define CONFIG_FILENAME F("/wifi_cred.dat")

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false;

#if (_WIFIMGR_LOGLEVEL_ > 3)
    #warning Using DHCP IP
#endif

IPAddress stationIP   = IPAddress(0, 0, 0, 0);
IPAddress gatewayIP   = IPAddress(192, 168, 1, 1);
IPAddress netMask     = IPAddress(255, 255, 255, 0);

IPAddress APStaticIP  = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW  = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN  = IPAddress(255, 255, 255, 0);

#define USE_ESP_WIFIMANAGER_NTP false

// Must be placed before #include <ESP_WiFiManager.h>,
// or default port 80 will be used
//#define HTTP_PORT     8080

#include <ESP_WiFiManager.h> //https://github.com/khoih-prog/ESP_WiFiManager

// SSID and PW for Config Portal
String ssid = "Nimble_" + String(ESP_getChipId(), HEX);
const char* password = "nimblestroker";

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// Function Prototypes
uint8_t connectMultiWiFi();

WiFi_AP_IPConfig  WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;

void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig)
{
    in_WM_AP_IPconfig._ap_static_ip   = APStaticIP;
    in_WM_AP_IPconfig._ap_static_gw   = APStaticGW;
    in_WM_AP_IPconfig._ap_static_sn   = APStaticSN;
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig)
{
    in_WM_STA_IPconfig._sta_static_ip   = stationIP;
    in_WM_STA_IPconfig._sta_static_gw   = gatewayIP;
    in_WM_STA_IPconfig._sta_static_sn   = netMask;
}

void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
    LOGERROR3(F("stationIP ="), in_WM_STA_IPconfig._sta_static_ip, ", gatewayIP =", in_WM_STA_IPconfig._sta_static_gw);
    LOGERROR1(F("netMask ="), in_WM_STA_IPconfig._sta_static_sn);
}

void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn);
}

uint8_t connectMultiWiFi()
{
// For ESP32 core v1.0.6, must be >= 500
#define WIFI_MULTI_1ST_CONNECT_WAITING_MS   800L
#define WIFI_MULTI_CONNECT_WAITING_MS       500L

    uint8_t status;

    //WiFi.mode(WIFI_STA);

    LOGERROR(F("ConnectMultiWiFi with :"));

    if ( (Router_SSID != "") && (Router_Pass != "") )
    {
        LOGERROR3(F("* Flash-stored Router_SSID = "), Router_SSID, F(", Router_Pass = "), Router_Pass );
        LOGERROR3(F("* Add SSID = "), Router_SSID, F(", PW = "), Router_Pass );
        wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
    }

    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
        // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
        if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
        {
            LOGERROR3(F("* Additional SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
        }
    }

    LOGERROR(F("Connecting MultiWifi..."));

    //WiFi.mode(WIFI_STA);
    configWiFi(WM_STA_IPconfig);

    int i = 0;
    status = wifiMulti.run();
    delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

    while ( ( i++ < 20 ) && ( status != WL_CONNECTED ) )
    {
        status = WiFi.status();
        if ( status == WL_CONNECTED )
            break;
        else
            delay(WIFI_MULTI_CONNECT_WAITING_MS);
    }

    if ( status == WL_CONNECTED )
    {
        LOGERROR1(F("WiFi connected after time: "), i);
        LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
        LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
    }
    else
    {
        LOGERROR(F("WiFi not connected"));
        ESP.restart();
    }

    return status;
}

void heartBeatPrint()
{
    static int num = 1;

    if (WiFi.status() == WL_CONNECTED)
        Serial.print(F("H"));        // H means connected to WiFi
    else
        Serial.print(F("F"));        // F means not connected to WiFi

    if (num == 80)
    {
        Serial.println();
        num = 1;
    }
    else if (num++ % 10 == 0)
    {
        Serial.print(F(" "));
    }
}

void check_WiFi()
{
    if ( (WiFi.status() != WL_CONNECTED) )
    {
        Serial.println(F("\nWiFi lost. Call connectMultiWiFi in loop"));
        connectMultiWiFi();
    }
}

void check_status()
{
    static ulong checkstatus_timeout  = 0;
    static ulong checkwifi_timeout    = 0;
    static ulong current_millis;

#define WIFICHECK_INTERVAL    1000L
#define HEARTBEAT_INTERVAL    10000L

    current_millis = millis();

    // Check WiFi every WIFICHECK_INTERVAL (1) seconds.
    if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
    {
        check_WiFi();
        checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
    }

    // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
    if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0))
    {
        heartBeatPrint();
        checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
    }
}

int calcChecksum(uint8_t* address, uint16_t sizeToCalc)
{
    uint16_t checkSum = 0;
    for (uint16_t index = 0; index < sizeToCalc; index++)
    {
        checkSum += * ( ( (byte*) address ) + index);
    }
    return checkSum;
}

bool loadConfigData()
{
    File file = FileFS.open(CONFIG_FILENAME, "r");
    LOGERROR(F("LoadWiFiCfgFile "));

    memset((void *) &WM_config,       0, sizeof(WM_config));
    memset((void *) &WM_STA_IPconfig, 0, sizeof(WM_STA_IPconfig));

    if (file)
    {
        file.readBytes((char *) &WM_config,   sizeof(WM_config));
        file.readBytes((char *) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
        file.close();
        LOGERROR(F("OK"));

        if ( WM_config.checksum != calcChecksum( (uint8_t*) &WM_config, sizeof(WM_config) - sizeof(WM_config.checksum) ) )
        {
            LOGERROR(F("WM_config checksum wrong"));
            return false;
        }
        displayIPConfigStruct(WM_STA_IPconfig);
        return true;
    }
    else
    {
        LOGERROR(F("failed"));
        return false;
    }
}

void saveConfigData()
{
    File file = FileFS.open(CONFIG_FILENAME, "w");
    LOGERROR(F("SaveWiFiCfgFile "));

    if (file)
    {
        WM_config.checksum = calcChecksum( (uint8_t*) &WM_config, sizeof(WM_config) - sizeof(WM_config.checksum) );
        file.write((uint8_t*) &WM_config, sizeof(WM_config));
        displayIPConfigStruct(WM_STA_IPconfig);
        file.write((uint8_t*) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
        file.close();
        LOGERROR(F("OK"));
    }
    else
    {
        LOGERROR(F("failed"));
    }
}

bool createAccessPoint(ESP_WiFiManager &ESP_wifiManager, bool isSetup = false)
{
    bool configDataLoaded = false;

    ESP_wifiManager.setDebugOutput(true);

    // Use only to erase stored WiFi Credentials
    // if (isSetup) {
    //     resetSettings();
    //     ESP_wifiManager.resetSettings();
    // }

    ESP_wifiManager.setMinimumSignalQuality(-1);
    // Set config portal channel, default = 1. Use 0 => random channel from 1-13
    ESP_wifiManager.setConfigPortalChannel(0);

    Router_SSID = ESP_wifiManager.WiFi_SSID();
    Router_Pass = ESP_wifiManager.WiFi_Pass();
    Serial.println("ESP Self-Stored: SSID = " + Router_SSID);

    if ( (Router_SSID != "") && (Router_Pass != "") )
    {
        LOGERROR3(F("* Add SSID = "), Router_SSID, F(", PW = "), Router_Pass);
        wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());

        ESP_wifiManager.setConfigPortalTimeout(120); //If no access point name has been previously entered disable timeout.
        Serial.println(F("Got ESP Self-Stored Credentials. Timeout 120s for Config Portal"));
    }
    else if (loadConfigData())
    {
        configDataLoaded = true;

        ESP_wifiManager.setConfigPortalTimeout(120); //If no access point name has been previously entered disable timeout.
        Serial.println(F("Got stored Credentials. Timeout 120s for Config Portal"));
        initialConfig = true;
    }
    else
    {
        // Enter CP only if no stored SSID on flash and file
        Serial.println(F("Open Config Portal without Timeout: No stored Credentials."));
        initialConfig = true;
    }

    if (!isSetup || initialConfig)
    {
        Serial.print(F("Starting configuration portal @ "));
        Serial.print(F("192.168.4.1"));
        Serial.print(F(", SSID = "));
        Serial.print(ssid);
        Serial.print(F(", PWD = "));
        Serial.println(password);

        //ledcWrite(WIFI_LED, 50); // turn ON the LED to tell us we are in configuration mode.

#if DISPLAY_STORED_CREDENTIALS_IN_CP
        // New. Update Credentials, got from loadConfigData(), to display on CP
        ESP_wifiManager.setCredentials(WM_config.WiFi_Creds[0].wifi_ssid, WM_config.WiFi_Creds[0].wifi_pw,
                                       WM_config.WiFi_Creds[1].wifi_ssid, WM_config.WiFi_Creds[1].wifi_pw);
#endif

        // Starts an access point
        if (!ESP_wifiManager.startConfigPortal((const char *) ssid.c_str(), password))
            Serial.println(F("Not connected to WiFi but continuing anyway."));
        else
        {
            Serial.println(F("WiFi connected!"));
            Serial.print(F("Local IP: "));
            Serial.println(WiFi.localIP());
        }

        if (isSetup || String(ESP_wifiManager.getSSID(0)) != "" && String(ESP_wifiManager.getSSID(1)) != "")
        {
            // Stored  for later usage, but clear first
            memset(&WM_config, 0, sizeof(WM_config));

            for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
            {
                String tempSSID = ESP_wifiManager.getSSID(i);
                String tempPW   = ESP_wifiManager.getPW(i);

                if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
                    strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
                else
                    strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);

                if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
                    strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
                else
                    strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);

                // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
                if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
                {
                    LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
                    wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
                }
            }

            ESP_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
            saveConfigData();
        }
    }

    //ledcWrite(WIFI_LED, 0); // turn OFF the LED to tell us we are NOT in configuration mode.

    return configDataLoaded;
}

/////////////////////////////////////////

BfButton btn(BfButton::STANDALONE_DIGITAL, ENC_BUTT, true, LOW);

void pressHandler(BfButton *btn, BfButton::press_pattern_t pattern)
{
    switch (pattern)
    {
    case BfButton::DOUBLE_PRESS:
        break;
    case BfButton::LONG_PRESS: // Reset Wifi
        ESP_WiFiManager ESP_wifiManager("NimbleTCodeWebsocket");
        createAccessPoint(ESP_wifiManager);
        break;
    }
}

void setup() {
    initNimbleConModule();
    while (!Serial);
    Serial.setDebugOutput(true);
    delay(200);

    Serial.print(F("\nStarting NimbleTCodeWebsocket using ")); Serial.print(FS_Name);
    Serial.print(F(" on ")); Serial.println(ARDUINO_BOARD);
    Serial.println(ESP_WIFIMANAGER_VERSION);

    if (FORMAT_FILESYSTEM)
    {
        Serial.println(F("Forced Formatting."));
        FileFS.format();
    }
    if (!FileFS.begin(true))
    {
        Serial.println(F("SPIFFS failed! Already tried formatting."));
        if (!FileFS.begin())
        {
            // prevents debug info from the library to hide err message.
            delay(100);
            Serial.println(F("A fatal error has occurred while mounting SPIFFS. Stopped."));
            while (true)
            {
                delay(1);
            }
        }
    }

    unsigned long startedAt = millis();
    initAPIPConfigStruct(WM_AP_IPconfig);
    initSTAIPConfigStruct(WM_STA_IPconfig);

    ssid.toUpperCase();
    ESP_WiFiManager ESP_wifiManager("NimbleTCodeWebsocket");
    bool configDataLoaded = createAccessPoint(ESP_wifiManager, true);

    if (!initialConfig)
    {
        // Load stored data, the addAP ready for MultiWiFi reconnection
        if (!configDataLoaded)
            loadConfigData();

        for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
        {
            // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
            if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
            {
                LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
                wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
            }
        }

        if ( WiFi.status() != WL_CONNECTED )
        {
            Serial.println(F("ConnectMultiWiFi in setup"));
            connectMultiWiFi();
        }
    }

    Serial.print(F("After waiting "));
    Serial.print((float) (millis() - startedAt) / 1000);
    Serial.print(F(" secs more in setup(), connection result is "));
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print(F("connected. Local IP: "));
        Serial.println(WiFi.localIP());
    }
    else
        Serial.println(ESP_wifiManager.getStatus(WiFi.status()));

    btn.onDoublePress(pressHandler)
        .onPressFor(pressHandler, 2000);
}

void loop() {
  btn.read();
  check_status();
}