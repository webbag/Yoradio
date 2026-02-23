#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "config.h"
#include "RotaryEncoder.h"
#include <Preferences.h>

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
RotaryEncoder encoder(ENC_A, ENC_B, RotaryEncoder::LatchMode::FOUR3);
Preferences preferences;

// Definicje stacji radiowych
struct Station
{
    const char *name;
    const char *url;
};

const Station stations[] = {

    {"1", "http://stream.nowyswiat.online/mp3"},
    {"2", "https://stream-uk1.radioparadise.com/aac-320"},
    {"3", "https://stream13.polskieradio.pl/pr3/pr3.sdp/playlist.m3u8"},
    {"4", "http://stream.srg-ssr.ch/m/rsj/mp3_128"},
    {"5", "https://stream15.polskieradio.pl/pr24/pr24.sdp/playlist.m3u8"},
    {"6", "http://ice5.somafm.com/groovesalad-128-mp3"},
    {"7", "https://go-audio.toya.net.pl/63214"},
    {"8", "http://stream4.nadaje.com:8578/poznan"},
    {"9", "https://houseoffunk.eu:8443/live"},
    {"10", "https://radio.pejpal.cloud/stream"},
    {"11", "https://dancewave.online/dance.mp3"},
    {"12", "http://198.15.94.34:8006/strumień"},
};
const int numStations = sizeof(stations) / sizeof(stations[0]);
int currentStationIndex = 0;

// Zmienne sterujące
int volume = 12;                 // Zostawiamy na wypadek przyszłego użycia
int lastButtonState = HIGH;      // Stan fizyczny pinu (do debounce)
int currentButtonState = HIGH;   // Stan logiczny przycisku
unsigned long lastDebounceTime = 0;
unsigned long lastRotationTime = 0; // Czas ostatniego obrotu enkodera
bool pendingChange = false;         // Flaga oczekująca na zmianę stacji

// Funkcja obsługi przerwania (ISR) dla enkodera
void IRAM_ATTR readEncoderISR() {
  encoder.tick();
}

// ===== POMOCNICZE FUNKCJE UI =====
void updateText(int x, int y, int w, int h, const char *text, uint16_t color, uint8_t size, bool wrap = false)
{
    tft.fillRect(x, y, w, h, ILI9341_BLACK); // Kluczowe: czyszczenie tła przed pisaniem
    tft.setCursor(x, y + 4);                   // Margines
    tft.setTextColor(color);
    tft.setTextSize(size);
    tft.setTextWrap(wrap);
    tft.print(text);
}

void drawBar(int x, int y, int w, int h, int val, int maxVal, const char *label, uint16_t color)
{
    // Etykieta
    tft.fillRect(x, y, 35, h, ILI9341_BLACK);
    tft.setCursor(x, y + (h / 2) - 4);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.print(label);

    // Pasek tło
    int barX = x + 35;
    int barW = w - 80; // Rezerwacja miejsca na etykietę i %
    tft.drawRect(barX, y, barW, h, ILI9341_WHITE);

    // Wypełnienie paska
    if (maxVal == 0) maxVal = 1;
    int fillW = (val * (barW - 2)) / maxVal;
    if (fillW > barW - 2) fillW = barW - 2;
    tft.fillRect(barX + 1, y + 1, fillW, h - 2, color);
    tft.fillRect(barX + 1 + fillW, y + 1, barW - 2 - fillW, h - 2, ILI9341_BLACK);

    // Wartość procentowa
    tft.fillRect(barX + barW + 5, y, 40, h, ILI9341_BLACK);
    tft.setCursor(barX + barW + 5, y + (h / 2) - 4);
    tft.print((val * 100) / maxVal);
    tft.print("%");
}


// ===== NOWY INTERFEJS UŻYTKOWNIKA I JEGO FUNKCJE =====

void drawLayout() {
    tft.fillScreen(ILI9341_BLACK);
    tft.drawRect(0, 0, 320, 240, ILI9341_DARKGREY); // Ramka

    // Linie oddzielające sekcje
    tft.drawFastHLine(1, 20, 318, ILI9341_DARKGREY);
    tft.drawFastHLine(1, 43, 318, ILI9341_DARKGREY);
    tft.drawFastHLine(1, 168, 318, ILI9341_DARKGREY);
}

void displayHeader(const char* text) {
    // Obszar: x=1, y=1, w=318, h=18
    updateText(5, 3, 310, 16, text, ILI9341_WHITE, 1);
}

void displayUrl(const char* text) {
    // Obszar: x=1, y=22, w=318, h=20
    updateText(5, 24, 310, 18, text, ILI9341_CYAN, 1);
}

void displayStationName(const char* text) {
    // Obszar: x=1, y=44, w=318, h=30
    updateText(5, 47, 310, 28, text, ILI9341_GREEN, 2);
}

void displaySongTitle(const char* text) {
    // Obszar: x=1, y=75, w=318, h=92
    updateText(5, 77, 310, 90, text, ILI9341_YELLOW, 2, true);
}

void displayBufferBar(int val, int maxVal) {
    drawBar(5, 190, 310, 15, val, maxVal, "BUF", ILI9341_ORANGE);
}

// ===== SYSTEM CALLBACKÓW AUDIO =====
void my_audio_info(Audio::msg_t m)
{
    Serial.printf("AUDIO_EVENT [%s]: %s\n", m.s, m.msg);

    switch (m.e)
    {
    case Audio::evt_streamtitle:
        displaySongTitle(m.msg);
        break;
    case Audio::evt_name:
        displayStationName(m.msg);
        break;
    case Audio::evt_bitrate:
        // Obsługiwane w updateDiagnostics
        break;
    default:
        break;
    }
}

void initDisplay()
{
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.begin();
    tft.setRotation(3); // WAŻNE: Ustawienie rotacji PRZED rysowaniem
    drawLayout();
}

void initWiFi()
{
    displayStationName("Laczenie WiFi...");
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi OK!");
    displayStationName("WiFi OK!");
}

void initAudio()
{
    Audio::audio_info_callback = my_audio_info;
    audio.setPinout(I2S_BCK, I2S_LCK, I2S_DIN);
    audio.setVolume(volume);
}

void initEncoder()
{
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_KEY, INPUT_PULLUP);
    // lastClk = digitalRead(ENC_A); // Zastąpione przez bibliotekę

    // Podłączenie przerwań dla pinów enkodera
    attachInterrupt(digitalPinToInterrupt(ENC_A), readEncoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), readEncoderISR, CHANGE);
}

void changeStation(int index)
{
    currentStationIndex = index;
    preferences.putInt("station", currentStationIndex); // Zapisz nową stację
    if (currentStationIndex >= numStations) currentStationIndex = 0;
    if (currentStationIndex < 0) currentStationIndex = numStations - 1;

    Serial.printf("Zmiana stacji na: %s\n", stations[currentStationIndex].name);
    audio.connecttohost(stations[currentStationIndex].url);

    char urlBuff[128];
    snprintf(urlBuff, sizeof(urlBuff), "[%02d] %s", currentStationIndex, stations[currentStationIndex].url);
    displayUrl(urlBuff);
    displayStationName(stations[currentStationIndex].name);
    displaySongTitle("..."); // Wyczyszczenie poprzedniego tytułu
}

void handleEncoderButton()
{
    // Jeśli enkoder był obracany w ciągu ostatnich 500ms, ignoruj potencjalne zakłócenia na przycisku.
    // To zapobiega przypadkowej zmianie stacji podczas regulacji głośności.
    if (millis() - lastRotationTime < 500) {
        // Zresetuj stan debounce, aby uniknąć fałszywego wciśnięcia zaraz po zakończeniu obrotu
        // lastButtonState = digitalRead(ENC_KEY); // Usunięcie tej linii zapobiega błędom w logice debounce
        return;
    }

    int reading = digitalRead(ENC_KEY);

    // Jeśli stan pinu się zmienił (szum lub wciśnięcie), resetujemy zegar
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }

    // Jeśli stan jest stabilny przez 80ms (zwiększono, aby odfiltrować szum z obrotu)
    if ((millis() - lastDebounceTime) > 80) {
        if (reading != currentButtonState) {
            currentButtonState = reading;
            if (currentButtonState == LOW) {
                // Przycisk obecnie nie pełni żadnej funkcji
            }
        }
    }
    lastButtonState = reading;
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    // Inicjalizacja Preferences i odczyt ustawień
    preferences.begin("yoradio", false); // Namespace "yoradio", tryb RW
    volume = preferences.getInt("volume", 18); // Domyślnie 18
    currentStationIndex = preferences.getInt("station", 0); // Domyślnie 0
    if (currentStationIndex >= numStations) currentStationIndex = 0;

    initAudio();
    initDisplay();
    initWiFi();
    initEncoder();

    changeStation(currentStationIndex);
}

void updateDiagnostics()
{
    static uint32_t lastDebugTime = 0;
    if (millis() - lastDebugTime > 1000) // Częstsza aktualizacja dla płynności
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

        char headBuff[64];
        snprintf(headBuff, sizeof(headBuff), "%uHz   %ukbps       RSSI %ddBm", 
                 audio.getSampleRate(), audio.getBitRate()/1000, WiFi.RSSI());
        displayHeader(headBuff);

        displayBufferBar(buffFilled, buffTotal);
    }
}
void loop()
{
    audio.loop();
    vTaskDelay(1);

    // Nowa logika enkodera: przeglądanie stacji
    static long lastEncoderPos = 0;
    long newEncoderPos = encoder.getPosition();

    if (newEncoderPos != lastEncoderPos) {
        lastRotationTime = millis(); // Zapisz czas ostatniego obrotu

        if (newEncoderPos > lastEncoderPos) {
            currentStationIndex++;
        } else {
            currentStationIndex--;
        }

        // Zapętlanie listy
        if (currentStationIndex >= numStations) currentStationIndex = 0;
        if (currentStationIndex < 0) currentStationIndex = numStations - 1;

        // Zaktualizuj UI, aby pokazać podświetloną stację
        char urlBuff[128];
        snprintf(urlBuff, sizeof(urlBuff), "[%02d] %s", currentStationIndex, stations[currentStationIndex].url);
        displayUrl(urlBuff);
        displayStationName(stations[currentStationIndex].name);

        pendingChange = true;
        lastEncoderPos = newEncoderPos;
    }

    if (pendingChange && (millis() - lastRotationTime > 500)) {
        pendingChange = false;
        changeStation(currentStationIndex);
    }

    handleEncoderButton();
    updateDiagnostics();
}
