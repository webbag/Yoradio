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
struct Station
{
    const char *name;
    const char *url;
};

const Station stations[] = {

    {"3 ", "https://live.wostreaming.net/direct/ppm-jazz24mp3-ibc1"},
    {"1 ", "https://stream13.polskieradio.pl/pr3/pr3.sdp/playlist.m3u8"},
    {"4 ", "http://stream.srg-ssr.ch/m/rsj/mp3_128"},
    {"5 ", "http://stream.nowyswiat.online/mp3"},
    {"6 ", "https://stream15.polskieradio.pl/pr24/pr24.sdp/playlist.m3u8"},
    {"2 ", "http://ice5.somafm.com/groovesalad-128-mp3"},

    {"7 ", "https://go-audio.toya.net.pl/63214"},
    {"8 ", "http://stream4.nadaje.com:8578/poznan"},
    {"9 ", "https://houseoffunk.eu:8443/live"},
    {"10", "https://radio.pejpal.cloud/stream"},
    {"11", "https://dancewave.online/dance.mp3"},
    {"12", "http://198.15.94.34:8006/strumień"},

    // {"RMF FM", "http://195.150.20.242:8000/rmf_fm"},
    // {"KEXP", "https://kexp-mp3-128.streamguys1.com/kexp128.mp3"},
    // {"SomaFM Groove Salad", "http://ice5.somafm.com/groovesalad-128-mp3"},
    // {"SomaFM Beat Blender", "http://ice5.somafm.com/beatblender-128-mp3"},
    // {"SomaFM Folk Forward", "http://ice5.somafm.com/folkfwd-128-mp3"},
    // {"Radio Nowy Swiat", "http://stream.nowyswiat.online/mp3"},
    // {"Radio ZET", "http://zet090-02.cdn.eurozet.pl:8404/"},
    // {"KEXP Seattle", "https://kexp-mp3-128.streamguys1.com/kexp128.mp3"},
    // {"Psychedelic Jukebox", "https://eagle.streemlion.com:2199/tunein/psychedelicj.asx"},
    // {"Rock Arsenal", "http://online.rockarsenal.ru:8000/rockarsenal_aacplus"},
    // {"knixx.fm", "http://s1.knixx.fm/dein_webradio_64.aac"},
    // {"Byte.fm 320k", "http://www.byte.fm/stream/320.m3u"},
    // {"Antenne Thueringen", "http://stream.antennethueringen.de/live/aac-64/stream.antennethueringen.de/"},
    // {"Radio Swiss Jazz", "http://stream.srg-ssr.ch/m/rsj/mp3_128"},
    // {"Classic FM", "https://icecast.mediauk.com/classicfm.mp3"},
    // {"SomaFM Underground 80s", "http://ice5.somafm.com/u80s-128-mp3"},
    // {"SomaFM Space Station", "http://ice5.somafm.com/spacestation-128-mp3"},
    // {"BBC Radio 6 Music", "http://stream.live.vc.bbcmedia.co.uk/bbc_6music"},
    // {"Radio Paradise 192k", "https://stream.radioparadise.com/mp3-192"},
    // {"Triple J", "http://live-radio01.mediahubaustralia.com/2TJW/mp3/"},
    // {"SomaFM Drone Zone", "http://ice5.somafm.com/dronezone-128-mp3"},
    // {"DI.FM House", "https://prem1.di.fm:80/house_hi"},
    // {"Ibiza Global Radio", "http://ibizaglobalradio.streaming-pro.com:8024/stream"},
    // {"Jazz24", "https://live.wostreaming.net/direct/ppm-jazz24mp3-ibc1"},
    // {"FIP", "http://direct.fipradio.fr/live/fip-midfi.mp3"},
    // {"Radio Caprice Stoner Rock", "http://79.111.14.76:8002/stonerrock"},
    // {"Metal Devastation Radio", "http://stream.metaldevastationradio.com:8000/live"},
    // {"FluxFM", "http://streams.fluxfm.de/fluxfm/mp3-128/audio/"},
    // {"KCRW Eclectic24", "http://kcrw.streamguys1.com/kcrw_192k_mp3_e24"},
    // {"NTS Radio 1", "https://stream-relay-geo.ntslive.net/stream"},
    // {"The Current", "http://current.stream.publicradio.org/current.mp3"},
    // {"Radio Nova (FR)", "http://novazz.ice.infomaniak.ch/novazz-128.mp3"},
};
const int numStations = sizeof(stations) / sizeof(stations[0]);
int currentStationIndex = 0;

// Zmienne sterujące
int volume = 10;
int lastClk = HIGH;
int lastButtonState = HIGH;
unsigned long lastPressTime = 0;

// ===== POMOCNICZE FUNKCJE UI =====
void updateText(int x, int y, int w, int h, const char *text, uint16_t color, uint8_t size, bool wrap = false)
{
    tft.fillRect(x, y, w, h, ILI9341_MAGENTA); // Kluczowe: czyszczenie tła przed pisaniem
    tft.setCursor(x, y + 1);                   // Margines
    tft.setTextColor(color);
    tft.setTextSize(size);
    tft.setTextWrap(wrap);
    tft.print(text);
}

// ===== NOWY SYSTEM CALLBACKÓW (v3.4.4) =====
void my_audio_info(Audio::msg_t m)
{
    // Logowanie wszystkich zdarzeń do Serial
    Serial.printf("AUDIO_EVENT [%s]: %s\n", m.s, m.msg);

    switch (m.e)
    {
    case Audio::evt_streamtitle:
        updateText(0, 80, 320, 80, m.msg, ILI9341_YELLOW, 2, true);
        break;
    case Audio::evt_name:
        // Nazwa stacji
        updateText(0, 30, 320, 40, m.msg, ILI9341_GREEN, 2, false);
        break;
    case Audio::evt_bitrate:
        // Aktualizacja paska info (Bitrate / SampleRate)
        char infoBuff[64];
        snprintf(infoBuff, sizeof(infoBuff), "%u kbps | %u Hz", audio.getBitRate() / 1000, audio.getSampleRate());
        updateText(0, 170, 320, 20, infoBuff, ILI9341_CYAN, 1, false);
        break;
    default:
        break;
    }
}

void drawInterface()
{
    tft.fillScreen(ILI9341_BLACK);
    // Linie oddzielające sekcje
    tft.drawFastHLine(0, 25, 320, ILI9341_DARKGREY);  // Pod statusem
    tft.drawFastHLine(0, 75, 320, ILI9341_DARKGREY);  // Pod nazwą stacji
    tft.drawFastHLine(0, 165, 320, ILI9341_DARKGREY); // Pod tytułem
    tft.drawFastHLine(0, 200, 320, ILI9341_DARKGREY); // Nad stopką

    // Etykiety statyczne
    tft.setCursor(2, 205);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setTextSize(1);
    tft.print("Buf:");
}

void setup()
{
    Serial.begin(115200);
    delay(2000); // Czas dla USB w ESP32-S3 [4]

    // 1. REJESTRACJA CALLBACKA - KLUCZ DO METADANYCH W 3.4.4
    Audio::audio_info_callback = my_audio_info;

    // 2. Inicjalizacja Ekranu
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.begin();
    tft.setRotation(1);

    drawInterface();
    updateText(0, 30, 320, 40, "Laczenie WiFi...", ILI9341_WHITE, 2);

    // 3. Połączenie WiFi
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi OK!");
    updateText(0, 30, 320, 40, "WiFi OK!", ILI9341_GREEN, 2);

    // 4. Inicjalizacja Audio
    audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
    audio.setVolume(volume); // Zakres 0...21 [5]

    updateText(0, 30, 320, 20,
               stations[currentStationIndex].name,
               ILI9341_GREEN, 2, false);

    updateText(0, 55, 320, 20,
               stations[currentStationIndex].url,
               ILI9341_CYAN, 1, false);

    // 5. Enkoder
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_KEY, INPUT_PULLUP);

    audio.connecttohost(stations[currentStationIndex].url);
}

void loop()
{
    audio.loop();
    vTaskDelay(1); // Pozwala FreeRTOS na obsługę tła [1-3]

    // Obsługa głośności enkoderem
    int clk = digitalRead(ENC_A);
    if (clk != lastClk && clk == LOW)
    {
        if (digitalRead(ENC_B) != clk)
        {
            if (volume < 21)
                volume++;
        }
        else
        {
            if (volume > 0)
                volume--;
        }
        audio.setVolume(volume);
        Serial.printf("Glosnosc: %d\n", volume);

        // Aktualizacja głośności na ekranie (Pasek statusu - lewa strona)
        char volBuff[16];
        snprintf(volBuff, sizeof(volBuff), "Vol: %d", volume);
        updateText(5, 5, 80, 20, volBuff, ILI9341_WHITE, 1);
    }
    lastClk = clk;

    // Obsługa przycisku enkodera (zmiana stacji)
    int buttonState = digitalRead(ENC_KEY);
    if (buttonState == LOW && lastButtonState == HIGH && (millis() - lastPressTime > 250))
    { // Debounce 250ms
        lastPressTime = millis();
        currentStationIndex++;
        if (currentStationIndex >= numStations)
        {
            currentStationIndex = 0;
        }
        Serial.printf("Zmiana stacji na: %s\n", stations[currentStationIndex].name);
        audio.connecttohost(stations[currentStationIndex].url);

        // Reset UI dla nowej stacji
        updateText(0, 80, 320, 80, "", ILI9341_BLACK, 2); // Czyść tytuł
        updateText(0, 30, 320, 40, stations[currentStationIndex].name, ILI9341_GREEN, 2);
        drawInterface(); // Odśwież linie
    }
    lastButtonState = buttonState;

    // Aktualizacja interfejsu co 1 sekundę (RSSI)
    static uint32_t lastDebugTime = 0;
    if (millis() - lastDebugTime > 10000)
    {
        lastDebugTime = millis();

        uint32_t buffFilled = audio.inBufferFilled();
        uint32_t buffTotal = audio.getInBufferSize();

        // --- PEŁNA DIAGNOSTYKA SERIAL ---
        Serial.println("\n--- DIAGNOSTYKA SYSTEMU ---");
        Serial.printf("RAM: Free: %u | MaxAlloc: %u | Total: %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getHeapSize());
        if (ESP.getPsramSize() > 0)
            Serial.printf("PSRAM: Free: %u | Total: %u\n", ESP.getFreePsram(), ESP.getPsramSize());
        Serial.printf("WiFi: RSSI: %d dBm | Status: %d\n", WiFi.RSSI(), WiFi.status());
        Serial.printf("Audio: Bitrate: %u | SampleRate: %u | Channels: %d | Bits: %d\n", audio.getBitRate(), audio.getSampleRate(), audio.getChannels(), audio.getBitsPerSample());
        Serial.printf("Buffer: %u / %u (%u%%)\n", buffFilled, buffTotal, (buffTotal > 0 ? buffFilled * 100 / buffTotal : 0));
        Serial.printf("Stream Time: %u s | Duration: %u s\n", audio.getAudioCurrentTime(), audio.getAudioFileDuration());
        Serial.println("---------------------------");

        // Wyświetlanie RSSI (Prawy górny róg)
        int rssi = WiFi.RSSI();
        char rssiBuff[16];
        snprintf(rssiBuff, sizeof(rssiBuff), "%d dBm", rssi);
        updateText(240, 5, 80, 20, rssiBuff, ILI9341_WHITE, 1);

        // Pasek bufora (Buffer Bar)
        if (buffTotal > 0)
        {
            int barWidth = 280;
            int barHeight = 10;
            int barX = 30;
            int barY = 205;
            int filledWidth = (buffFilled * barWidth) / buffTotal;

            tft.drawRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2, ILI9341_WHITE);             // Ramka
            tft.fillRect(barX, barY, filledWidth, barHeight, ILI9341_GREEN);                          // Wypełnienie
            tft.fillRect(barX + filledWidth, barY, barWidth - filledWidth, barHeight, ILI9341_BLACK); // Tło (czyszczenie)
        }
    }
}
