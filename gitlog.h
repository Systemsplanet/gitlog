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

// ─────────────────────────────────────────────
// Shared structures for ESP-NOW payload
// ESP-NOW max payload is 250 bytes.
// 244 bytes for the message + 6 bytes struct overhead = safe.
// ─────────────────────────────────────────────
#define GITLOG_MSG_MAX 243   // max usable chars (244 - 1 for null terminator)

typedef struct { char event_msg[244]; } gitlog_message_t;
typedef struct { bool success; }        gitlog_ack_t;


// ==========================================
// CLIENT CLASS
// ==========================================
class Gitlog {
private:
    uint8_t           serverAddress[6];
    esp_now_peer_info_t peerInfo;
    static Gitlog*    instance;
    volatile bool     ackReceived = false;

    static void onAckRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
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

    // FIX: Validate and warn if message exceeds the struct's capacity.
    // Returns false immediately (without broadcasting) if message is empty.
    // Truncation is logged so the caller is never silently misled.
    bool broadcast(const char* message) {

        if (!message || message[0] == '\0') {
            Serial.println("ERROR: broadcast() called with empty message.");
            return false;
        }

        if (strlen(message) >= sizeof(gitlog_message_t::event_msg)) {
           Serial.println("ERROR: Message exceeded %d bytes.", sizeof(gitlog_message_t::event_msg) - 1);
           return(false);
        }

        size_t msgLen = strlen(message);
        if (msgLen > GITLOG_MSG_MAX) {
            Serial.printf("WARNING: Message length %u exceeds max %d — truncated.\n",
                          msgLen, GITLOG_MSG_MAX);
        }

        gitlog_message_t msg;
        strncpy(msg.event_msg, message, sizeof(msg.event_msg) - 1);
        msg.event_msg[sizeof(msg.event_msg) - 1] = '\0';

        const int MAX_RETRIES = 2;
        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            Serial.printf("\n--- Sweep %d of %d ---\n", attempt, MAX_RETRIES);

            // Scan standard 2.4GHz channels (1–11)
            for (uint8_t ch = 1; ch <= 11; ch++) {
                esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                Serial.printf("Trying Channel %d... ", ch);

                ackReceived = false;
                esp_now_send(serverAddress, (uint8_t*)&msg, sizeof(msg));

                // Wait up to 150 ms for the server ACK
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

// FIX: Non-blocking callback queue.
// The ESP-NOW receive callback runs at near-interrupt level.
// Doing a full TLS HTTPS PUT inside it blocks the radio stack and
// causes instability.  Instead we copy the incoming event into a
// small pending-event slot and set a flag; process() (called from
// loop()) drains the queue safely.
struct PendingEvent {
    bool     pending;
    char     guid[13];
    char     event_msg[244];
};

class GitlogServer {
private:
    String githubToken, githubUser, githubRepo;
    static GitlogServer* instance;

    // Single-slot queue — sufficient because the GitHub PUT takes
    // ~1–3 s, far longer than any realistic inter-event gap for a
    // mailbox/water sensor. Extend to a ring-buffer if needed.
    static volatile bool pendingFlag;
    static PendingEvent  pendingEvent;

    // ── Clock sync ────────────────────────────────────────────────
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

    // ── Timestamp from RTC ────────────────────────────────────────
    String getTimestampFromRTC() {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        char buff[20];
        strftime(buff, sizeof(buff), "%Y%m%d-%H%M%S", &timeinfo);
        return String(buff);
    }

    // ── GitHub file upload ────────────────────────────────────────
    void pushToGitHub(const char* guid, const char* event) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi Disconnected. Cannot push to GitHub.");
            return;
        }

        String ts       = getTimestampFromRTC();
        String fileName = ts + "-" + guid + ".txt";
        String url      = "https://api.github.com/repos/" + githubUser + "/"
                          + githubRepo + "/contents/" + fileName;

        String fileContent = "Timestamp: " + ts + "\nGUID: " + guid
                             + "\nEvent: " + event;
        String payload = "{\"message\":\"Log " + String(guid)
                         + "\",\"content\":\"" + base64::encode(fileContent) + "\"}";

        WiFiClientSecure client;
        client.setInsecure(); // Note: disables cert verification — acceptable for
                              // isolated sensor networks; replace with a pinned cert
                              // for higher-security deployments.
        HTTPClient http;
        http.begin(client, url);

        const char* headerKeys[] = { "Date" };
        http.collectHeaders(headerKeys, 1);
        http.addHeader("Accept",        "application/vnd.github.v3+json");
        http.addHeader("Authorization", "Bearer " + githubToken);
        http.addHeader("User-Agent",    "ESP32-GitLog");

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

    // ── ESP-NOW receive callback (ISR-like context) ───────────────
    // FIX: Only copy data and set flag here — NO network calls.
    static void staticOnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
        if (!instance || len != sizeof(gitlog_message_t)) return;

        // 1. IMMEDIATELY SEND ACK
        gitlog_ack_t ack = { true };
        esp_now_peer_info_t replyPeer = {};
        memcpy(replyPeer.peer_addr, mac, 6);
        replyPeer.channel = 0;
        replyPeer.encrypt = false;
        esp_now_add_peer(&replyPeer);
        esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
        esp_now_del_peer(mac); // Prevent hitting the 20-peer limit

        // 2. COPY PAYLOAD INTO QUEUE — do NOT call pushToGitHub() here
        if (!pendingFlag) { // drop if still processing previous event
            gitlog_message_t msg;
            memcpy(&msg, incomingData, sizeof(msg));

            snprintf(pendingEvent.guid, sizeof(pendingEvent.guid),
                     "%02X%02X%02X%02X%02X%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

            strncpy(pendingEvent.event_msg, msg.event_msg,
                    sizeof(pendingEvent.event_msg) - 1);
            pendingEvent.event_msg[sizeof(pendingEvent.event_msg) - 1] = '\0';

            pendingFlag = true; // signal process() to upload
        } else {
            Serial.println("WARNING: Event dropped — still processing previous upload.");
        }
    }

public:
    GitlogServer(String token, String user, String repo,
                 const char* ssid, const char* pwd)
    {
        instance    = this;
        githubToken = token;
        githubUser  = user;
        githubRepo  = repo;

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pwd);
        Serial.print("Connecting to Wi-Fi");
        while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
        Serial.printf("\nWi-Fi Connected on Channel %d!\n", WiFi.channel());

        // Initial boot: ping GitHub to seed the RTC
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, "https://api.github.com/");
        const char* headerKeys[] = { "Date" };
        http.collectHeaders(headerKeys, 1);
        Serial.println("Fetching time from GitHub...");
        if (http.GET() > 0) {
            syncInternalClockWithGitHub(http.header("Date"));
        } else {
            Serial.println("Initial time sync failed. Will retry on next event.");
        }
        http.end();

        pendingFlag          = false;
        pendingEvent.pending = false;

        if (esp_now_init() == ESP_OK) {
            esp_now_register_recv_cb(staticOnDataRecv);
            Serial.println("Bridge Server Listening...");
        } else {
            Serial.println("Failed to initialize ESP-NOW Server.");
        }
    }

    // FIX: Call this from loop(). Drains the pending-event queue and
    // performs the actual GitHub upload outside of the callback context.
    void process() {
        if (pendingFlag) {
            pushToGitHub(pendingEvent.guid, pendingEvent.event_msg);
            pendingFlag = false;
        }
    }
};

// ── Static member definitions ──────────────────────────────────────
Gitlog*          Gitlog::instance       = nullptr;
GitlogServer*    GitlogServer::instance = nullptr;
volatile bool    GitlogServer::pendingFlag  = false;
PendingEvent     GitlogServer::pendingEvent = {};

#endif // GITLOG_H
