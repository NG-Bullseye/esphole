#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#include "DNSServer.h"
#include "config.h"

#define LED_PIN LED_BUILTIN  // Onboard LED (GPIO2 on NodeMCU)

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer webServer(80);
File f;

// Statistics
unsigned long totalRequests = 0;
unsigned long blockedRequests = 0;
unsigned long allowedRequests = 0;
unsigned long startTime = 0;
bool blockingEnabled = true;

// Last queries log (circular buffer)
#define LOG_SIZE 20
String lastQueries[LOG_SIZE];
bool lastQueriesBlocked[LOG_SIZE];
int logIndex = 0;

// Forward declarations
void handleRoot();
void handleStats();
void handleToggle();
void handleManifest();
void logQuery(String domain, bool blocked);
int find_text(String needle, String haystack);

void setup_wifi() {
  delay(10);

  Serial.println();
  Serial.print("Connecting to: ");
  Serial.println(wifi_ssid);

  // Configure static IP before connecting
  IPAddress staticIP(192, 168, 178, 87);      // Fixed IP for NodeMCU
  IPAddress gateway(192, 168, 178, 1);        // Router/FritzBox IP
  IPAddress subnet(255, 255, 255, 0);         // Subnet mask
  IPAddress dns1(8, 8, 8, 8);                 // Google DNS primary
  IPAddress dns2(8, 8, 4, 4);                 // Google DNS secondary

  WiFi.config(staticIP, gateway, subnet, dns1, dns2);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("WiFi connected | IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("Upstream DNS: ");
  Serial.print(dns1);
  Serial.print(", ");
  Serial.println(dns2);
}

void setup() {
  Serial.begin(9600);

  // Configure and turn on onboard LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // LED is active LOW on NodeMCU

  WiFi.mode(WIFI_AP_STA);

  setup_wifi();

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("ERROR: LittleFS mount failed!");
  } else {
    Serial.println("LittleFS mounted successfully");

    // List files in filesystem
    Dir dir = LittleFS.openDir("/");
    Serial.println("Files in LittleFS:");
    while (dir.next()) {
      Serial.print("  ");
      Serial.print(dir.fileName());
      Serial.print(" (");
      Serial.print(dir.fileSize());
      Serial.println(" bytes)");
    }
  }

  dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);

  bool dns_running = dnsServer.start(DNS_PORT, "*", WiFi.localIP());
  if (dns_running)
  {
    Serial.println("DNS Server ready");
  }
  else
  {
    Serial.println("Error: DNS Server not running");
  }

  // Start web server
  webServer.on("/", handleRoot);
  webServer.on("/api/stats", handleStats);
  webServer.on("/api/toggle", handleToggle);
  webServer.on("/manifest.json", handleManifest);
  webServer.begin();
  Serial.println("Web Server started on http://" + WiFi.localIP().toString());

  // Initialize start time for uptime calculation
  startTime = millis();
}

void logQuery(String domain, bool blocked) {
  lastQueries[logIndex] = domain;
  lastQueriesBlocked[logIndex] = blocked;
  logIndex = (logIndex + 1) % LOG_SIZE;
}

int find_text(String needle, String haystack) {
  int foundpos = -1;
  for (int i = 0; i <= haystack.length() - needle.length(); i++) {
    if (haystack.substring(i,needle.length()+i) == needle) {
      foundpos = i;
    }
  }
  return foundpos;
}

// Web Interface Handlers
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=no'>";
  html += "<meta name='theme-color' content='#2196F3'>";
  html += "<meta name='apple-mobile-web-app-capable' content='yes'>";
  html += "<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>";
  html += "<meta name='apple-mobile-web-app-title' content='ESPhole'>";
  html += "<link rel='manifest' href='/manifest.json'>";
  html += "<link rel='icon' type='image/svg+xml' href='data:image/svg+xml,<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"><circle cx=\"50\" cy=\"50\" r=\"45\" fill=\"%232196F3\"/><text x=\"50\" y=\"70\" font-size=\"60\" text-anchor=\"middle\" fill=\"white\">🛡️</text></svg>'>";
  html += "<title>ESPhole - DNS Ad Blocker</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5;}";
  html += ".container{max-width:1200px;margin:0 auto;}";
  html += ".card{background:white;border-radius:8px;padding:20px;margin:20px 0;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
  html += "h1{color:#333;margin:0 0 10px 0;}";
  html += ".subtitle{color:#666;margin:0 0 20px 0;}";
  html += ".stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin:20px 0;}";
  html += ".stat-box{background:#f9f9f9;padding:15px;border-radius:5px;text-align:center;}";
  html += ".stat-value{font-size:32px;font-weight:bold;color:#2196F3;}";
  html += ".stat-label{color:#666;margin-top:5px;}";
  html += ".blocked{color:#f44336;}";
  html += ".allowed{color:#4CAF50;}";
  html += ".log-entry{padding:10px;border-bottom:1px solid #eee;display:flex;justify-content:space-between;}";
  html += ".log-entry:last-child{border:none;}";
  html += ".badge{padding:4px 8px;border-radius:4px;font-size:12px;font-weight:bold;}";
  html += ".badge-blocked{background:#ffebee;color:#c62828;}";
  html += ".badge-allowed{background:#e8f5e9;color:#2e7d32;}";
  html += "button{background:#2196F3;color:white;border:none;padding:10px 20px;border-radius:5px;cursor:pointer;font-size:16px;}";
  html += "button:hover{background:#1976D2;}";
  html += ".status{display:inline-block;width:12px;height:12px;border-radius:50%;background:#4CAF50;margin-right:8px;}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<div class='card'>";
  html += "<h1><span class='status'></span>ESPhole DNS Ad Blocker</h1>";
  html += "<p class='subtitle'>IP: " + WiFi.localIP().toString() + " | Uptime: " + String((millis() - startTime) / 1000) + "s</p>";
  html += "<p style='color:" + String(blockingEnabled ? "#4CAF50" : "#f44336") + ";font-weight:bold;'>";
  html += "Status: " + String(blockingEnabled ? "Blocking Enabled ✓" : "Blocking Disabled ✗");
  html += "</p></div>";

  html += "<div class='card'>";
  html += "<h2>Statistics</h2>";
  html += "<div class='stats'>";
  html += "<div class='stat-box'><div class='stat-value'>" + String(totalRequests) + "</div><div class='stat-label'>Total Requests</div></div>";
  html += "<div class='stat-box'><div class='stat-value blocked'>" + String(blockedRequests) + "</div><div class='stat-label'>Blocked</div></div>";
  html += "<div class='stat-box'><div class='stat-value allowed'>" + String(allowedRequests) + "</div><div class='stat-label'>Allowed</div></div>";
  html += "<div class='stat-box'><div class='stat-value'>" + String(totalRequests > 0 ? (blockedRequests * 100 / totalRequests) : 0) + "%</div><div class='stat-label'>Block Rate</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Recent Queries</h2>";
  for (int i = 0; i < LOG_SIZE; i++) {
    int idx = (logIndex - 1 - i + LOG_SIZE) % LOG_SIZE;
    if (lastQueries[idx] != "") {
      html += "<div class='log-entry'>";
      html += "<span>" + lastQueries[idx] + "</span>";
      html += "<span class='badge " + String(lastQueriesBlocked[idx] ? "badge-blocked" : "badge-allowed") + "'>";
      html += lastQueriesBlocked[idx] ? "BLOCKED" : "ALLOWED";
      html += "</span></div>";
    }
  }
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Controls</h2>";
  html += "<div style='display:flex;gap:10px;flex-wrap:wrap;'>";
  html += "<button onclick='toggleBlocking()'>" + String(blockingEnabled ? "Disable Blocking" : "Enable Blocking") + "</button>";
  html += "<button onclick='location.reload()'>Refresh</button>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Update Block Lists</h2>";
  html += "<p>To update with the latest German-optimized block lists:</p>";
  html += "<ol style='line-height:1.8;'>";
  html += "<li>On your PC, run: <code style='background:#f5f5f5;padding:2px 6px;border-radius:3px;'>python utils/gen_block_lists.py</code></li>";
  html += "<li>Then upload to NodeMCU: <code style='background:#f5f5f5;padding:2px 6px;border-radius:3px;'>pio run --target uploadfs</code></li>";
  html += "<li>Lists will be updated automatically</li>";
  html += "</ol>";
  html += "<p style='color:#666;font-size:14px;'>Block list sources: <a href='https://pgl.yoyo.org/adservers/' target='_blank'>pgl.yoyo.org</a>, OISD, StevenBlack</p>";
  html += "</div>";

  html += "<script>";
  html += "function toggleBlocking(){";
  html += "fetch('/api/toggle').then(r=>r.json()).then(d=>{alert(d.message);location.reload();});";
  html += "}";
  html += "</script>";

  html += "</div></body></html>";

  webServer.send(200, "text/html", html);
}

void handleStats() {
  String json = "{";
  json += "\"total\":" + String(totalRequests) + ",";
  json += "\"blocked\":" + String(blockedRequests) + ",";
  json += "\"allowed\":" + String(allowedRequests) + ",";
  json += "\"uptime\":" + String((millis() - startTime) / 1000) + ",";
  json += "\"blocking_enabled\":" + String(blockingEnabled ? "true" : "false");
  json += "}";
  webServer.send(200, "application/json", json);
}

void handleToggle() {
  blockingEnabled = !blockingEnabled;
  String json = "{";
  json += "\"blocking_enabled\":" + String(blockingEnabled ? "true" : "false") + ",";
  json += "\"message\":\"Blocking " + String(blockingEnabled ? "enabled" : "disabled") + "\"";
  json += "}";
  Serial.println("Blocking " + String(blockingEnabled ? "ENABLED" : "DISABLED"));
  webServer.send(200, "application/json", json);
}

void handleManifest() {
  String manifest = "{";
  manifest += "\"name\":\"ESPhole DNS Ad Blocker\",";
  manifest += "\"short_name\":\"ESPhole\",";
  manifest += "\"description\":\"Network-wide Ad & Tracking Blocker\",";
  manifest += "\"start_url\":\"/\",";
  manifest += "\"display\":\"standalone\",";
  manifest += "\"background_color\":\"#f5f5f5\",";
  manifest += "\"theme_color\":\"#2196F3\",";
  manifest += "\"icons\":[{";
  manifest += "\"src\":\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'><circle cx='256' cy='256' r='240' fill='%232196F3'/><text x='256' y='380' font-size='300' text-anchor='middle' fill='white'>🛡️</text></svg>\",";
  manifest += "\"sizes\":\"512x512\",";
  manifest += "\"type\":\"image/svg+xml\"";
  manifest += "}]";
  manifest += "}";
  webServer.send(200, "application/manifest+json", manifest);
}

void loop() {
  // Handle web server requests
  webServer.handleClient();

  int dnsOK = dnsServer.processNextRequest();
  if (dnsOK == 0)
  {
    String dom = dnsServer.getQueryDomainName();
    f.setTimeout(5000);

    if ((dom != ""))
    {
      // Remove router suffixes (Fritz!Box, local network)
      dom.replace(".fritz.box", "");
      dom.replace(".local", "");

      Serial.println ();
      Serial.print ("Domain: ");
      Serial.print (dom);

      char str[12];
      sprintf(str, "/hosts_%d", dom.length());

      f = LittleFS.open(str, "r");
      if (!f) {
          Serial.printf("\nError: file open failed\n");
      }
     
      f.seek(0, SeekSet);
       
      uint32_t oMillis = millis();
      
      char dom_str[dom.length()+2];
      sprintf(dom_str, ",%s,", dom.c_str());
      
      bool found = f.findUntil(dom_str,"@@@");

      // If not found, check parent domains (for subdomains like adclick.g.doubleclick.net)
      if (!found && dom.indexOf('.') != -1) {
        String parentDom = dom;
        while (!found && parentDom.indexOf('.') != -1) {
          // Remove first subdomain level
          int firstDot = parentDom.indexOf('.');
          parentDom = parentDom.substring(firstDot + 1);

          // Check parent domain
          char parent_str[12];
          sprintf(parent_str, "/hosts_%d", parentDom.length());
          File pf = LittleFS.open(parent_str, "r");

          if (pf) {
            char parent_dom_str[parentDom.length()+2];
            sprintf(parent_dom_str, ",%s,", parentDom.c_str());
            found = pf.findUntil(parent_dom_str,"@@@");
            pf.close();

            if (found) {
              Serial.print(" (parent: ");
              Serial.print(parentDom);
              Serial.print(")");
            }
          }
        }
      }

      uint32_t findMs = millis() - oMillis;

      // Check if blocking is enabled
      if (found && blockingEnabled)
      {
        Serial.printf(" Blocked | Find took %lu ms\n", findMs );

        // Update statistics
        totalRequests++;
        blockedRequests++;
        logQuery(dom, true);

        // Blink LED to indicate blocked domain
        digitalWrite(LED_PIN, HIGH);  // Turn LED off
        delay(100);                   // Wait 100ms
        digitalWrite(LED_PIN, LOW);   // Turn LED back on

        dnsServer.replyWithIP(IPAddress(0, 0, 0, 0));
      }
      else
      {
        IPAddress ip;
        uint32_t oldMillis = millis();
        int result = WiFi.hostByName(dom.c_str(), ip);

        // Check if DNS resolution was successful
        if (result == 1 && ip != IPAddress(0, 0, 0, 0)) {
          // Update statistics
          totalRequests++;
          allowedRequests++;
          logQuery(dom, false);

          dnsServer.replyWithIP(ip);

          uint32_t resolvMs = millis() - oMillis;

          Serial.print(" | IP:");
          Serial.print(ip);
          Serial.printf("\nResolv took %lu ms", resolvMs );
          Serial.printf(" | Find took %lu ms\n", findMs );
        } else {
          // DNS resolution failed - return server failure
          Serial.printf(" | DNS resolution FAILED (result: %d)\n", result);

          // Count as allowed (not blocked by us, just failed to resolve)
          totalRequests++;
          allowedRequests++;
          logQuery(dom + " (FAILED)", false);

          dnsServer.replyWithIP(IPAddress(0, 0, 0, 0));
        }
      }
      dom = "";
    }
  }
}
