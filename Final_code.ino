#include "esp_camera.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

const char* ssid = "Velxio-GUEST";

// --- Azure VM Configuration ---
const char* serverIP = "00.00.00.000";
const int serverPort = 8000;
const char* apiEndpoint = "/api/v1/process_vehicle";

// --- Hardware Pins ---
const int S1_PIN = 13;
const int S2_PIN = 14;
const int S3_PIN = 15;
const int SERVO_PIN = 2;

Servo gateServo;

// State tracking so we don't spam the API while a car sits on the sensors
bool vehiclePresent = false; 

void sendPhotoToServer();

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Initialize Pins
    pinMode(S1_PIN, INPUT);
    pinMode(S2_PIN, INPUT);
    pinMode(S3_PIN, INPUT);

    // Initialize Servo (Starts closed at 0 degrees)
    gateServo.setPeriodHertz(50);
    gateServo.attach(SERVO_PIN, 500, 2400);
    gateServo.write(0); 

    WiFi.begin(ssid, "", 6);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    // --- Camera Configuration ---
    camera_config_t cfg;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.pin_d0 = 5;
    cfg.pin_d1 = 18;
    cfg.pin_d2 = 19;
    cfg.pin_d3 = 21;
    cfg.pin_d4 = 36;
    cfg.pin_d5 = 39;
    cfg.pin_d6 = 34;
    cfg.pin_d7 = 35;
    cfg.pin_xclk = 0;
    cfg.pin_pclk = 22;
    cfg.pin_vsync = 25;
    cfg.pin_href = 23;
    cfg.pin_sccb_sda = 26;
    cfg.pin_sccb_scl = 27;
    cfg.pin_pwdn  = 32;
    cfg.pin_reset = -1;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size   = FRAMESIZE_SVGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count     = 1;
    cfg.fb_location  = CAMERA_FB_IN_DRAM;
    cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        Serial.println("Camera Init Failed");
        return;
    }
}

void loop() {
    // Read the three sensors
    bool s1 = digitalRead(S1_PIN) == HIGH;
    bool s2 = digitalRead(S2_PIN) == HIGH;
    bool s3 = digitalRead(S3_PIN) == HIGH;

    // The Detection Logic: (S1 AND S2) OR (S2 AND S3)
    bool triggerCondition = (s1 && s2) || (s2 && s3);

    if (triggerCondition && !vehiclePresent) {
        // A new vehicle just hit the sensors
        vehiclePresent = true;
        Serial.println("\n[INFO] Vehicle detected by sensors! Snapping photo...");
        sendPhotoToServer();
        
    } else if (!triggerCondition && vehiclePresent) {
        // Vehicle has fully cleared all sensors, reset the system
        vehiclePresent = false;
        Serial.println("[INFO] Vehicle cleared the gate area. System reset.");
    }

    delay(100); // Small delay to prevent CPU hogging
}

void sendPhotoToServer() {
    camera_fb_t* fb = esp_camera_fb_get();
    
    // Quick sensor setting fix for SVGA stability
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_SVGA);

    if (!fb) {
        Serial.println("Camera capture failed!");
        return;
    }

    WiFiClient client;
    Serial.printf("Connecting to Azure VM at %s:%d...\n", serverIP, serverPort);

    if (client.connect(serverIP, serverPort)) {
        Serial.println("Connected to Cloud Backend!");

        String boundary = "----ESP32Boundary";
        String head =
            "--" + boundary +
            "\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
            "Content-Type: image/jpeg\r\n\r\n";
        String tail = "\r\n--" + boundary + "--\r\n";

        uint32_t totalLen = head.length() + fb->len + tail.length();

        client.print("POST " + String(apiEndpoint) + " HTTP/1.1\r\n");
        client.print("Host: " + String(serverIP) + "\r\n");
        client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
        client.print("Content-Length: " + String(totalLen) + "\r\n");
        client.print("Connection: close\r\n\r\n");

        client.print(head);

        // --- CHUNKING LOGIC TO FIX CORRUPT JPEG ERROR ---
        uint8_t *fbBuf = fb->buf;
        size_t fbLen = fb->len;
        size_t chunkSize = 1024; // Send 1KB at a time
        
        for (size_t n = 0; n < fbLen; n += chunkSize) {
            if (n + chunkSize < fbLen) {
                client.write(fbBuf, chunkSize);
                fbBuf += chunkSize;
            } else {
                // Send the final remaining bytes
                client.write(fbBuf, fbLen - n);
            }
            delay(10);
        }
        // ------------------------------------------------

        client.print(tail);
        // Parse HTTP Response & JSON Payload
        Serial.println("Image payload sent. Awaiting AI response...");

        // Parse HTTP Response & JSON Payload
        String responseBody = "";
        bool headersPassed = false;
        unsigned long timeout = millis();

        // Keep reading as long as we are connected OR data is sitting in the buffer
        while ((client.connected() || client.available()) && millis() - timeout < 15000) {
            if (client.available()) {
                timeout = millis();
                String line = client.readStringUntil('\n');
                line.trim(); // Strip hidden \r and whitespace

                if (!headersPassed) {
                    if (line.length() == 0) {
                        headersPassed = true; // A completely blank line means headers are done!
                    } else {
                        Serial.print("[HEADER] ");
                        Serial.println(line);
                    }
                } else {
                    // We are past the headers, so this is our JSON payload
                    responseBody += line;
                }
            }
        }
        
        // Serial.println("Raw Server JSON: " + responseBody);

        // --- Execute JSON Actuation Logic ---
        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, responseBody);

        if (!error) {
            String gateAction = doc["gate_action"] | ""; // Grab the decision from Python
            
            if (gateAction == "CLOSED") {
                Serial.println("[ACTUATION] Authentication passed! Opening Gate Barrier...");
                gateServo.write(90); // Open gate 90 degrees
                
                delay(7000);         // Hold gate open for 7 seconds to let car pass
                
                gateServo.write(0);  // Close gate
                Serial.println("[ACTUATION] Gate Closed.");
            } else {
                Serial.println("[ACTUATION] Authentication failed or gate_action was not OPEN.");
            }
        } else {
            Serial.print("JSON Parse failed: ");
            Serial.println(error.f_str());
        }

    } else {
        Serial.println("❌ Connection to Azure failed!");
    }

    client.stop();
    esp_camera_fb_return(fb); // Free memory buffer
}
