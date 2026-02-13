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

// Zmienne sterujące
int volume = 10;
int lastClk = HIGH;

// ===== NOWY SYSTEM CALLBACKÓW (v3.4.4) =====
void my_audio_info(Audio::msg_t m) {
    Serial.printf("AUDIO_EVENT: %s: %s\n", m.s, m.msg);

    switch (m.e) {
        case Audio::evt_streamtitle:
            // Tu odbierasz metadane (tytuł utworu)
            tft.fillRect(10, 150, 300, 40, ILI9341_BLACK);
            tft.setCursor(10, 150);
            tft.setTextColor(ILI9341_YELLOW);
            tft.println(m.msg); 
            break;
        case Audio::evt_info:
            // Status połączenia
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

    audio.connecttohost("http://ice5.somafm.com/groovesalad-128-mp3");
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
    }
    lastClk = clk;
}