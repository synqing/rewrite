#ifndef USB_CDC_CONFIG_H
#define USB_CDC_CONFIG_H

#include <Arduino.h>

// USB CDC Configuration for ESP32-S3
#if defined(ARDUINO_ARCH_ESP32) && !defined(ARDUINO_USB_MODE)
  // Enable USB CDC on boot
  #define ARDUINO_USB_MODE 1
  #define ARDUINO_USB_CDC_ON_BOOT 1
  
  // USB CDC buffer sizes
  #define CONFIG_USB_CDC_ENABLED 1
  #define CONFIG_USB_CDC_RX_BUFSIZE 512
  #define CONFIG_USB_CDC_TX_BUFSIZE 512
  #define CONFIG_USB_CDC_RINGBUF_SIZE 1024
  
  // USB CDC task configurations
  #define CONFIG_USB_CDC_EVENT_TASK_STACK_SIZE 4096
  #define CONFIG_USB_CDC_EVENT_TASK_PRIORITY 5
  #define CONFIG_USB_CDC_RX_TASK_STACK_SIZE 4096
  #define CONFIG_USB_CDC_RX_TASK_PRIORITY 5
  #define CONFIG_USB_CDC_TX_TASK_STACK_SIZE 4096
  #define CONFIG_USB_CDC_TX_TASK_PRIORITY 5
  
  // TinyUSB CDC ACM (Abstract Control Model) configuration
  #define CONFIG_TINYUSB_CDC_ENABLED 1
  #define CONFIG_TINYUSB_CDC_RX_BUFSIZE 512
  #define CONFIG_TINYUSB_CDC_TX_BUFSIZE 512
  #define CONFIG_TINYUSB_CDC_ACM_ENABLED 1
  
  // Enable CDC ACM notifications
  #define CONFIG_TINYUSB_CDC_ACM_NOTIFY 1
  #define CONFIG_TINYUSB_CDC_ACM_NOTIFY_SERIAL_STATE 1
  #define CONFIG_TINYUSB_CDC_ACM_NOTIFY_NETWORK_CONNECTION 1
  #define CONFIG_TINYUSB_CDC_ACM_NOTIFY_LINE_STATE 1
  #define CONFIG_TINYUSB_CDC_ACM_NOTIFY_LINE_CODING 1
  #define CONFIG_TINYUSB_CDC_ACM_NOTIFY_CONTROL_LINE_STATE 1
  #define CONFIG_TINYUSB_CDC_ACM_NOTIFY_LINE_BREAK 1
  
  // Function declarations with C++ name mangling protection
  #ifdef __cplusplus
    extern "C" {
  #endif
    
    /**
     * @brief Initialize USB CDC serial communication
     * 
     * This function initializes the USB CDC interface on ESP32-S3 with the following features:
     * - Sets up USB descriptors with manufacturer and product information
     * - Configures USB CDC with appropriate buffer sizes
     * - Registers event callbacks for USB events
     * - Starts the USB stack
     */
    void usb_cdc_init(void);
    
    /**
     * @brief USB event callback handler
     * 
     * @param arg User data pointer
     * @param event_base Event base
     * @param event_id Event ID
     * @param event_data Event data
     */
    void usbEventCallback(void *arg, esp_event_base_t event_base, 
                         int32_t event_id, void *event_data);
  
  #ifdef __cplusplus
    }
  #endif
  
  // Include Arduino USB and USBCDC headers
  #include <USB.h>
  #include <USBCDC.h>
  
  // Define USBSerial if not already defined
  #ifndef USBSerial
    #define USBSerial Serial
  #endif
  
  // Redirect stdout/stderr to USB CDC
  #ifdef __cplusplus
    extern "C" {
  #endif
    int _write(int fd, const char *buf, size_t count) {
      if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        USBSerial.write((const uint8_t *)buf, count);
        return count;
      }
      return -1;
    }
  #ifdef __cplusplus
    }
  #endif
  
#endif // ARDUINO_ARCH_ESP32 && !ARDUINO_USB_MODE

#endif // USB_CDC_CONFIG_H
