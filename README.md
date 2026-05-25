# GitLog: ESP-NOW to GitHub Zero-Config Logging Bridge

GitLog is a lightweight C++ library and architecture for the ESP32. It allows multiple battery-powered ESP32 sensor nodes to wake up, broadcast data, and immediately return to deep sleep without ever connecting to Wi-Fi. 
Perfect for a Mailbox Sensors or Water Sensors applications.

A central "Bridge" ESP32 listens for these broadcasts via the ESP-NOW protocol, connects to Wi-Fi, and logs the events directly to a GitHub repository as individual text files using the GitHub REST API.

## 🚀 Key Features

* **Zero-Configuration Clients:** Client sensors require zero network configuration. They do not hold Wi-Fi SSIDs, passwords, or even unique IDs. The bridge dynamically extracts the sensor's hardware MAC address from the packet to use as its Globally Unique ID (GUID).
* **Auto-Channel Hopping & ACK:** Clients automatically sweep standard 2.4GHz Wi-Fi channels (1-11) until they receive an instant ACK (acknowledgment) from the server, preventing packet loss if the router assigns the server a new channel.
* **Time Syncing:** Server extracts the HTTP `Date` header from GitHub's API responses to perfectly sync and dynamically correct the ESP32's internal RTC down to the second.
* **Log File:** `YYYYMMDD-HHMMSS-[MAC].txt` file for every event. 
## 🏗️ System Architecture

1. **Client Node:** Wakes from Deep Sleep -> Sweeps channels broadcasting via ESP-NOW -> Receives ACK -> Sleeps.
2. **Server Bridge:** Listens constantly -> Sends ACK immediately on receipt -> Connects to GitHub API via HTTPS -> Formats payload to Base64 -> Pushes new file -> Updates RTC from GitHub's reply.

## 🛠️ Hardware & Software Requirements

* **Hardware:** Minimum of 2x ESP32 development boards (1 Server, 1+ Clients).
* **Software:** Arduino IDE with the official ESP32 Core installed.
* **Libraries:** All required libraries (`esp_now.h`, `WiFi.h`, `esp_wifi.h`, `HTTPClient.h`, `WiFiClientSecure.h`, `time.h`, `base64.h`) are built into the standard ESP32 core. No external downloads needed!

## ⚙️ Setup & Installation

### 1. GitHub Personal Access Token
To allow the Server to write files to your repository, you need a Personal Access Token (PAT).
1. Go to GitHub -> Settings -> Developer Settings -> Personal access tokens (Tokens (classic)).
2. Generate a new token and give it the **`repo`** scope (Full control of private repositories).
3. Copy the token. **Keep it secret.**

### 2. Configure the Server (Gateway)
Open `server/server.ino` and update the constructor with your credentials:

```cpp
gitlogServer = new GitlogServer(
  "YOUR_GITHUB_TOKEN", 
  "YOUR_GITHUB_USERNAME", 
  "YOUR_REPO_NAME", 
  "YOUR_WIFI_SSID", 
  "YOUR_WIFI_PWD"
);
```


## ⚠️ Troubleshooting

| Symptom | Common Cause | Solution |
| :--- | :--- | :--- |
| **Clients aren't reaching the server** | Range or interference issues. | Ensure your Server ESP32 is close enough to your Wi-Fi router to maintain a stable connection, and that the Clients are within ESP-NOW radio range of the Server (typically up to 200 meters line-of-sight, though walls reduce this). |
| **HTTP Error 401 or 403** | Authentication failure. | Your GitHub Personal Access Token is invalid, expired, or missing the `repo` scope. Generate a new token and update the Server sketch. |
| **HTTP Error 404** | Target not found. | The GitHub username or repository name provided in the Server constructor is incorrect or the repository is entirely empty (no main branch). Double-check your spelling. |
| **Clock Sync Failing** | Network timeout on boot. | Ensure the Server has a working internet connection.

