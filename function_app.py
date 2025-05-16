import azure.functions as func
import logging
import cv2
import numpy as np
import easyocr
import imutils
import pyodbc
from azure.storage.blob import BlobServiceClient
import os

app = func.FunctionApp()

@app.blob_trigger(arg_name="myblob", path="photos", connection="iotproject353843_STORAGE")
def NumberPlate(myblob: func.InputStream):
    logging.info(f"Triggered by blob: {myblob.name}, Size: {myblob.length} bytes")

    blob_bytes = myblob.read()
    npimg = np.frombuffer(blob_bytes, np.uint8)
    img = cv2.imdecode(npimg, cv2.IMREAD_COLOR)

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    bfilter = cv2.bilateralFilter(gray, 11, 17, 17)
    edged = cv2.Canny(bfilter, 30, 100)

    keypoints = cv2.findContours(edged.copy(), cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
    contours = imutils.grab_contours(keypoints)
    contours = sorted(contours, key=cv2.contourArea, reverse=True)[:10]

    location = None
    for contour in contours:
        approx = cv2.approxPolyDP(contour, 10, True)
        if len(approx) == 4:
            location = approx
            break

    if location is None:
        logging.warning("No license plate-like contour detected.")
        write_result_to_blob(0)
        return

    mask = np.zeros(gray.shape, np.uint8)
    new_image = cv2.drawContours(mask, [location], 0, 255, -1)
    new_image = cv2.bitwise_and(img, img, mask=mask)

    (x, y) = np.where(mask == 255)
    (x1, y1) = (np.min(x), np.min(y))
    (x2, y2) = (np.max(x), np.max(y))
    cropped_image = gray[x1:x2 + 1, y1:y2 + 1]

    reader = easyocr.Reader(['en'], gpu=False)
    result = reader.readtext(cropped_image)

    if result:
        for detection in result:
            text = detection[1]
            if len(text.strip()) >= 8:
                logging.info(f"Detected text: {text}")
                insert_to_db(myblob.name, text)
                write_result_to_blob(1)
                return
        logging.info("All detected text too short.")
        insert_to_db(myblob.name, "")
        write_result_to_blob(0)
    else:
        logging.info("No text detected on license plate.")
        insert_to_db(myblob.name, "")
        write_result_to_blob(0)


def insert_to_db(blob_name, detected_text):
    from datetime import datetime
    import pytz
    import pyodbc
    import logging

    server = 'mysql353843.database.windows.net'
    database = 'PlateDB'
    username = 'mysql353843'
    password = '353843IoT'
    driver = '{ODBC Driver 17 for SQL Server}'

    # Correctly get IST time
    ist = pytz.timezone('Asia/Kolkata')
    ist_time = datetime.now(ist)

    try:
        with pyodbc.connect(f'DRIVER={driver};SERVER={server};PORT=1433;DATABASE={database};UID={username};PWD={password}') as conn:
            with conn.cursor() as cursor:
                cursor.execute(
                    "INSERT INTO NumberPlateLogs (BlobName, DetectedText, Timestamp) VALUES (?, ?, ?)",
                    (blob_name, detected_text, ist_time)
                )
                conn.commit()
                logging.info("Data inserted into SQL successfully.")
    except Exception as e:
        logging.error(f"Error inserting to DB: {e}")




def write_result_to_blob(result):
    try:
        connect_str = os.getenv("AzureWebJobsStorage")  # Same conn string as in local.settings.json
        blob_service_client = BlobServiceClient.from_connection_string(connect_str)
        container_client = blob_service_client.get_container_client("txtfile")
        blob_client = container_client.get_blob_client("result.txt")
        blob_client.upload_blob(str(result), overwrite=True)
        logging.info(f"Wrote result {result} to result.txt in txtfile container.")
    except Exception as e:
        logging.error(f"Error writing result to blob: {e}")
