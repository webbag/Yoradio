#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "config.h"

// ===== KONFIGURACJA PINÓW =====
#define TFT_CS 5
#define TFT_DC 4
#define TFT_RST 6
#define TFT_MOSI 17
#define TFT_SCK 18
#define TFT_MISO 16

#define I2S_BCK 42
#define I2S_LCK 40
#define I2S_DIN 41

#define ENC_A 45
#define ENC_B 48
#define ENC_KEY 47

// ===== OBIEKTY =====
Audio audio;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Definicje stacji radiowych
struct Station {
    const char* name;
    const char* url;
};

const Station stations[] = {
    {"RMF FM", "http://195.150.20.242:8000/rmf_fm"},
    {"SomaFM Groove Salad", "http://ice5.somafm.com/groovesalad-128-mp3"},
    {"SomaFM Beat Blender", "http://ice5.somafm.com/beatblender-128-mp3"},
    {"SomaFM Folk Forward", "http://ice5.somafm.com/folkfwd-128-mp3"},
    {"Radio Nowy Swiat", "http://stream.nowyswiat.online/mp3"},
    {"Radio ZET", "http://zet090-02.cdn.eurozet.pl:8404/"}
};
const int numStations = sizeof(stations) / sizeof(stations[0]);
int currentStationIndex = 0;

// Zmienne sterujące
int volume = 10;
int lastClk = HIGH;
int lastButtonState = HIGH;
unsigned long lastPressTime = 0;

// ===== NOWY SYSTEM CALLBACKÓW (v3.4.4) =====
void my_audio_info(Audio::msg_t m) {
    Serial.printf("AUDIO_EVENT: %s: %s\n", m.s, m.msg);

    switch (m.e) {
        case Audio::evt_streamtitle:
            tft.fillRect(0, 100, 320, 60, ILI9341_BLACK); // Czyszczenie obszaru tytułu
            tft.setCursor(10, 100);
            tft.setTextSize(2);
            tft.setTextColor(ILI9341_YELLOW);
            tft.println(m.msg);
            break;
        case Audio::evt_name:
            tft.fillRect(0, 40, 320, 40, ILI9341_BLACK); // Czyszczenie obszaru nazwy stacji
            tft.setCursor(10, 40);
            tft.setTextSize(2);
            tft.setTextColor(ILI9341_GREEN);
            tft.println(m.msg);
            break;
        case Audio::evt_bitrate:
            tft.fillRect(0, 180, 320, 20, ILI9341_BLACK);
            tft.setCursor(10, 180);
            tft.setTextSize(1);
            tft.setTextColor(ILI9341_CYAN);
            tft.printf("Bitrate: %s", m.msg);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Czas dla USB w ESP32-S3 [4]

    // 1. REJESTRACJA CALLBACKA - KLUCZ DO METADANYCH W 3.4.4
    Audio::audio_info_callback = my_audio_info;

    // 2. Inicjalizacja Ekranu
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.println("Radio Startuje...");

    // 3. Połączenie WiFi
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi OK!");

    // 4. Inicjalizacja Audio
    audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
    audio.setVolume(volume); // Zakres 0...21 [5]

    // 5. Enkoder
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_KEY, INPUT_PULLUP);

    audio.connecttohost(stations[currentStationIndex].url);
}

void loop() {
    audio.loop();
    vTaskDelay(1); // Pozwala FreeRTOS na obsługę tła [1-3]

    // Obsługa głośności enkoderem
    int clk = digitalRead(ENC_A);
    if (clk != lastClk && clk == LOW) {
        if (digitalRead(ENC_B) != clk) {
            if (volume < 21) volume++;
        } else {
            if (volume > 0) volume--;
        }
        audio.setVolume(volume);
        Serial.printf("Glosnosc: %d\n", volume);
        
        // Aktualizacja głośności na ekranie
        tft.fillRect(10, 200, 100, 20, ILI9341_BLACK);
        tft.setCursor(10, 200);
        tft.setTextSize(1);
        tft.setTextColor(ILI9341_WHITE);
        tft.printf("Vol: %d", volume);
    }
    lastClk = clk;

    // Obsługa przycisku enkodera (zmiana stacji)
    int buttonState = digitalRead(ENC_KEY);
    if (buttonState == LOW && lastButtonState == HIGH && (millis() - lastPressTime > 250)) { // Debounce 250ms
        lastPressTime = millis();
        currentStationIndex++;
        if (currentStationIndex >= numStations) {
            currentStationIndex = 0;
        }
        Serial.printf("Zmiana stacji na: %s\n", stations[currentStationIndex].name);
        audio.connecttohost(stations[currentStationIndex].url);

        // Czyszczenie starego tytułu i bitrate
        tft.fillRect(0, 100, 320, 60, ILI9341_BLACK); 
        tft.fillRect(0, 180, 320, 20, ILI9341_BLACK);

        // Wyświetl nazwę nowej stacji na TFT
        tft.fillRect(0, 40, 320, 40, ILI9341_BLACK); 
        tft.setCursor(10, 40);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_GREEN);
        tft.println(stations[currentStationIndex].name);
    }
    lastButtonState = buttonState;

    // Aktualizacja interfejsu co 1 sekundę (RSSI)
    static uint32_t lastDebugTime = 0;
    if (millis() - lastDebugTime > 1000) {
        lastDebugTime = millis();
        
        // Diagnostyka Serial
        uint32_t buffFilled = audio.inBufferFilled();
        uint32_t buffTotal = audio.getInBufferSize();
        Serial.printf("DIAG: Heap: %u b | Buff: %u/%u (%u%%) | Bitrate: %u\n", 
            ESP.getFreeHeap(), 
            buffFilled, buffTotal, (buffTotal > 0 ? buffFilled * 100 / buffTotal : 0),
            audio.getBitRate());
            
        // Wyświetlanie RSSI
        int rssi = WiFi.RSSI();
        tft.fillRect(240, 0, 80, 20, ILI9341_BLACK);
        tft.setCursor(240, 5);
        tft.setTextSize(1);
        tft.setTextColor(ILI9341_WHITE);
        tft.printf("%d dBm", rssi);

        // Pasek bufora (Buffer Bar)
        if (buffTotal > 0) {
            int barWidth = 300;
            int barHeight = 8;
            int barX = 10;
            int barY = 230;
            int filledWidth = (buffFilled * barWidth) / buffTotal;

            tft.drawRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2, ILI9341_WHITE); // Ramka
            tft.fillRect(barX, barY, filledWidth, barHeight, ILI9341_GREEN);              // Wypełnienie
            tft.fillRect(barX + filledWidth, barY, barWidth - filledWidth, barHeight, ILI9341_BLACK); // Tło (czyszczenie)
        }
    }
   }
