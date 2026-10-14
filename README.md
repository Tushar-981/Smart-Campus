# 🏫 Smart Campus Automated Gate Access System

![Version](https://img.shields.io/badge/version-3.0-blue.svg)
![Python](https://img.shields.io/badge/Python-3.9%2B-blue)
![C++](https://img.shields.io/badge/C%2B%2B-ESP32-orange)
![Docker](https://img.shields.io/badge/Docker-Enabled-blue)
![Azure](https://img.shields.io/badge/Deployed_on-Azure-0078D4)

An end-to-end IoT and Computer Vision pipeline designed to automate vehicle access at campus security gates. This project bridges resource-constrained edge hardware (ESP32-CAM) with a high-performance, cloud-native AI backend (FastAPI, YOLO, PaddleOCR) hosted on Microsoft Azure.

## 🌟 Key Features

- **Edge-Triggered Capture:** Physical sensor logic ensures the camera only triggers when a vehicle is properly positioned.
- **Optimized Network Transmission:** Custom 1KB HTTP chunking algorithm in C++ prevents TCP buffer overflows when sending high-resolution images over WiFi.
- **Real-Time AI Inference:** Utilizes YOLO for rapid vehicle/plate localization and PaddleOCR for highly accurate alphanumeric text extraction.
- **OCR Auto-Correction:** Built-in heuristic engine to correct common AI misreads (e.g., confusing 'O' and '0') and validate regional state codes.
- **Digital Audit Trail:** Asynchronously streams visual evidence to Azure Blob Storage and logs authentication metadata to an Azure MySQL database.
- **Physical Actuation:** Parses cloud JSON responses directly on the microcontroller to actuate a servo motor gate barrier.

## 🛠️ Technology Stack

**Edge Hardware (IoT)**
- ESP32-CAM Microcontroller
- Arduino (C++) / ArduinoJson
- IR/Ultrasonic Proximity Sensors & Servo Motor

**Cloud AI Backend**
- Python 3 / FastAPI / Uvicorn
- Ultralytics YOLO (Object Detection)
- PaddleOCR (Optical Character Recognition)
- OpenCV (Image alignment and skew correction)

**Infrastructure & Data**
- Microsoft Azure Virtual Machine (Ubuntu)
- Docker (Containerization)
- Azure Blob Storage (Image retention)
- Azure Database for MySQL (Metadata logging)

---

## 🏗️ System Architecture

1. **Detection:** Sensors detect a vehicle at the gate `(S1 && S2) || (S2 && S3)`.
2. **Capture & Transmit:** ESP32-CAM captures a frame and sends a chunked `multipart/form-data` POST request to the Azure VM.
3. **AI Processing:** 
   - YOLO isolates and crops the license plate.
   - OpenCV aligns the cropped plate horizontally.
   - PaddleOCR extracts the text.
4. **Cloud Logging:** The image is uploaded to Azure Blob Storage, and the plate text/timestamp is inserted into Azure MySQL.
5. **Actuation:** FastAPI returns a JSON payload (`{"gate_action": "OPEN"}`). The ESP32 parses this and actuates the physical servo motor.

---

## 🚀 Setup and Deployment

### 1. Cloud Backend (Docker on Azure VM)

Clone the repository to your server:
```bash
git clone [https://github.com/Tushar-981/Smart-Campus.git](https://github.com/Tushar-981/Smart-Campus.git)
cd Smart-Campus
cd Smart-Campus-System
```

Create a vars.env file in the root directory to hold your Azure credentials securely:
```bash
DB_HOST=your-azure-mysql-server.mysql.database.azure.com
DB_USER=your_username
DB_PASSWORD=your_secure_password
DB_NAME=your_database_name
AZURE_STORAGE_CONNECTION_STRING=DefaultEndpointsProtocol=https;AccountName=...
```

Build and run the container in detached mode with automated restarts:
```bash
sudo docker build -t smart-campus-api .
sudo docker run -d --name campus-backend --env-file vars.env -p 8000:8000 --restart unless-stopped smart-campus-api
```
### 2. Edge Hardware (ESP32-CAM)
Open ESP32_Gate_Controller.ino in the Arduino IDE.

Update your WiFi credentials and the Static IP of your Azure server:
```bash
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* serverIP = "00.00.00.000"; // Replace with your Azure VM IP
```
### 3. Flash the firmware to the ESP32-CAM board.

## API reference
```bash
POST /api/v1/process_vehicle
```
Request Body: multipart/form-data containing the image file (capture.jpg).

Response :-
```JSON
{
  "status": "success",
  "plate_number": "HR39F5014",
  "gate_action": "OPEN",
  "message": "Welcome to Campus",
  "image_url": "[https://xyz.blob.core.windows.net/gate-images/car_cee750a7...jpg](https://xyz.blob.core.windows.net/gate-images/car_cee750a7...jpg)"
}
```

## Future Scope
1. Migrate single Docker container to Azure Kubernetes Service (AKS) for multi-gate concurrent load balancing.
2. Integrate IR-sensitive lenses on the ESP32-CAM for nighttime recognition accuracy.
3. Develop a React/Next.js frontend web dashboard for security guards to monitor MySQL logs and Blob images in real-time.
