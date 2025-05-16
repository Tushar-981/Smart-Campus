Smart Campus System 🚗🏫
Overview
The Smart Campus System is an IoT-based project designed to automate and secure campus entry using smart number plate recognition and motion-based lighting. The system utilizes an embedded ESP-based webpage for uploading vehicle images, which are then processed in the cloud to extract and verify number plates. The result determines whether to grant or deny access, controlling a gate barrier accordingly. Additionally, motion-detected lighting enhances energy efficiency around the entrance area.

Features
📷 Smart Number Plate Recognition

Vehicle images are uploaded via an ESP-hosted embedded web page.
Images are sent to Azure Blob Storage.
A cloud function processes the image to extract the number plate using machine learning.
The extracted number is stored in an Azure SQL Database along with a timestamp.
Based on recognition:
✅ Positive acknowledgement triggers the barrier gate to open.
❌ Negative acknowledgement shows "Access Denied" on an LCD display at the gate.
💡 Motion-Activated Lights

IR sensors detect motion at the gate area.
Lights are activated only when motion is detected, saving energy.
Tech Stack
Component	Description
ESP32	Hosts the embedded web page, handles communication with Azure
Azure Blob Storage	Stores uploaded vehicle images
Azure Function / VM	Performs number plate recognition using image processing
Azure SQL Database	Stores number plate entries with timestamps
IR Sensor	Detects motion near the gate
LCD Display	Displays access status ("Access Granted" or "Access Denied")
Servo Motor	Controls the gate barrier mechanism
