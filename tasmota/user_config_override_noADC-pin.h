/*
  04dec19   - add USE_ADC_VCC, on for sw013
  25nov2019 - 1st AP changed to pafo_SSID5 (Fritz/test) , 2nd set to pafo_SSID4 (home)
  user_config_override.h - user configuration overrides user_config.h for Sonoff-Tasmota

  Copyright (C) 2018  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

//#define FIRMWARE_MINIMAL                         // Create tasmota-minimal as intermediate firmware for OTA-MAGIC


#ifndef _USER_CONFIG_OVERRIDE_H_
#define _USER_CONFIG_OVERRIDE_H_

// force the compiler to show a warning to confirm that this file is inlcuded
#warning **** user_config_override.h: Using Settings from this File ****

/*****************************************************************************************************\
 * USAGE:
 *   To modify the stock configuration without changing the user_config.h file:
 *   (1) copy this file to "user_config_override.h" (It will be ignored by Git)
 *   (2) define your own settings below
 *   (3) for platformio:
 *         define USE_CONFIG_OVERRIDE as a build flags.
 *         ie1 : export PLATFORMIO_BUILD_FLAGS='-DUSE_CONFIG_OVERRIDE'
 *         ie2 : enable in file platformio.ini "build_flags = -Wl,-Tesp8266.flash.1m0.ld -DUSE_CONFIG_OVERRIDE"
 *       for Arduino IDE:
 *         enable define USE_CONFIG_OVERRIDE in user_config.h
 ******************************************************************************************************
 * ATTENTION:
 *   - Changes to SECTION1 PARAMETER defines will only override flash settings if you change define CFG_HOLDER.
 *   - Expect compiler warnings when no ifdef/undef/endif sequence is used.
 *   - You still need to update user_config.h for major define USE_MQTT_TLS.
 *   - All parameters can be persistent changed online using commands via MQTT, WebConsole or Serial.
\*****************************************************************************************************/

// -- Internal Analog input -----------------------
#undef  USE_ADC_VCC
#define USE_ADC_VCC                              // Display Vcc in Power status. Disable for use as Analog input on selected devices


// Overrides  - added as of 30nov19 for ptrooms who continues to use the sonoff name
#undef  PROJECT
#define PROJECT                "sonoff-sw077"         // PROJECT is used as the default topic delimiter

#undef  WIFI_HOSTNAME
#define WIFI_HOSTNAME		   "%s"     // [hostname] Expands to <MQTT_TOPIC> (-<last 4 decimal chars of MAC address>)
// const char WIFI_HOSTNAME[] = "%s-%04d";     // Expands to <MQTT_TOPIC>-<last 4 decimal chars of MAC address>

#undef  MQTT_GRPTOPIC
#define MQTT_GRPTOPIC          "sonoffs"

#undef  CFG_HOLDER
#define CFG_HOLDER             4617


// -- Wifi ----------------------------------------
#undef  STA_SSID1
#define STA_SSID1              "Pafo SSID4"      		// [Ssid1] Wifi SSID
#undef  STA_PASS1
#define STA_PASS1              "a1234567890bccb9876543210a"      // [Password1] Wifi password
#undef  STA_SSID2
#define STA_SSID2              "Pafo SSID5" 	     		// [Ssid2] Optional alternate AP Wifi SSID
#undef  STA_PASS2
#define STA_PASS2              "a1234567890bccb9876543210a"      // [Password2] Optional alternate AP Wifi password

// -- Location ------------------------------------ Almere 52.3508� N, 5.2647� E
// -- Location ------------------------------------ Amsterdam 52.379189� N, 4.899431� E
#undef  LATITUDE
#define LATITUDE               52.379189        // [Latitude] Your location to be used with sunrise and sunset
#undef  LONGITUDE
#define LONGITUDE              4.899431         // [Longitude] Your location to be used with sunrise and sunset


// -- Syslog --------------------------------------
#undef  SYS_LOG_HOST
#define SYS_LOG_HOST           "192.168.1.8"                // [LogHost] (Linux) syslog host

// -- MQTT ----------------------------------------
#undef  MQTT_HOST
#define MQTT_HOST              "192.168.1.8"    // [MqttHost]
#undef  MQTT_USER
#define MQTT_USER              "ptromqqt"       // [MqttUser] MQTT user
#undef  MQTT_PASS
#define MQTT_PASS              "ptromqqt"       // [MqttPassword] MQTT password

// -- Time - Up to three NTP servers in your region
#undef  NTP_SERVER1
#define NTP_SERVER1            "ntp.xs4all.nl"      // [NtpServer1] Select first NTP server by name or IP address (129.250.35.250)

/*
	Examples :
	
	// -- Master parameter control --------------------
	#undef  CFG_HOLDER
	#define CFG_HOLDER        0x20161209             // [Reset 1] Change this value to load SECTION1 configuration parameters to flash
	
	// -- Setup your own Wifi settings  ---------------
	#undef  STA_SSID1
	#define STA_SSID1         "YourSSID"             // [Ssid1] Wifi SSID
	
	#undef  STA_PASS1
	#define STA_PASS1         "YourWifiPassword"     // [Password1] Wifi password
	
	// -- Setup your own MQTT settings  ---------------
	#undef  MQTT_HOST
	#define MQTT_HOST         "your-mqtt-server.com" // [MqttHost]
	
	#undef  MQTT_PORT
	#define MQTT_PORT         1883                   // [MqttPort] MQTT port (10123 on CloudMQTT)
	
	#undef  MQTT_USER
	#define MQTT_USER         "YourMqttUser"         // [MqttUser] Optional user
	
	#undef  MQTT_PASS
	#define MQTT_PASS         "YourMqttPass"         // [MqttPassword] Optional password
	
	// You might even pass some parameters from the command line ----------------------------
	// Ie:  export PLATFORMIO_BUILD_FLAGS='-DUSE_CONFIG_OVERRIDE -DMY_IP="192.168.1.99" -DMY_GW="192.168.1.1" -DMY_DNS="192.168.1.1"'
	
	#ifdef MY_IP
	#undef  WIFI_IP_ADDRESS
	#define WIFI_IP_ADDRESS   MY_IP                  // Set to 0.0.0.0 for using DHCP or IP address
	#endif
	
	#ifdef MY_GW
	#undef  WIFI_GATEWAY
	#define WIFI_GATEWAY      MY_GW                  // if not using DHCP set Gateway IP address
	#endif
	
	#ifdef MY_DNS
	#undef  WIFI_DNS
	#define WIFI_DNS          MY_DNS                 // If not using DHCP set DNS IP address (might be equal to WIFI_GATEWAY)
	#endif

*/


#endif  // _USER_CONFIG_OVERRIDE_H_
