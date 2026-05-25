#pragma once
#ifndef GITLOG_H
#define GITLOG_H

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <base64.h>

// Shared structures for ESP-NOW payload
typedef struct { char event_msg[32]; } gitlog_message_t;
typedef struct { bool success; } gitlog_ack_t;

// ==========================================
// CLIENT CLASS
// ==========================================
class Gitlog {
  private:
    uint8_t serverAddress[6];
    esp_now_peer_info_t peerInfo;
    static Gitlog* instance;
    volatile bool ackReceived = false;

    static void onAckRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
      if (instance && len == sizeof(gitlog_ack_t)) {
        instance->ackReceived = true;
      }
    }

  public:
    Gitlog() {
      instance = this;
      WiFi.mode(WIFI_STA);
      WiFi.disconnect(); 
      
      if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Init Failed");
        return;
      }

      esp_now_register_recv_cb(onAckRecv);

      // Setup Broadcast Peer (FF:FF:FF:FF:FF:FF)
      memset(serverAddress, 0xFF, 6); 
      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, serverAddress, 6);
      peerInfo.channel = 0;  
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);
    }

    bool broadcast(const char* message) {
      gitlog_message_t msg;
      strncpy(msg.event_msg, message, sizeof(msg.event_msg) - 1);
      msg.event_msg[sizeof(msg.event_msg) - 1] = '\0';

      const int MAX_RETRIES = 2; 

      for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        Serial.printf("\n--- Sweep %d of %d ---\n", attempt, MAX_RETRIES);
        
        // Scan standard 2.4GHz channels (1-11)
        for (uint8_t ch = 1; ch <= 11; ch++) {
          esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
          Serial.printf("Trying Channel %d... ", ch);
          
          ackReceived = false;
          esp_now_send(serverAddress, (uint8_t *) &msg, sizeof(msg));

          // Wait up to 150ms for the server to reply with an ACK
          unsigned long startWait = millis();
          while (millis() - startWait < 150) {
            if (ackReceived) {
              Serial.println("ACK RECEIVED!");
              return true; 
            }
            delay(5);
          }
          Serial.println("No reply.");
        }
      }
      return false; // Server unreachable
    }
};

// ==========================================
// SERVER CLASS
// ==========================================
class GitlogServer {
  private:
    String githubToken, githubUser, githubRepo;
    static GitlogServer* instance; 

    void syncInternalClockWithGitHub(String httpDate) {
      if (httpDate == "") return;
      struct tm tm;
      if (strptime(httpDate.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm)) {
        time_t githubTime = mktime(&tm);
        time_t internalTime;
        time(&internalTime);
        long drift = (long)(githubTime - internalTime);

        struct timeval tv = { .tv_sec = githubTime, .tv_usec = 0 };
        settimeofday(&tv, NULL);

        Serial.printf("-> GitHub Time: %s\n", httpDate.c_str());
        if (internalTime != 0) {
          Serial.printf("-> Clock Drift Corrected: %ld seconds\n", drift);
        }
      }
    }

    String getTimestampFromRTC() {
      time_t now;
      struct tm timeinfo;
      time(&now);
      localtime_r(&now, &timeinfo);
      char buff[20];
      strftime(buff, sizeof(buff), "%Y%m%d-%H%M%S", &timeinfo);
      return String(buff);
    }

    void pushToGitHub(String guid, String event) {
      if(WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi Disconnected. Cannot push to GitHub.");
        return;
      }

      String ts = getTimestampFromRTC();
      String fileName = ts + "-" + guid + ".txt";
      String url = "https://api.github.com/repos/" + githubUser + "/" + githubRepo + "/contents/" + fileName;

      String fileContent = "Timestamp: " + ts + "\nGUID: " + guid + "\nEvent: " + event;
      String payload = "{\"message\":\"Log " + guid + "\",\"content\":\"" + base64::encode(fileContent) + "\"}";

      // Stack allocation prevents heap fragmentation over time
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      http.begin(client, url);
      
      const char* headerKeys[] = {"Date"};
      http.collectHeaders(headerKeys, 1);
      
      http.addHeader("Accept", "application/vnd.github.v3+json");
      http.addHeader("Authorization", "Bearer " + githubToken);
      http.addHeader("User-Agent", "ESP32-GitLog");
      
      Serial.println("\nCreating GitHub File: " + fileName);
      int httpCode = http.PUT(payload);
      
      if (httpCode == 201) {
        Serial.println("Success! File uploaded.");
        syncInternalClockWithGitHub(http.header("Date"));
      } else {
        Serial.printf("HTTP Error Code: %d\n", httpCode);
        Serial.println(http.getString());
      }
      http.end();
    }

    static void staticOnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
      if (!instance || len != sizeof(gitlog_message_t)) return;

      // 1. IMMEDIATELY SEND ACK 
      gitlog_ack_t ack = {true};
      esp_now_peer_info_t replyPeer = {};
      memcpy(replyPeer.peer_addr, mac, 6);
      replyPeer.channel = 0; 
      replyPeer.encrypt = false;
      
      esp_now_add_peer(&replyPeer);
      esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
      esp_now_del_peer(mac); // Cleanup to prevent max peer limits

      // 2. PROCESS PAYLOAD
      gitlog_message_t msg;
      memcpy(&msg, incomingData, sizeof(msg));
      
      char guidStr[13]; 
      snprintf(guidStr, sizeof(guidStr), "%02X%02X%02X%02X%02X%02X", 
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
               
      // 3. UPLOAD TO CLOUD
      instance->pushToGitHub(String(guidStr), String(msg.event_msg));
    }

  public:
    GitlogServer(String token, String user, String repo, const char* ssid, const char* pwd) {
      instance = this;
      githubToken = token; 
      githubUser = user; 
      githubRepo = repo;

      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, pwd);
      Serial.print("Connecting to Wi-Fi");
      while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
      Serial.printf("\nWi-Fi Connected on Channel %d!\n", WiFi.channel());

      // Ping GitHub for initial boot time to seed the RTC
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      http.begin(client, "https://api.github.com/");
      const char* headerKeys[] = {"Date"};
      http.collectHeaders(headerKeys, 1);
      Serial.println("Fetching time from GitHub...");
      if (http.GET() > 0) {
        syncInternalClockWithGitHub(http.header("Date"));
      } else {
        Serial.println("Initial time sync failed. Will retry on next event.");
      }
      http.end();

      if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(staticOnDataRecv);
        Serial.println("Bridge Server Listening...");
      } else {
        Serial.println("Failed to initialize ESP-NOW Server.");
      }
    }
};

// Initialize static instances
Gitlog* Gitlog::instance = nullptr;
GitlogServer* GitlogServer::instance = nullptr;

#endif
