/*
  xsns_05_ds18x20.ino - DS18x20 temperature sensor support for Tasmota

  Copyright (C) 2021  Theo Arends and md5sum-as (https://github.com/md5sum-as)

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

#ifdef ESP8266
#ifdef USE_DS18x20
/*********************************************************************************************\
 * DS18B20 - Temperature - Multiple sensors
\*********************************************************************************************/

#define XSNS_05              5

//#define USE_DS18x20_RECONFIGURE    // When sensor is lost keep retrying or re-configure
//#define DS18x20_USE_ID_AS_NAME     // Use last 3 bytes for naming of sensors

/* #define DS18x20_USE_ID_ALIAS in my_user_config.h or user_config_override.h
  * Use alias for fixed sensor name in scripts by autoexec. Command: DS18Alias XXXXXXXXXXXXXXXX,N where XXXXXXXXXXXXXXXX full serial and N number 1-255
  * Result in JSON:  "DS18Sens_2":{"Id":"000003287CD8","Temperature":26.3} (example with N=2)
  * Setting N to an alphanumeric value, the complete name is replaced with it
  * Result in JSON:  "Outside1":{"Id":"000003287CD8","Temperature":26.3} (example with N=Outside1)
*/

#define DS18S20_CHIPID       0x10     // +/-0.5C 9-bit
#define DS1822_CHIPID        0x22     // +/-2C 12-bit
#define DS18B20_CHIPID       0x28     // +/-0.5C 12-bit
#define M1601_CHIPID         0x28 +1  // +/-0.1C 16-bit (M1601B = +/-0.5C 16-bit)
#define MAX31850_CHIPID      0x3B     // +/-0.25C 14-bit

#define W1_SKIP_ROM          0xCC
#define W1_CONVERT_TEMP      0x44
#define W1_WRITE_EEPROM      0x48
#define W1_WRITE_SCRATCHPAD  0x4E
#define W1_READ_SCRATCHPAD   0xBE

#ifndef DS18X20_MAX_SENSORS           // DS18X20_MAX_SENSORS fallback to 8 if not defined in user_config_override.h
#define DS18X20_MAX_SENSORS  8
#endif

#define DS18X20_ALIAS_LEN    17

//#define DS18X20_DEBUG

const char kDs18x20Types[] PROGMEM = "DS18x20|DS18S20|DS1822|DS18B20|MAX31850|M1601";

uint8_t ds18x20_chipids[] = { 0, DS18S20_CHIPID, DS1822_CHIPID, DS18B20_CHIPID, MAX31850_CHIPID, M1601_CHIPID };

struct {
  float temperature;
  float temp_sum;
  uint16_t numread;
  uint8_t address[8];
  uint8_t chip_id;
  uint8_t index;
  uint8_t valid;
  int8_t pins_id;
#ifdef DS18x20_USE_ID_ALIAS
  char *alias = (char*)calloc(DS18X20_ALIAS_LEN, 1);
#endif  // DS18x20_USE_ID_ALIAS
} ds18x20_sensor[DS18X20_MAX_SENSORS];

struct {
  int8_t pin = 0;            // Shelly GPIO3 input only
  int8_t pin_out = 0;        // Shelly GPIO00 output only
  bool dual_mode = false;    // Single pin mode
} ds18x20_gpios[MAX_DSB];

struct {
#ifdef W1_PARASITE_POWER
  uint32_t w1_power_until = 0;
  uint8_t current_sensor = 0;
#endif
  char name[17];
  uint8_t sensors;
  uint8_t gpios;             // Count of GPIO found
  uint8_t input_mode = 0;    // INPUT or INPUT_PULLUP (=2)
  int8_t pin = 0;            // Shelly GPIO3 input only
  int8_t pin_out = 0;        // Shelly GPIO00 output only
  bool dual_mode = false;    // Single pin mode
} DS18X20Data;

/*********************************************************************************************\
 * Embedded tuned OneWire library
\*********************************************************************************************/

#define W1_MATCH_ROM         0x55
#define W1_SEARCH_ROM        0xF0

uint8_t onewire_last_discrepancy = 0;
uint8_t onewire_last_family_discrepancy = 0;
bool onewire_last_device_flag = false;
unsigned char onewire_rom_id[8] = { 0 };

/*------------------------------------------------------------------------------------------*/

uint8_t OneWireReset(void) {
  uint8_t retries = 125;

  if (!DS18X20Data.dual_mode) {
    pinMode(DS18X20Data.pin, DS18X20Data.input_mode);
    do {
      if (--retries == 0) {
        return 0;
      }
      delayMicroseconds(2);
    } while (!digitalRead(DS18X20Data.pin));
    pinMode(DS18X20Data.pin, OUTPUT);
    digitalWrite(DS18X20Data.pin, LOW);
    delayMicroseconds(480);
    pinMode(DS18X20Data.pin, DS18X20Data.input_mode);
    delayMicroseconds(70);
    uint8_t r = !digitalRead(DS18X20Data.pin);
    delayMicroseconds(410);
    return r;
  } else {
    digitalWrite(DS18X20Data.pin_out, HIGH);
    do {
      if (--retries == 0) {
        return 0;
      }
      delayMicroseconds(2);
    } while (!digitalRead(DS18X20Data.pin));
    digitalWrite(DS18X20Data.pin_out, LOW);
    delayMicroseconds(480);
    digitalWrite(DS18X20Data.pin_out, HIGH);
    delayMicroseconds(70);
    uint8_t r = !digitalRead(DS18X20Data.pin);
    delayMicroseconds(410);
    return r;
  }
}

void OneWireWriteBit(uint8_t v) {
  static const uint8_t delay_low[2] = { 65, 10 };
  static const uint8_t delay_high[2] = { 5, 55 };

  v &= 1;
  if (!DS18X20Data.dual_mode) {
    digitalWrite(DS18X20Data.pin, LOW);
    pinMode(DS18X20Data.pin, OUTPUT);
    delayMicroseconds(delay_low[v]);
    digitalWrite(DS18X20Data.pin, HIGH);
  } else {
    digitalWrite(DS18X20Data.pin_out, LOW);
    delayMicroseconds(delay_low[v]);
    digitalWrite(DS18X20Data.pin_out, HIGH);
  }
  delayMicroseconds(delay_high[v]);
}

uint8_t OneWire1ReadBit(void) {
  pinMode(DS18X20Data.pin, OUTPUT);
  digitalWrite(DS18X20Data.pin, LOW);
  delayMicroseconds(3);
  pinMode(DS18X20Data.pin, DS18X20Data.input_mode);
  delayMicroseconds(10);
  uint8_t r = digitalRead(DS18X20Data.pin);
  delayMicroseconds(53);
  return r;
}

uint8_t OneWire2ReadBit(void) {
  digitalWrite(DS18X20Data.pin_out, LOW);
  delayMicroseconds(3);
  digitalWrite(DS18X20Data.pin_out, HIGH);
  delayMicroseconds(10);
  uint8_t r = digitalRead(DS18X20Data.pin);
  delayMicroseconds(53);
  return r;
}

/*------------------------------------------------------------------------------------------*/

void OneWireWrite(uint8_t v) {
  for (uint8_t bit_mask = 0x01; bit_mask; bit_mask <<= 1) {
    OneWireWriteBit((bit_mask & v) ? 1 : 0);
  }
}

uint8_t OneWireRead(void) {
  uint8_t r = 0;

  if (!DS18X20Data.dual_mode) {
    for (uint8_t bit_mask = 0x01; bit_mask; bit_mask <<= 1) {
      if (OneWire1ReadBit()) {
        r |= bit_mask;
      }
    }
  } else {
    for (uint8_t bit_mask = 0x01; bit_mask; bit_mask <<= 1) {
      if (OneWire2ReadBit()) {
        r |= bit_mask;
      }
    }
  }
  return r;
}

void OneWireSelect(const uint8_t rom[8]) {
  OneWireWrite(W1_MATCH_ROM);
  for (uint32_t i = 0; i < 8; i++) {
    OneWireWrite(rom[i]);
  }
}

uint8_t OneWireSearch(uint8_t *newAddr) {
  uint8_t id_bit_number = 1;
  uint8_t last_zero = 0;
  uint8_t rom_byte_number = 0;
  uint8_t search_result = 0;
  uint8_t id_bit;
  uint8_t cmp_id_bit;
  unsigned char rom_byte_mask = 1;
  unsigned char search_direction;

  if (!onewire_last_device_flag) {
    if (!OneWireReset()) {
      onewire_last_discrepancy = 0;
      onewire_last_device_flag = false;
      onewire_last_family_discrepancy = 0;
#ifdef DS18X20_DEBUG
      AddLog(LOG_LEVEL_DEBUG, PSTR("DSB: OneWireReset fail"));
#endif  // DS18X20_DEBUG
      return false;
    }
    OneWireWrite(W1_SEARCH_ROM);
    do {
      if (!DS18X20Data.dual_mode) {
        id_bit     = OneWire1ReadBit();
        cmp_id_bit = OneWire1ReadBit();
      } else {
        id_bit     = OneWire2ReadBit();
        cmp_id_bit = OneWire2ReadBit();
      }
      if ((id_bit == 1) && (cmp_id_bit == 1)) {
        break;
      } else {
        if (id_bit != cmp_id_bit) {
          search_direction = id_bit;
        } else {
          if (id_bit_number < onewire_last_discrepancy) {
            search_direction = ((onewire_rom_id[rom_byte_number] & rom_byte_mask) > 0);
          } else {
            search_direction = (id_bit_number == onewire_last_discrepancy);
          }
          if (search_direction == 0) {
            last_zero = id_bit_number;
            if (last_zero < 9) {
              onewire_last_family_discrepancy = last_zero;
            }
          }
        }
        if (search_direction == 1) {
          onewire_rom_id[rom_byte_number] |= rom_byte_mask;
        } else {
          onewire_rom_id[rom_byte_number] &= ~rom_byte_mask;
        }
        OneWireWriteBit(search_direction);
        id_bit_number++;
        rom_byte_mask <<= 1;
        if (rom_byte_mask == 0) {
          rom_byte_number++;
          rom_byte_mask = 1;
        }
      }
    } while (rom_byte_number < 8);
    if (!(id_bit_number < 65)) {
      onewire_last_discrepancy = last_zero;
      if (onewire_last_discrepancy == 0) {
        onewire_last_device_flag = true;
      }
      search_result = true;
    }
#ifdef DS18X20_DEBUG
    AddLog(LOG_LEVEL_DEBUG, PSTR("DSB: OneWireSearch result %d, bits %d, %8_H"), search_result, id_bit_number, onewire_rom_id);
#endif  // DS18X20_DEBUG
  }
  if (!search_result || !onewire_rom_id[0]) {
    onewire_last_discrepancy = 0;
    onewire_last_device_flag = false;
    onewire_last_family_discrepancy = 0;
    search_result = false;
  }
  for (uint32_t i = 0; i < 8; i++) {
    newAddr[i] = onewire_rom_id[i];
  }
  return search_result;
}

bool OneWireCrc8(uint8_t *addr, uint8_t len) {
  uint8_t crc = 0;

  while (len--) {
    uint8_t inbyte = *addr++;
    for (uint32_t i = 8; i; i--) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) {
        crc ^= 0x8C;
      }
      inbyte >>= 1;
    }
  }
  return (crc == *addr);
}

/********************************************************************************************/

void Ds18x20Init(void) {
  DS18X20Data.gpios = 0;
  for (uint32_t pins = 0; pins < MAX_DSB; pins++) {
    if (PinUsed(GPIO_DSB, pins)) {
      ds18x20_gpios[pins].pin = Pin(GPIO_DSB, pins);

      if (PinUsed(GPIO_DSB_OUT, pins)) {
        ds18x20_gpios[pins].dual_mode = true;
        ds18x20_gpios[pins].pin_out = Pin(GPIO_DSB_OUT, pins);
      }
      DS18X20Data.gpios++;
    }
  }

  uint64_t ids[DS18X20_MAX_SENSORS];
  DS18X20Data.sensors = 0;
  DS18X20Data.input_mode = Settings->flag3.ds18x20_internal_pullup ? INPUT_PULLUP : INPUT;  // SetOption74 - Enable internal pullup for single DS18x20 sensor

  for (uint32_t pins = 0; pins < DS18X20Data.gpios; pins++) {
    DS18X20Data.pin = ds18x20_gpios[pins].pin;
    DS18X20Data.dual_mode = ds18x20_gpios[pins].dual_mode;
    if (ds18x20_gpios[pins].dual_mode) {
      DS18X20Data.pin_out = ds18x20_gpios[pins].pin_out;
      pinMode(DS18X20Data.pin_out, OUTPUT);
      pinMode(DS18X20Data.pin, DS18X20Data.input_mode);
    }

    onewire_last_discrepancy = 0;
    onewire_last_device_flag = false;
    onewire_last_family_discrepancy = 0;
    for (uint32_t i = 0; i < 8; i++) {
      onewire_rom_id[i] = 0;
    }

    while (DS18X20Data.sensors < DS18X20_MAX_SENSORS) {
      if (!OneWireSearch(ds18x20_sensor[DS18X20Data.sensors].address)) {
#ifdef DS18X20_DEBUG
        AddLog(LOG_LEVEL_DEBUG, PSTR("DSB: OneWireSearch fail"));
#endif  // DS18X20_DEBUG
        break;
      }

      uint32_t chip_id = ds18x20_sensor[DS18X20Data.sensors].address[0];
      bool crc = OneWireCrc8(ds18x20_sensor[DS18X20Data.sensors].address, 7);
      if (!crc) {
        // Look for M1601 which has same chip_id as DS18B20 but has wrong CRC over 64-bit ROM code
        // DS18B20 address 284CC48E04000079
        //   M1601 address 2894020000000000
        if ((ds18x20_sensor[DS18X20Data.sensors].address[0] == DS18B20_CHIPID) &&
            (ds18x20_sensor[DS18X20Data.sensors].address[7] == 0)) {
          chip_id = M1601_CHIPID;  // Need different chip_id as different temperature calculation
          crc = true;
        }
      }
      if (crc &&
         ((chip_id == DS18S20_CHIPID) ||
          (chip_id == DS1822_CHIPID) ||
          (chip_id == DS18B20_CHIPID) ||
          (chip_id == M1601_CHIPID) ||
          (chip_id == MAX31850_CHIPID))) {
        ds18x20_sensor[DS18X20Data.sensors].index = DS18X20Data.sensors;
        ids[DS18X20Data.sensors] = chip_id;  // Chip id
        for (uint32_t j = 6; j > 0; j--) {
          ids[DS18X20Data.sensors] = ids[DS18X20Data.sensors] << 8 | ds18x20_sensor[DS18X20Data.sensors].address[j];
        }
#ifdef DS18x20_USE_ID_ALIAS
        ds18x20_sensor[DS18X20Data.sensors].alias[0] = '0';
#endif
        ds18x20_sensor[DS18X20Data.sensors].chip_id = chip_id;
        ds18x20_sensor[DS18X20Data.sensors].pins_id = pins;
        DS18X20Data.sensors++;
      }
#ifdef DS18X20_DEBUG
      else {
        AddLog(LOG_LEVEL_DEBUG, PSTR("DSB: Ds18x20Init CRC fail"));
      }
#endif  // DS18X20_DEBUG
    }
  }

  for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
    for (uint32_t j = i + 1; j < DS18X20Data.sensors; j++) {
      if (ids[ds18x20_sensor[i].index] > ids[ds18x20_sensor[j].index]) {  // Sort ascending
        std::swap(ds18x20_sensor[i].index, ds18x20_sensor[j].index);
      }
    }
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DSB D_SENSORS_FOUND " %d"), DS18X20Data.sensors);
}

void Ds18x20Convert(void) {
  for (uint8_t i = 0; i < DS18X20Data.gpios; i++) {
    DS18X20Data.pin = ds18x20_gpios[i].pin;
    DS18X20Data.dual_mode = ds18x20_gpios[i].dual_mode;
    DS18X20Data.pin_out = ds18x20_gpios[i].pin_out;
    OneWireReset();
#ifdef W1_PARASITE_POWER
    // With parasite power address one sensor at a time
    if (++DS18X20Data.current_sensor >= DS18X20Data.sensors)
      DS18X20Data.current_sensor = 0;
    OneWireSelect(ds18x20_sensor[DS18X20Data.current_sensor].address);
#else
    OneWireWrite(W1_SKIP_ROM);           // Address all Sensors on Bus
#endif
    OneWireWrite(W1_CONVERT_TEMP);       // start conversion, no parasite power on at the end
//    delay(750);                          // 750ms should be enough for 12bit conv
  }
}

bool Ds18x20Read(uint8_t sensor) {
  float temperature;
  uint8_t data[9];
  int8_t sign = 1;

  uint8_t index = ds18x20_sensor[sensor].index;
  DS18X20Data.pin = ds18x20_gpios[ds18x20_sensor[index].pins_id].pin;
  DS18X20Data.pin_out = ds18x20_gpios[ds18x20_sensor[index].pins_id].pin_out;
  DS18X20Data.dual_mode = ds18x20_gpios[ds18x20_sensor[index].pins_id].dual_mode;
  if (ds18x20_sensor[index].valid) { ds18x20_sensor[index].valid--; }
  for (uint32_t retry = 0; retry < 3; retry++) {
    OneWireReset();
    OneWireSelect(ds18x20_sensor[index].address);
    OneWireWrite(W1_READ_SCRATCHPAD);
    for (uint32_t i = 0; i < 9; i++) {
      data[i] = OneWireRead();
    }
#ifdef DS18X20_DEBUG
    AddLog(LOG_LEVEL_DEBUG, PSTR("DSB: OneWireRead ChipId 0x%02X, %9_H"), ds18x20_sensor[index].chip_id, data);
#endif  // DS18X20_DEBUG
    if (OneWireCrc8(data, 8)) {
      switch(ds18x20_sensor[index].chip_id) {
        case DS18S20_CHIPID: {
          int16_t tempS = (((data[1] << 8) | (data[0] & 0xFE)) << 3) | ((0x10 - data[6]) & 0x0F);
          temperature = ConvertTemp(tempS * 0.0625f - 0.250f);
          break;
        }
        case DS1822_CHIPID:
        case DS18B20_CHIPID: {
          // 71 01 4B 46 7F FF 0F 10 56
          if (data[4] != 0x7F) {
            data[4] = 0x7F;                 // Set resolution to 12-bit
            OneWireReset();
            OneWireSelect(ds18x20_sensor[index].address);
            OneWireWrite(W1_WRITE_SCRATCHPAD);
            OneWireWrite(data[2]);          // Th Register
            OneWireWrite(data[3]);          // Tl Register
            OneWireWrite(data[4]);          // Configuration Register
            OneWireSelect(ds18x20_sensor[index].address);
            OneWireWrite(W1_WRITE_EEPROM);  // Save scratchpad to EEPROM
#ifdef W1_PARASITE_POWER
            DS18X20Data.w1_power_until = millis() + 10; // 10ms specified duration for EEPROM write
#endif
          }
          uint16_t temp12 = (data[1] << 8) + data[0];
          if (temp12 > 2047) {
            temp12 = (~temp12) +1;
            sign = -1;
          }
          temperature = ConvertTemp(sign * temp12 * 0.0625f);  // Divide by 16
          break;
        }
        case M1601_CHIPID: {
          // 96 F1 00 80 55 05 02 09 86
          float temp = (int16_t)(data[1] << 8) + data[0];
          temperature = ConvertTemp(40 + (temp / 256));
          break;
        }
        case MAX31850_CHIPID: {
          int16_t temp14 = (data[1] << 8) + (data[0] & 0xFC);
          temperature = ConvertTemp(temp14 * 0.0625f);  // Divide by 16
          break;
        }
      }
      ds18x20_sensor[index].temperature = temperature;
      if (Settings->flag5.ds18x20_mean) {
        if (ds18x20_sensor[index].numread++ == 0) {
          ds18x20_sensor[index].temp_sum = 0;
        }
        ds18x20_sensor[index].temp_sum += temperature;
      }
      ds18x20_sensor[index].valid = SENSOR_MAX_MISS;
      return true;
    }
  }
  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DSB D_SENSOR_CRC_ERROR));
  return false;
}

void Ds18x20Name(uint8_t sensor) {
  uint32_t sensor_index = ds18x20_sensor[sensor].index;

  uint32_t index = sizeof(ds18x20_chipids);
  while (--index) {
    if (ds18x20_sensor[sensor_index].chip_id == ds18x20_chipids[index]) {
      break;
    }
  }
  // DS18B20
  GetTextIndexed(DS18X20Data.name, sizeof(DS18X20Data.name), index, kDs18x20Types);

#ifdef DS18x20_USE_ID_AS_NAME
  char address[17];
  for (uint32_t j = 0; j < 3; j++) {
    sprintf(address+2*j, "%02X", ds18x20_sensor[sensor_index].address[3-j]);  // Only last 3 bytes
  }
  // DS18B20-8EC44C
  snprintf_P(DS18X20Data.name, sizeof(DS18X20Data.name), PSTR("%s%c%s"), DS18X20Data.name, IndexSeparator(), address);
  return;
#elif defined(DS18x20_USE_ID_ALIAS)
  if (ds18x20_sensor[sensor_index].alias[0] && (ds18x20_sensor[sensor_index].alias[0] != '0')) {
    if (isdigit(ds18x20_sensor[sensor_index].alias[0])) {
      // DS18Sens-1
      snprintf_P(DS18X20Data.name, sizeof(DS18X20Data.name), PSTR("DS18Sens%c%d"), IndexSeparator(), atoi(ds18x20_sensor[sensor_index].alias));
    } else {
      // UserText
      snprintf_P(DS18X20Data.name, sizeof(DS18X20Data.name), PSTR("%s"), ds18x20_sensor[sensor_index].alias);
    }
    return;
  }
#endif  // DS18x20_USE_ID_AS_NAME or DS18x20_USE_ID_ALIAS

  if (DS18X20Data.sensors > 1) {
    // DS18B20-1
    snprintf_P(DS18X20Data.name, sizeof(DS18X20Data.name), PSTR("%s%c%d"), DS18X20Data.name, IndexSeparator(), sensor + 1);
  }
}

/********************************************************************************************/

void Ds18x20EverySecond(void) {
  if (!DS18X20Data.sensors) { return; }

#ifdef W1_PARASITE_POWER
  // skip access if there is still an eeprom write ongoing
  unsigned long now = millis();
  if (now < DS18X20Data.w1_power_until) { return; }
#endif
  if (TasmotaGlobal.uptime & 1
#ifdef W1_PARASITE_POWER
      // if more than 1 sensor and only parasite power: convert every cycle
      || DS18X20Data.sensors >= 2
#endif
  ) {
    // 2mS
    Ds18x20Convert();          // Start conversion, takes up to one second
  } else {
    for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
      // 12mS per device
      if (!Ds18x20Read(i)) {   // Read temperature
        Ds18x20Name(i);
        AddLogMissed(DS18X20Data.name, ds18x20_sensor[ds18x20_sensor[i].index].valid);
#ifdef USE_DS18x20_RECONFIGURE
        if (!ds18x20_sensor[ds18x20_sensor[i].index].valid) {
          memset(&ds18x20_sensor, 0, sizeof(ds18x20_sensor));
          Ds18x20Init();       // Re-configure
        }
#endif  // USE_DS18x20_RECONFIGURE
      }
    }
  }
}

void Ds18x20Show(bool json) {
  for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
    uint8_t index = ds18x20_sensor[i].index;

    if (ds18x20_sensor[index].valid) {   // Check for valid temperature
      Ds18x20Name(i);

      if (json) {
        if (Settings->flag5.ds18x20_mean) {
          if ((0 == TasmotaGlobal.tele_period) && ds18x20_sensor[index].numread) {
            ds18x20_sensor[index].temperature = ds18x20_sensor[index].temp_sum / ds18x20_sensor[index].numread;
            ds18x20_sensor[index].numread = 0;
          }
        }
        char address[17];
        for (uint32_t j = 0; j < 6; j++) {
          sprintf(address+2*j, "%02X", ds18x20_sensor[index].address[6-j]);  // Skip sensor type and crc
        }
        ResponseAppend_P(PSTR(",\"%s\":{\"" D_JSON_ID "\":\"%s\",\"" D_JSON_TEMPERATURE "\":%*_f}"),
          DS18X20Data.name, address, Settings->flag2.temperature_resolution, &ds18x20_sensor[index].temperature);
#ifdef USE_DOMOTICZ
        if ((0 == TasmotaGlobal.tele_period) && (0 == i)) {
          DomoticzFloatSensor(DZ_TEMP, ds18x20_sensor[index].temperature);
        }
#endif  // USE_DOMOTICZ
#ifdef USE_KNX
        if ((0 == TasmotaGlobal.tele_period) && (0 == i)) {
          KnxSensor(KNX_TEMPERATURE, ds18x20_sensor[index].temperature);
        }
#endif  // USE_KNX
#ifdef USE_WEBSERVER
      } else {
        WSContentSend_Temp(DS18X20Data.name, ds18x20_sensor[index].temperature);
#endif  // USE_WEBSERVER
      }
    }
  }
}

#ifdef DS18x20_USE_ID_ALIAS
const char kds18Commands[] PROGMEM = "DS18|"  // prefix
  D_CMND_DS_ALIAS;

void (* const DSCommand[])(void) PROGMEM = {
  &CmndDSAlias };

void CmndDSAlias(void) {
  // Ds18Alias 430516707FA6FF28,SensorName - Use SensorName instead of DS18B20
  // Ds18Alias 430516707FA6FF28,0          - Disable alias (default)
  char Argument1[XdrvMailbox.data_len];
  char Argument2[XdrvMailbox.data_len];
  char address[17];

  if (ArgC()==2) {
    ArgV(Argument1, 1);
    ArgV(Argument2, 2);
    TrimSpace(Argument2);

    for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
      for (uint32_t j = 0; j < 8; j++) {
        sprintf(address+2*j, "%02X", ds18x20_sensor[i].address[7-j]);
      }
      if (!strncmp(Argument1, address, 12) && Argument2[0]) {
        snprintf_P(ds18x20_sensor[i].alias, DS18X20_ALIAS_LEN, PSTR("%s"), Argument2);
        break;
      }
    }
  }

  Response_P(PSTR("{"));
  for (uint32_t i = 0; i < DS18X20Data.sensors; i++) {
    Ds18x20Name(i);
    char address[17];
    for (uint32_t j = 0; j < 8; j++) {
      sprintf(address+2*j, "%02X", ds18x20_sensor[ds18x20_sensor[i].index].address[7-j]);  // Skip sensor type and crc
    }
    ResponseAppend_P(PSTR("\"%s\":{\"" D_JSON_ID "\":\"%s\"}"),DS18X20Data.name, address);
    if (i < DS18X20Data.sensors-1) { ResponseAppend_P(PSTR(",")); }
  }
  ResponseAppend_P(PSTR("}"));
}
#endif  // DS18x20_USE_ID_ALIAS

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns05(uint32_t function) {
  bool result = false;

  if (PinUsed(GPIO_DSB, GPIO_ANY)) {
    switch (function) {
      case FUNC_INIT:
        Ds18x20Init();
        break;
      case FUNC_EVERY_SECOND:
        Ds18x20EverySecond();
        break;
      case FUNC_JSON_APPEND:
        Ds18x20Show(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        Ds18x20Show(0);
        break;
#endif  // USE_WEBSERVER
#ifdef DS18x20_USE_ID_ALIAS
      case FUNC_COMMAND:
        result = DecodeCommand(kds18Commands, DSCommand);
        break;
#endif // DS18x20_USE_ID_ALIAS
    }
  }
  return result;
}

#endif  // USE_DS18x20
#endif  // ESP8266