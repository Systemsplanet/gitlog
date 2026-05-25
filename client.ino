// Gitlog Deep Sleep Mode Example Client
//
// Powers off CPU cores, most RAM, Wi-Fi radio, and peripherals.
// Only the RTC module and RTC memory remain on.
// Current during sleep: ~10–25 µA.
//
// On wakeup the ESP32 reboots completely and starts
// from the top of setup().

#include "gitlog.h"
#include "esp_sleep.h"
#include <WiFi.h>
#define SLEEP_FOREVER (60ULL * 60ULL * 24ULL * 365ULL * 100ULL * 1000000ULL)

// RTC slow memory stays powered during Deep Sleep.
// bootCount persists across sleep cycles
// but resets to 0 when power is removed or the chip is physically reset.
RTC_DATA_ATTR int bootCount = 0;
Gitlog* gitlog;

void setup() {
    Serial.begin(115200);
    delay(500); // Let UART settle
    ++bootCount;
    gitlog = new Gitlog(); // Init ESP-NOW client — MAC address is available after this
    
    // NOTE: Keep total msg length under 243 characters or broadcast() will fail.
    String msg = "{"
                 "\"mac\":\"" + WiFi.macAddress() + "\","
                 "\"type\":\"WAKE_UP_EVENT\","
                 "\"bootCount\":" + String(bootCount) +
                 "}";
    Serial.println(msg);
    Serial.flush();

    // Broadcast event and wait for server ACK
    if (!gitlog->broadcast(msg.c_str())) {
        Serial.println("ERROR: Event discarded.");
    }

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Wakeup cause: RTC Timer");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("Wakeup cause: EXT0 GPIO");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("Wakeup cause: EXT1 GPIO");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            Serial.println("Wakeup cause: Touch sensor");
            break;
        default:
            Serial.println("Wakeup cause: Power-on / hard reset");
            break;
    }

    // <- Your sensor reading / work goes here

    delay(1000); // minimum time between logs

    // ULTRA-LOW POWER DEEP SLEEP CONFIGURATION
    Serial.println("Sleeping...");
    Serial.flush(); // Ensure all serial data is sent before cutting power

    // Configure timer wakeup and sleep
    esp_sleep_enable_timer_wakeup(SLEEP_FOREVER);

    // Power down peripherals and fast memory to save energy,
    // but leave RTC_SLOW_MEM ON so our bootCount survives!
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON); // Required for RTC variables!

    esp_deep_sleep_start(); // Does not return
}

void loop() {
    // Not used — device always deep-sleeps before reaching here.
}
