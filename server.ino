#include "gitlog.h"

// Keep this pointer global so it stays alive
GitlogServer* gitlogServer;

void setup() {
  Serial.begin(115200);

  // Initialize Server: Connects to Wi-Fi, syncs time, and listens for clients
  gitlogServer = new GitlogServer(
    "YOUR_GITHUB_PERSONAL_ACCESS_TOKEN", 
    "YOUR_GITHUB_USERNAME",              
    "YOUR_REPO_NAME",                    
    "YOUR_WIFI_SSID",                    
    "YOUR_WIFI_PASSWORD"                 
  );
}

void loop() {
  // The server loops endlessly, letting the library handle callbacks in the background
  delay(1000);
}
