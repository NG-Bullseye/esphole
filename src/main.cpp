#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#include "DNSServer.h"

#define wifi_ssid "REMOVED_SSID"
#define wifi_password "REMOVED_PASSWORD"
#define LED_PIN LED_BUILTIN  // Onboard LED (GPIO2 on NodeMCU)

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer webServer(80);
File f;

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

void loop() {
  
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

      if (found)
      {
        Serial.printf(" Blocked | Find took %lu ms\n", findMs );

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
          dnsServer.replyWithIP(ip);

          uint32_t resolvMs = millis() - oMillis;

          Serial.print(" | IP:");
          Serial.print(ip);
          Serial.printf("\nResolv took %lu ms", resolvMs );
          Serial.printf(" | Find took %lu ms\n", findMs );
        } else {
          // DNS resolution failed - return server failure
          Serial.printf(" | DNS resolution FAILED (result: %d)\n", result);
          dnsServer.replyWithIP(IPAddress(0, 0, 0, 0));
        }
      }
      dom = "";
    }
  }
}
