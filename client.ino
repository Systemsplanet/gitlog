
#include "gitlog.h"

Gitlog* gitlog;

// Sleep time in microseconds (e.g., 60 seconds)
#define TIME_TO_SLEEP 60 * 1000000ULL 

void setup() {
  Serial.begin(115200);
  
  // Initialize the scanner client
  gitlog = new Gitlog();

  // Broadcast event and wait for server ACK
  bool success = gitlog->broadcast("WAKE_UP_EVENT");
  
  if (success) {
    Serial.println("Data delivered successfully!");
  } else {
    Serial.println("Failed to reach server. Event lost.");
  }
  
  // Enter Deep Sleep to conserve battery
  Serial.println("Going to sleep.");
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP); 
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
  // Never reached due to deep sleep
}

