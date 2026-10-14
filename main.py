from fastapi import FastAPI, UploadFile, File, HTTPException
from fastapi.responses import JSONResponse
import uvicorn
from ultralytics import YOLO
from paddleocr import PaddleOCR
import torch
import numpy as np
import cv2
import re
import uuid
from datetime import datetime
import os
import logging
import traceback
import math

# Cloud Drivers
from azure.storage.blob import BlobServiceClient
import mysql.connector

app = FastAPI(
    title="Smart Campus ALPR Engine",
    description="Production Azure Backend for ESP32 Gate Control",
    version="3.0"
)

print("Loading AI Models...")
logging.getLogger('ppocr').setLevel(logging.ERROR)

# Load Models (Adjust paths for your VM directory structure)
model = YOLO('./best.pt') 
reader = PaddleOCR(use_angle_cls=True, lang='en', show_log=False, use_gpu=False)

VALID_STATE_CODES = {
  "AN","AP","AR","AS","BR","CG","CH","DD","DL","DN",
  "GA","GJ","HP","HR","JH","JK","KA","KL","LA","LD",
  "MH","ML","MN","MP","MZ","NL","OD","PB","PY","RJ",
  "SK","TN","TR","TS","UK","UP","WB"
}

OCR_MAP = {
    '0': ['0', 'O', 'D'], 'O': ['O', '0', 'D'], 'D': ['D', '0', 'O'],
    '8': ['8', 'B'], 'B': ['B', '8'], '1': ['1', 'I'], 'I': ['I', '1'],
    '2': ['2', 'Z'], 'Z': ['Z', '2'], '5': ['5', 'S'], 'S': ['S', '5'],
    '6': ['6', 'G'], 'G': ['G', '6'], '4': ['4', 'A'], 'A': ['A', '4'],
    'X': ['K','H']
}

# --- Azure Configuration ---

AZURE_CONNECTION_STRING = os.getenv("AZURE_STORAGE_CONNECTION_STRING")
CONTAINER_NAME = "gate-images"
blob_service_client = BlobServiceClient.from_connection_string(AZURE_CONNECTION_STRING)

DB_CONFIG = {
    "host": os.getenv("DB_HOST"), 
    "user": os.getenv("DB_USER"),
    "password": os.getenv("DB_PASSWORD"),
    "database": os.getenv("DB_NAME"),
    "ssl_ca": "/etc/ssl/certs/ca-certificates.crt", # Keep this as is for Azure SSL
    "ssl_disabled": False
}

def correct_state_code(code):
    code = code.upper()
    if code in VALID_STATE_CODES:
        return code
    first_chars = OCR_MAP.get(code[0], [code[0]])
    second_chars = OCR_MAP.get(code[1], [code[1]])
    for c1 in first_chars:
        for c2 in second_chars:
            candidate = c1 + c2
            if candidate in VALID_STATE_CODES:
                return candidate
    return code

def align_plate(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    edges = cv2.Canny(gray, 50, 150, apertureSize=3)
    lines = cv2.HoughLinesP(edges, 1, math.pi/180, threshold=50, minLineLength=40, maxLineGap=10)
    angle = 0
    if lines is not None:
        angles = []
        for line in lines:
            x1, y1, x2, y2 = line[0]
            if x2 - x1 != 0:
                a = math.degrees(math.atan((y2 - y1) / (x2 - x1)))
                if -30 < a < 30:
                    angles.append(a)
        if len(angles) > 0:
            angle = np.median(angles)
    if angle != 0:
        (h, w) = img.shape[:2]
        center = (w // 2, h // 2)
        M = cv2.getRotationMatrix2D(center, angle, 1.0)
        return cv2.warpAffine(img, M, (w, h), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
    return img

def clean_indian_plate(ocr_results):
    ignore_words = ["HERO", "JERO", "HONDA", "BAJAJ", "TVS", "YAMAHA", "SUZUKI", "ROYAL", "ENFIELD", "IND", "INDIA"]
    raw_text = ""
    for detection in ocr_results:
        if detection[2] < 0.70:
            continue
        text = detection[1].upper()
        clean_chunk = re.sub(r'[^A-Z0-9]', '', text)
        if clean_chunk not in ignore_words:
            raw_text += clean_chunk
    if len(raw_text) >= 8:
        return correct_state_code(raw_text[:2]) + raw_text[2:4] + raw_text[4:]
    return raw_text

@app.post("/api/v1/process_vehicle")
async def process_vehicle(file: UploadFile = File(...)):
    print("📸 [SERVER] Received image bytes, starting AI inference...")
    try:
        if not file.filename:
            raise HTTPException(status_code=400, detail="No file uploaded")

        contents = await file.read()
        nparr = np.frombuffer(contents, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
        
        if img is None:
            raise HTTPException(status_code=400, detail="Invalid image format received")

        # 1. YOLO Inference
        results = model.predict(source=img, conf=0.4, verbose=False)
        final_plate = "UNKNOWN"
        gate_action = "CLOSED"
        message = "No plate found"

        if len(results[0].boxes) > 0:
            x1, y1, x2, y2 = map(int, results[0].boxes[0].xyxy[0].tolist())
            cropped_plate = img[y1:y2, x1:x2]

            h, w = cropped_plate.shape[:2]
            if w > 400:
                scale = 400 / w
                cropped_plate = cv2.resize(cropped_plate, (int(w * scale), int(h * scale)))

            aligned_plate = align_plate(cropped_plate)

            # 2. PaddleOCR Extraction
            paddle_results = reader.ocr(aligned_plate, cls=True)
            ocr_results = []
            if paddle_results and paddle_results[0] is not None:
                for line in paddle_results[0]:
                    ocr_results.append((line[0], line[1][0], line[1][1]))

            final_plate = clean_indian_plate(ocr_results)
            print(f"✅ Extracted Plate: {final_plate}")
            
            if len(final_plate) >= 5:
                gate_action = "OPEN"
                message = "Welcome to Campus"

        # 3. Stream Upload Image directly to Azure Blob Storage
        unique_filename = f"car_{uuid.uuid4().hex}.jpg"
        cloud_url = None
        try:
            # Convert OpenCV frame back to raw JPEG bytes to send to cloud
            _, encoded_img = cv2.imencode('.jpg', img)
            image_bytes = encoded_img.tobytes()
            
            blob_client = blob_service_client.get_blob_client(container=CONTAINER_NAME, blob=unique_filename)
            blob_client.upload_blob(image_bytes, overwrite=True)
            cloud_url = blob_client.url
            print(f"☁️ Cloud Image Saved: {cloud_url}")
        except Exception as azure_err:
            print(f"❌ Azure Storage Upload Failed: {azure_err}")

        # 4. Log Metadata Into Azure Database for MySQL
        try:
            conn = mysql.connector.connect(**DB_CONFIG)
            cursor = conn.cursor()
            sql = """
                INSERT INTO vehicle_logs (plate_number, image_url, timestamp, gate_status) 
                VALUES (%s, %s, %s, %s)
            """
            values = (final_plate, cloud_url, datetime.now(), gate_action)
            cursor.execute(sql, values)
            conn.commit()
            print("💾 Metadata written to Cloud Database successfully.")
        except mysql.connector.Error as db_err:
            print(f"❌ Azure MySQL Insert Failure: {db_err}")
        finally:
            if 'conn' in locals() and conn.is_connected():
                cursor.close()
                conn.close()

        # 5. Return JSON to Edge Hardware
        return JSONResponse(content={
            "status": "success" if gate_action == "OPEN" else "failed",
            "plate_number": final_plate,
            "gate_action": gate_action,
            "message": message,
            "image_url": cloud_url
        })

    except Exception as e:
        print("\n❌ FASTAPI PIPELINE CRASHED:")
        traceback.print_exc()
        return JSONResponse(content={"status": "error", "message": str(e)}, status_code=500)

if __name__ == "__main__":
    # Standard Uvicorn initialization loop for standalone Virtual Machine server
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=False)
