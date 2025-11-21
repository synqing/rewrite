#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include <Arduino.h>
#include <HardwareSerial.h>

// For ESP32-S3 with Arduino core 3.x and USB CDC on boot,
// we need to ensure proper Serial availability
#if defined(ARDUINO_ESP32S3_DEV)
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    // Use the built-in Serial which is USB CDC
    #define USBSerial Serial
  #else
    // Use HWCDC for S3 without CDC on boot
    #include <HWCDC.h>
    extern HWCDC USBSerial;
  #endif
#else
  // ESP32-S2 uses USBCDC
  #include <USB.h>
  extern USBCDC USBSerial;  
#endif

#endif // SERIAL_CONFIG_H