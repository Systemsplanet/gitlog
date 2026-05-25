#include "gitlog.h"

// Keep this pointer global so it stays alive for the duration of the program.
GitlogServer* gitlogServer;

void setup() {
    Serial.begin(115200);

    // Initialize Server: connects to Wi-Fi, syncs RTC from GitHub,
    // and registers the ESP-NOW receive callback.
    gitlogServer = new GitlogServer(
        "YOUR_GITHUB_PERSONAL_ACCESS_TOKEN",
        "YOUR_GITHUB_USERNAME",
        "YOUR_REPO_NAME",
        "YOUR_WIFI_SSID",
        "YOUR_WIFI_PASSWORD"
    );
}

void loop() {
    // GitHub HTTPS upload runs in the main task context —
    // NOT inside the ESP-NOW receive callback (which is interrupt-level and
    // cannot safely block for TLS/HTTP).
    gitlogServer->process();

    delay(100); // Yield to the radio stack between iterations
}
