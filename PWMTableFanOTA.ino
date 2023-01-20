/*
 *  This sketch uses a simple HTTP-like server.to control the 2-channel L293 esp-12e motor shield
 *    http://server_ip:8080/pwm1/n will set motor 1 output value n where n=0-100
 *    http://server_ip:8080/pwm2/n will set motor 2 output value n where n=0-100
 *    http://server_ip:8080/dir1/1 will reverse motor 1 output polarity
 *    http://server_ip:8080/dir2/1 will reverse motor 2 output polarity
 *    http://server_ip:8080/index.html will return status
 *    
 *    2019-05-14 uses the NodeMCU motor shield as a dual fan PWM controller
 *    https://hackaday.io/project/8856-incubator-controller/log/29291-node-mcu-motor-shield
 *    23-01-07 Added analogWriteRange(100) and analogWriteFreq(5); Note the
 *             reference states 1000Hz is minimum but it works with Arduino IDE
 *             builtin library v1.8.5
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h> // 2019-10-15
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
extern "C" {
  #include <user_interface.h>
}

// #define deploy 1


const char* ssid = "MYHOTSPOT"; // For testing in study
const char* password = "MYPASSWORD"; // 343 wifi repeater
IPAddress staticIP(12,34,56,78); 
IPAddress gateway(12,34,56,1);    

IPAddress subnet(255,255,255,0);
WiFiServer server (8080);
uint8_t PWMpin1 = D1;
uint8_t PWMpin2 = D2;
uint8_t Dirpin1 = D3;
uint8_t Dirpin2 = D4;
uint8_t Dir1 = HIGH; // 2023-01-08 set output polarities to conform to shield 
uint8_t Dir2 = HIGH;

static int val = 0; // 2018-10-25
static int val2 = 0; // 2019-04-19
static int dots = 0;
void setup () 
{
  delay (10);
  Serial.begin (115200);
  pinMode(Dirpin1, OUTPUT); 
  pinMode(Dirpin2, OUTPUT); 
  analogWriteRange(100); /* 2023-01-09 Use default  */
  analogWriteFreq(3); /* Arduino v1.8.5 only */
  digitalWrite(Dirpin1, Dir1); 
  digitalWrite(Dirpin2, Dir2);
  analogWrite(PWMpin1, val);  /* 60 == 4V at VM 9V set initial duty cycle 2022-10-03 */
  analogWrite(PWMpin2, val2);  /* set initial duty cycle 2022-10-03 */
  
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_OFF); // 2018-10-09 workaround
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  WiFi.config(staticIP, gateway, subnet); // Static IP. Not required for dhcp  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    dots++;
    if (dots >= 10)
    {
      Serial.println("Giving up! Resetting CPU ...");
      delay(2000);
      ESP.restart();
    }            
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Static IP address: "); 
  Serial.println(WiFi.localIP());
  
  // Start the server
  server.begin();
  delay(50);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("PWMTableFanOTA");

  // No authentication by default
  ArduinoOTA.setPassword("MyArduinoOTApassword");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("ArduinoOTA Ready");
  
}


static int commas = 0;
static unsigned long timeNow = 0; // 2019-03-17 
static unsigned long timeLast = 0;
static int seconds = 0;

uint16_t dutycycle = 0; /* Set PWM duty cycle */
uint8_t dir1 = 0; // Set direction of motor. Also reverses polarity!
uint8_t dir2 = 0;

// Make text reply to HTTP GET. Note the String s *must* be pre-allocated memory and not
// from the stack
void http_reply_txt(String &s, const String msg) {
  String preamble = "HTTP/1.1 200 OK\r\nContent-Length: "; // need to add \r\n;
  
  s = "\r\n" + msg + "\r\n";

  preamble += String(s.length(), DEC);
  preamble += "\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";

  s = preamble + s;  
}

void loop() {
  ArduinoOTA.handle(); // 2019-10-15

  // 2019-03-17
  timeNow = millis()/1000; // the number of milliseconds that have passed since boot
  if (timeLast==0 || timeNow<timeLast)
    timeLast = timeNow; // Start with time since bootup. Note millis() number overflows after 50 days
  seconds = timeNow - timeLast;//the number of seconds that have passed since the last time 60 seconds was reached.
  
  // 2019-02-24 Check for broken wifi links
  if (WiFi.status() != WL_CONNECTED) {
    delay(10);
    // Connect to WiFi network
    Serial.println();
    delay(10);
    Serial.print("Reconnecting WiFi ");
    delay(10);
    WiFi.mode(WIFI_OFF); // 2018-10-09 workaround
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    WiFi.config(staticIP, gateway, subnet); // Static IP. Not required for dhcp  

    while (WiFi.status() != WL_CONNECTED) {
      Serial.print("x");
      delay(500);
      dots++;
      if (dots >= 10)
      {
        dots = 0;
        Serial.println("Giving up! Resetting CPU ...");
        delay(2000);
        ESP.restart();        
      }
    }
    Serial.println("");
    delay(10);
    Serial.println("WiFi reconnected");

    // Start the server
    server.begin();
    Serial.println("Server restarted");
    delay(10);
    // Print the IP address
    Serial.print("IP address: ");
    delay(10);
    Serial.println(WiFi.localIP());
    delay(10);
  }
  
  // Check if a client has connected
  WiFiClient client = server.available();
  if ( ! client ) {
    return;
  }

  //Wait until the client sends some data
  while ( ! client.available () )
  {
    delay (10); // 2019-02-28 reduced from 100
    Serial.print(".");
    if (commas++ > 5) {
      commas = 0;
      Serial.println("Terminating client connecting without reading");
      delay(20);
      client.stop();
      return;
    }  
  }

  client.setTimeout(3000); // 2019-03-19
  Serial.println("new client connect, waiting for data ");
  
  // Read the first line of the request
  String req = client.readStringUntil ('\r');
  client.flush ();
  
  // Match the request
  String s = "";
  // Prepare the response
  if (req.indexOf ("/pwm1") != -1) // first channel
  {
    Serial.println("Command for PWM1 received");     

    int value_index = req.indexOf("/pwm1/");// Here you get the index to split the response string where it says "pwm1"
    String value_string = req.substring(value_index);
    String value = value_string.substring(6, value_string.indexOf("\r"));// Skip first 6 characters, get value
    Serial.println(value);
    
    val = value.toInt();
    
    if (val < 0) // 2021-01-16 value must be 0-100%
      val = 0;
    else if (val > 100)
      val = 100;  
 
    // 2021-01-16
    String ss = "Duty cycle (percent) used is: ";
    ss += String(val);
    if (val > 75) 
      ss += " CAUTION! exceeding fan volatage 5V";
    http_reply_txt(s, ss);
    client.print(s);
    
    Serial.println(ss);

    // dutycycle = (val * 1023) / 100; // Convert to 16-bit unsigned
    dutycycle = (val * 100) / 100; // 23-01-09 corrected for range 100
    
    analogWrite(PWMpin1, dutycycle);
    
  } else if (req.indexOf ("/pwm2") != -1) {
      Serial.println("PWM2 command received");     

      int value_index = req.indexOf("/pwm2/");// Here you get the index to split the response string where it says "pwm1"
      String value_string = req.substring(value_index);
      String value = value_string.substring(6, value_string.indexOf("\r"));// Skip first 6 characters, get value
      Serial.println(value);

      val2 = value.toInt();
    
      if (val2 < 0) // 2019-04-19 value must be 0-100%
        val2 = 0;
      else if (val2 > 100)
        val2 = 100;  

      // 2021-01-16
      String ss = "Duty cycle (percent) used is: ";
      ss += String(val2);
      http_reply_txt(s, ss);
      client.print (s);
    
      Serial.print("Duty cycle (percent) used is: ");
      Serial.println(val2);

      //dutycycle = (val2 * 1023) / 100; // Convert to 16-bit unsigned
      dutycycle = (val2 * 100) / 100; // 23-01-09 corrected for range 100
      analogWrite(PWMpin2, dutycycle);
  } else if (req.indexOf ("/dir1") != -1) {
      Serial.println("DIR1 command received");     

      int value_index = req.indexOf("/dir1/");// Here you get the index to split the response string where it says "pwm1"
      String value_string = req.substring(value_index);
      String value = value_string.substring(6, value_string.indexOf("\r"));// Skip first 6 characters, get value
      Serial.println(value);

      int dir1 = value.toInt();
    
      if (dir1 == 0) { // 2023-01-08 value must be 0-1
        String ss = "Channel 1 direction: reverse. CAUTION! fan will stop";
        Dir1 = LOW;
        analogWrite(PWMpin1, 0);  /* Stop motor */
        digitalWrite(Dirpin1, Dir1);         
        http_reply_txt(s, ss);
        client.print (s);
        Serial.println(ss);
      }
      else if (dir1 == 1) {
        Dir1 = HIGH;  
        String ss = "Channel 1 direction is: forward";
        analogWrite(PWMpin1, 0);  /* Stop motor */
        digitalWrite(Dirpin1, Dir1);         
        http_reply_txt(s, ss);
        client.print (s);
        Serial.println(ss);
      }
      else {
        String ss = "Invalid direction 1 value ";        
        ss += String(dir1);
        ss += " must be 0 or 1";
        http_reply_txt(s, ss);
        client.print (s);
        Serial.println(ss);
      }
  } else if (req.indexOf ("/dir2") != -1) {
      Serial.println("DIR2 command received");     

      int value_index = req.indexOf("/dir2/");// Here you get the index to split the response string where it says "pwm1"
      String value_string = req.substring(value_index);
      String value = value_string.substring(6, value_string.indexOf("\r"));// Skip first 6 characters, get value
      Serial.println(value);

      int dir2 = value.toInt();
    
      if (dir2 == 0) { // 2023-01-08 value must be 0-1
        String ss = "Channel 2 direction is: reverse";
        Dir2 = LOW;
        analogWrite(PWMpin2, 0);  /* Stop motor */
        digitalWrite(Dirpin2, Dir1);         
        http_reply_txt(s, ss);
        client.print (s);
        Serial.println(ss);
      }
      else if (dir2 == 1) {
        Dir2 = HIGH;  
        String ss = "Channel 2 direction is: forward";
        analogWrite(PWMpin2, 0);  /* Stop motor */
        digitalWrite(Dirpin2, Dir1);         
        http_reply_txt(s, ss);
        client.print (s);
        Serial.println(ss);
      }
      else {
        String ss = "Invalid direction 2 value ";        
        ss += String(dir2);
        ss += " must be 0 or 1";
        http_reply_txt(s, ss);
        client.print (s);
        Serial.println(ss);
      }      
  } else if (req.indexOf("/index.html") != -1) {
      // 2021-01-16
      String ss = "Status PWM1 ";
      ss += String(val); 
      ss += "% dir ";
      if (Dir1 == HIGH) 
        ss += "forward ";
      else
        ss += "reverse ";
      ss += " PWM2 ";
      ss += String(val2); 
      ss += "% dir ";
      if (Dir2 == HIGH) 
        ss += "forward ";
      else
        ss += "reverse ";
      http_reply_txt(s, ss);
      client.print (s);
    
      Serial.println("Status command received");
      
  } else {
      Serial.println("invalid request");

      String ss = "Invalid Request";
      http_reply_txt(s, ss);
      client.print (s);
  }

  delay (100); // 2020-01-17 needed because of curl disconnects
  client.stop(); // 2019-02-24
  // Serial.println("Client disonnected");
  delay(10);
}

