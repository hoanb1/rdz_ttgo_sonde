#include <Arduino.h>
#include <SPI.h>

#define RADIO_NSS 8
#define RADIO_BUSY 13
#define RADIO_RESET 12
#define RADIO_DIO_1 14

#ifdef SX126X
#include "sx126x_hal.h"

SPISettings spiSettings = SPISettings(4E6L, MSBFIRST, SPI_MODE0);

sx126x_hal_status_t sx126x_hal_write(const void* context, const uint8_t* command, const uint16_t command_length,
                                     const uint8_t* data, const uint16_t data_length) {
  int i;

  while (digitalRead(RADIO_BUSY) == HIGH)
    ;
  digitalWrite(RADIO_NSS, LOW);
  SPI.beginTransaction(spiSettings);
  for (i = 0; i < command_length; i++)
    SPI.transfer(command[i]);
  for (i = 0; i < data_length; i++)
    SPI.transfer(data[i]);
  SPI.endTransaction();
  digitalWrite(RADIO_NSS, HIGH);
  return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_read(const void* context, const uint8_t* command, const uint16_t command_length,
                                    uint8_t* data, const uint16_t data_length) {
  int i;

  while (digitalRead(RADIO_BUSY) == HIGH)
    ;
  digitalWrite(RADIO_NSS, LOW);
  SPI.beginTransaction(spiSettings);
  for (i = 0; i < command_length; i++)
    SPI.transfer(command[i]);
  for (i = 0; i < data_length; i++)
    data[i] = SPI.transfer(0);
  SPI.endTransaction();
  digitalWrite(RADIO_NSS, HIGH);
  return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_reset(const void* context) {
  digitalWrite(RADIO_RESET, LOW);
  delayMicroseconds(120);
  digitalWrite(RADIO_RESET, HIGH);
  return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_wakeup(const void* context) {
  digitalWrite(RADIO_NSS, LOW);
  delay(1);
  digitalWrite(RADIO_NSS, HIGH);
  return SX126X_HAL_STATUS_OK;
}
#endif