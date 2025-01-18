#include <SPI.h>
#include <FS.h>
#include <TFT_eSPI.h>
#include <JPEGDecoder.h>
#include <string.h>
TFT_eSPI tft = TFT_eSPI();
#include <WiFi.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include <WiFiMulti.h>

#include <HTTPClient.h>

const char WIFI_SSID[] = "WIFI TANG 4";     // Change this to your WiFi SSID
const char WIFI_PASSWORD[] = "wifitang4@";  // Change this to your WiFi password



const int PUBLISH_INTERVAL = 5000;  // 5 seconds
// Đây là tên file dùng để lưu trữ dữ liệu hiệu chuẩn
#define CALIBRATION_FILE "/TouchCalData1"
#define REPEAT_CAL false  // true nếu muốn mỗi lần reset phải hiệu chỉnh

WiFiClient network;
MQTTClient mqtt = MQTTClient(256);

unsigned long lastPublishTime = 0;

#define MAX_FILES 20  // Số file tối đa lưu trong danh sách

String fileNames[MAX_FILES];  // Mảng lưu danh sách tên file ảnh
int fileCount = 0;            // Số lượng file ảnh tìm thấy
int currentFileIndex = 0;     // Vị trí file hiện tại trong danh sách

uint16_t t_x = 0, t_y = 0;
uint16_t last_t_x = 0, last_t_y = 0;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  // Khởi tạo màn hình TFT
  tft.begin();
  tft.setRotation(1);

  digitalWrite(22, HIGH);  // Touch controller chip select (if used)
  digitalWrite(15, HIGH);  // TFT screen chip select
  digitalWrite(5, HIGH);   // SD card chips select, must use GPIO 5 (ESP32 SS)

  tft.begin();
  if (!SD.begin(5, tft.getSPIinstance())) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  File root = SD.open("/");
  listImages(root);
  Serial.print("Found ");
  Serial.print(fileCount);
  Serial.println(" image files.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  // downloadImage("https://th.bing.com/th/id/OIP.YxGhw3I-iJ0uVuqpSxePaAAAAA?rs=1&pid=ImgDetMain", "/downloaded.jpg");
  


  // // Set all chip selects high to avoid bus contention during initialisation of each peripheral
 
}
#define SWIPE_THRESHOLD 60  // Ngưỡng vuốt tối thiểu (px)
#define SWIPE_DELAY 500     // Thời gian tối thiểu giữa các lần vuốt (ms)

unsigned long lastSwipeTime = 0;  // Lưu thời gian lần vuốt trước
bool startTouch = false;
uint16_t startX = 0;
void loop() {
  // mqtt.loop();
  tft.setRotation(1);
  // Hiển thị ảnh hiện tại (chỉ vẽ lại khi cần)
  static int lastFileIndex = -1;
  if (fileCount > 0 && currentFileIndex != lastFileIndex) {
    tft.fillScreen(TFT_BLACK);
    Serial.print("Displaying: ");
    Serial.println(fileNames[currentFileIndex]);
    drawSdJpeg(("/" + fileNames[currentFileIndex]).c_str(), 0, 0);
    lastFileIndex = currentFileIndex;
  }
  
  
  bool pressed = tft.getTouch(&t_x, &t_y);
  if (pressed) {
    if (startTouch == false) { 
      startTouch = true;
      startX = t_x;  // Lưu tọa độ ban đầu khi chạm
    }

    if (millis() - lastSwipeTime > 300) {  // Chống nhiễu thao tác vuốt
      int swipeDistance = t_x - startX; 

      if (abs(swipeDistance) > 50) {  // Chỉ xử lý khi vuốt đủ xa
        if (swipeDistance > 0) { 
          Serial.println("Vuốt phải -> Next Image");
          currentFileIndex = (currentFileIndex + 1) % fileCount;
        } else {
          Serial.println("Vuốt trái -> Previous Image");
          currentFileIndex = (currentFileIndex - 1 + fileCount) % fileCount;
        }
        lastSwipeTime = millis();  // Cập nhật thời gian vuốt
      }
    }
  } else {
    startTouch = false;  // Reset khi thả tay
  }
  
}

void listImages(File dir) {
  fileCount = 0;  // Reset số lượng file tìm thấy
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;  // Hết file

    String fileName = entry.name();
    if (fileName.endsWith(".jpg") || fileName.endsWith(".JPG")) {
      if (fileCount < MAX_FILES) {
        fileNames[fileCount] = fileName;
        fileCount++;
      }
    }
    entry.close();
  }
}
void drawSdJpeg(const char *filename, int xpos, int ypos) {
  File jpegFile = SD.open(filename, FILE_READ);
  if (!jpegFile) {
    Serial.print("ERROR: File \"");
    Serial.print(filename);
    Serial.println("\" not found!");
    return;
  }
  Serial.println("===========================");
  Serial.print("Drawing file: ");
  Serial.println(filename);
  Serial.println("===========================");

  bool decoded = JpegDec.decodeSdFile(jpegFile);
  if (decoded) {
    // render the image onto the screen at given coordinates
    jpegRender(xpos, ypos);
  } else {
    Serial.println("Jpeg file format not supported!");
  }
}
void jpegRender(int xpos, int ypos) {
  uint16_t *pImg;
  uint16_t mcu_w = JpegDec.MCUWidth;
  uint16_t mcu_h = JpegDec.MCUHeight;
  uint32_t max_x = JpegDec.width;
  uint32_t max_y = JpegDec.height;

  bool swapBytes = tft.getSwapBytes();
  tft.setSwapBytes(true);

  // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
  // Typically these MCUs are 16x16 pixel blocks
  // Determine the width and height of the right and bottom edge image blocks
  uint32_t min_w = jpg_min(mcu_w, max_x % mcu_w);
  uint32_t min_h = jpg_min(mcu_h, max_y % mcu_h);

  // save the current image block size
  uint32_t win_w = mcu_w;
  uint32_t win_h = mcu_h;

  // record the current time so we can measure how long it takes to draw an image
  uint32_t drawTime = millis();

  // save the coordinate of the right and bottom edges to assist image cropping
  // to the screen size
  max_x += xpos;
  max_y += ypos;

  // Fetch data from the file, decode and display
  while (JpegDec.read()) {  // While there is more data in the file
    pImg = JpegDec.pImage;  // Decode a MCU (Minimum Coding Unit, typically a 8x8 or 16x16 pixel block)

    // Calculate coordinates of top left corner of current MCU
    int mcu_x = JpegDec.MCUx * mcu_w + xpos;
    int mcu_y = JpegDec.MCUy * mcu_h + ypos;

    // check if the image block size needs to be changed for the right edge
    if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
    else win_w = min_w;

    // check if the image block size needs to be changed for the bottom edge
    if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
    else win_h = min_h;

    // copy pixels into a contiguous block
    if (win_w != mcu_w) {
      uint16_t *cImg;
      int p = 0;
      cImg = pImg + win_w;
      for (int h = 1; h < win_h; h++) {
        p += mcu_w;
        for (int w = 0; w < win_w; w++) {
          *cImg = *(pImg + w + p);
          cImg++;
        }
      }
    }

    // calculate how many pixels must be drawn
    uint32_t mcu_pixels = win_w * win_h;

    // draw image MCU block only if it will fit on the screen
    if ((mcu_x + win_w) <= tft.width() && (mcu_y + win_h) <= tft.height())
      tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
    else if ((mcu_y + win_h) >= tft.height())
      JpegDec.abort();  // Image has run off bottom of screen so abort decoding
  }

  tft.setSwapBytes(swapBytes);

  showTime(millis() - drawTime);  // These lines are for sketch testing only
}
void showTime(uint32_t msTime) {
  tft.setCursor(0, 0);
  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print(F(" JPEG drawn in "));
  tft.print(msTime);
  tft.println(F(" ms "));
  Serial.print(F(" JPEG drawn in "));
  Serial.print(msTime);
  Serial.println(F(" ms "));
}

void downloadImage(const char *url, const char *filePath) {
  HTTPClient http;
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    File file = SD.open(filePath, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing!");
      return;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[512];  // Buffer đọc dữ liệu
    int bytesRead;

    while ((bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0) {
      file.write(buffer, bytesRead);
    }

    file.close();
    Serial.println("Download complete!");
    delay(2000);
    ESP.restart();
  } else {
    Serial.printf("HTTP request failed, error: %d\n", httpCode);
  }

  http.end();
}

void touch_calibrate()
{
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // check file system exists
  if (!SPIFFS.begin()) {
    Serial.println("formatting file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL)
    {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    }
    else
    {
      File f = SPIFFS.open(CALIBRATION_FILE, "r");
      if (f) {
        if (f.readBytes((char *)calData, 14) == 14)
          calDataOK = 1;
        f.close();
      }
    }
  }

  if (calDataOK && !REPEAT_CAL) {
    // calibration data valid
    tft.setTouch(calData);
  } else {
    // data not valid so recalibrate
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Touch corners as indicated");

    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    // store data
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calData, 14);
      f.close();
    }
  }
}
