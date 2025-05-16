#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <ESP32Servo.h>

Servo myServo;

// ====== WiFi Credentials ======
const char* ssid = "ABC";
const char* password = "23456789";

// ====== GPIO Pins ======
const int motionPin = 2;
const int streetLight1 = 25;
const int streetLight2 = 26;
const int green = 32;
const int red = 33;
int detection = 0; 
void uploadToAzure(uint8_t* data, size_t len);
int getDetectionResult();
void invalid_number();



LiquidCrystal_PCF8574 lcd(0x27); // Use the correct I2C addres

// ====== Azure Storage Configuration ======
#define MAX_IMAGE_SIZE 70000
uint8_t imageBuffer[MAX_IMAGE_SIZE];
size_t imageSize = 0;

const char* azurePhotoBaseURL = "https://iotproject353843.blob.core.windows.net/photos";
const char* azureTxtFileURL = "https://iotproject353843.blob.core.windows.net/txtfile/result.txt";

// ====== Two Separate SAS Tokens ======
const char* sasTokenPhotos = "?sp=rcw&st=2025-04-19T08:55:57Z&se=2025-04-23T16:55:57Z&spr=https&sv=2024-11-04&sr=c&sig=3pSW%2BCOXJPkMZtdbVJtQYe2J8v8C76ntoeIqw5XjfcE%3D";
const char* sasTokenTxtfile = "?sp=r&st=2025-04-19T21:08:28Z&se=2025-04-23T05:08:28Z&spr=https&sv=2024-11-04&sr=c&sig=p1dfqM%2FSyFDNFvbfBrItU%2Bk20ilHdx9s4fyTPfs2%2FE8%3D";

// ====== Web Server ======
WebServer server(80);

// ====== Variables to control reading of result.txt ======
unsigned long lastUploadTime = 0;
bool imageUploaded = false;
bool checkDone = false;  // Flag to track if result.txt has been checked

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>Photo Upload</title></head>
<body>
  <h2>Take a Photo</h2>
  <form id="uploadForm" method="POST" action="/upload" enctype="multipart/form-data">
    <input type="file" name="photo" accept="image/*" capture="environment" onchange="document.getElementById('uploadForm').submit();">
  </form>
</body>
</html>
)rawliteral";

// ====== Handle Root Web Page ======
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

// ====== Handle Image Upload from Web Interface ======
void handleUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    imageSize = 0;
    Serial.println("Receiving image...");
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (imageSize + upload.currentSize <= MAX_IMAGE_SIZE) {
      memcpy(&imageBuffer[imageSize], upload.buf, upload.currentSize);
      imageSize += upload.currentSize;
    } else {
      Serial.println("Image too large!");
      server.send(500, "text/plain", "Image too large");
      return;
    }
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("Upload complete. Size: %d bytes\n", imageSize);
    server.send(200, "text/plain", "Image received. Uploading to Azure...");
    uploadToAzure(imageBuffer, imageSize);
  }
}

// ====== Upload Photo to Azure (WRITE to photos container) ======
void uploadToAzure(uint8_t* data, size_t len) {
  WiFiClientSecure client;
  client.setInsecure();  // Only for testing, insecure

  String filename = "/photo_" + String(millis()) + ".jpg";
  String fullURL = String(azurePhotoBaseURL) + filename + String(sasTokenPhotos);

  Serial.println("Uploading to: " + fullURL);

  HTTPClient http;
  http.begin(client, fullURL);
  http.addHeader("x-ms-blob-type", "BlockBlob");
  http.addHeader("Content-Type", "image/jpeg");

  int response = http.sendRequest("PUT", data, len);

  if (response > 0) {
    Serial.printf("✅ Azure responded with code: %d\n", response);
    lastUploadTime = millis(); // Store the upload time
    imageUploaded = true; // Mark image as uploaded
    checkDone = false;  // Reset the check flag to allow checking result
  } else {
    Serial.printf("❌ Upload failed: %s\n", http.errorToString(response).c_str());
  }
  delay(15000);
  detection = getDetectionResult();
  http.end();
}

// ====== Check Result File from Azure (READ from txtfile/result.txt) ======
int getDetectionResult() {
  if (imageUploaded && !checkDone) { // 10 seconds after upload
    WiFiClientSecure client;
    client.setInsecure();  // Only for testing

    HTTPClient http;
    String fullURL = String(azureTxtFileURL) + String(sasTokenTxtfile);

    http.begin(client, fullURL);

    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      payload.trim();
      Serial.println("📄 result.txt content: " + payload);

      if (payload == "1") {
        checkDone = true; // Mark as checked
        return 1;
      }
    } else {
      Serial.printf("❌ Error reading result.txt: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
  return 0;
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected! IP: " + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/upload", HTTP_POST, []() { server.send(200); }, handleUpload);
  server.begin();

  pinMode(motionPin, INPUT);
  pinMode(streetLight1, OUTPUT);
  pinMode(streetLight2, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(red, OUTPUT);

  myServo.attach(13);
  Wire.begin();

  lcd.begin(16, 2);  // 16 characters, 2 lines
  lcd.setBacklight(255);

}


void invalid_number(){
  digitalWrite(red,HIGH);
  digitalWrite(green,LOW);
  myServo.write(5);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("STOP");
  detection=0;
}

void valid_number(){
  digitalWrite(green,HIGH);
  digitalWrite(red,LOW);
  myServo.write(60);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GO AHEAD");
  delay(4000);
  invalid_number();
}
// ====== Loop ======
void loop() {
  server.handleClient();
  if (detection == 1) {
    valid_number();
  } else {
    invalid_number();
  }
  int a = digitalRead(motionPin);
if (a) {
  digitalWrite(25, HIGH);
  digitalWrite(26, HIGH);
  Serial.println("🚶 Motion detected - Lights ON");
} else {
  digitalWrite(25, LOW);
  digitalWrite(26, LOW);
  Serial.println("😴 No motion - Lights OFF");
}
  delay(1000);
}