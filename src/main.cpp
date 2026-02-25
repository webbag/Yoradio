#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "config.h"
#include "RotaryEncoder.h"
#include <Preferences.h>
#include <time.h>
#include <SunMoonCalc.h>

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
    {"Dark", "http://dark.sh/mp3"},
    {"RdMix Classic Rock 70s 80s 90s", "https://cast1.torontocast.com:4610/stream"},
    {"RdMix DJSET 70s 80s 90s", "https://cast1.torontocast.com:4560/stream"},
    {"Technolovers - BASS HOUSE", "https://stream.technolovers.fm/bass-house"},
    {"Technolovers HOUSE", "https://stream.technolovers.fm/house"},
    {"Yabiladi Azawan Amazigh", "https://radio.yabiladi.com:9002/;stream.mp3"},
    {"Worldwide FM", "https://worldwide-fm.radiocult.fm/stream"},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    // {"", ""},
    
    
};
const int numStations = sizeof(stations) / sizeof(stations[0]);
int currentStationIndex = 0;

// Zmienne sterujące
int volume = 8;                 // Zostawiamy na wypadek przyszłego użycia
int lastButtonState = HIGH;      // Stan fizyczny pinu (do debounce)
int currentButtonState = HIGH;   // Stan logiczny przycisku
unsigned long lastDebounceTime = 0;
unsigned long lastRotationTime = 0; // Czas ostatniego obrotu enkodera
bool pendingChange = false;         // Flaga oczekująca na zmianę stacji

// Statystyki CPU
unsigned long cpuWorkTime = 0;
float cpuLoad5 = 0.0;
float cpuLoad10 = 0.0;
float cpuLoad15 = 0.0;

// FFT (Equalizer)
float fftPeak[32] = {0}; // Efekt opadających szczytów

// Zegar i WiFi
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;      // UTC+1 (Dostosuj do swojej strefy)
const int   daylightOffset_sec = 3600; // Czas letni


// Zmienne do przewijania tekstu (Scrolling Text)
GFXcanvas16 *titleCanvas = nullptr;
char currentSongTitle[256] = "";
int titleTextWidth = 0;
int titleScrollOffset = 0;
unsigned long lastTitleScrollTime = 0;
bool titleChanged = false;

// Zmienne do cyklicznego wyświetlania info (URL / Słońce / Księżyc)
char currentUrl[128] = "";
unsigned long lastInfoSwitchTime = 0;
int infoMode = 0; // 0: URL, 1: Słońce, 2: Księżyc

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
    tft.drawFastHLine(1, 118, 318, ILI9341_DARKGREY); // Linia pod tytułem (przesunięta wyżej dla większego spektrum)
    tft.drawFastHLine(1, 188, 318, ILI9341_DARKGREY); // Linia pod spektrum
}

void drawWifiSignal(int x, int y, int rssi) {
    // Czyść obszar ikony
    tft.fillRect(x, y, 24, 16, ILI9341_BLACK);
    
    int bars = 0;
    if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else if (rssi > -85) bars = 1;

    for (int i = 0; i < 4; i++) {
        int h = 4 + (i * 3); // Wysokość słupka
        uint16_t color = (i < bars) ? ILI9341_WHITE : ILI9341_DARKGREY;
        tft.fillRect(x + (i * 6), y + (14 - h), 4, h, color);
    }
}

void drawClock(int x, int y) {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return;
    }
    char timeBuff[20];
    strftime(timeBuff, sizeof(timeBuff), "%H:%M %d.%m", &timeinfo);
    
    // Wyświetl zegar (używamy updateText dla łatwego czyszczenia tła)
    updateText(x, y, 135, 16, timeBuff, ILI9341_CYAN, 2); 
}

// Bitmapy ikon 16x16
const unsigned char sunBitmap[] PROGMEM = {
    0x00, 0x00, 0x04, 0x20, 0x08, 0x10, 0x10, 0x08, 0x00, 0x00, 0x07, 0xE0, 0x0F, 0xF0, 0x1F, 0xF8,
    0x1F, 0xF8, 0x0F, 0xF0, 0x07, 0xE0, 0x00, 0x00, 0x10, 0x08, 0x08, 0x10, 0x04, 0x20, 0x00, 0x00
};

const unsigned char moonBitmap[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x01, 0xC0, 0x03, 0xE0, 0x07, 0xF0, 0x0E, 0x70, 0x0C, 0x38, 0x0C, 0x1C,
    0x0C, 0x1C, 0x0C, 0x38, 0x0E, 0x70, 0x07, 0xF0, 0x03, 0xE0, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00
};

void drawSunMoonInfo() {
    time_t now;
    time(&now);
    if (now < 100000) return; // Czas nieustawiony

    SunMoonCalc smc(now, LATITUDE, LONGITUDE);
    SunMoonCalc::Result result = smc.calculateSunAndMoonData();

    char buff[64];
    struct tm *ti;

    // Czyść obszar info (taki sam jak dla URL)
    tft.fillRect(5, 24, 310, 18, ILI9341_BLACK);
    tft.setCursor(26, 28);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_YELLOW);

    if (infoMode == 1) { // Słońce
        char rise[10], set[10];
        ti = localtime(&result.sun.rise);
        strftime(rise, sizeof(rise), "%H:%M", ti);
        ti = localtime(&result.sun.set);
        strftime(set, sizeof(set), "%H:%M", ti);
        
        snprintf(buff, sizeof(buff), "%s - %s", rise, set);
        tft.drawBitmap(5, 25, sunBitmap, 16, 16, ILI9341_YELLOW);
        tft.print(buff);
    } 
    else if (infoMode == 2) { // Księżyc
        char rise[10], set[10];
        if (result.moon.rise) {
            ti = localtime(&result.moon.rise);
            strftime(rise, sizeof(rise), "%H:%M", ti);
        } else strcpy(rise, "--:--");
        
        if (result.moon.set) {
            ti = localtime(&result.moon.set);
            strftime(set, sizeof(set), "%H:%M", ti);
        } else strcpy(set, "--:--");

        tft.setTextColor(ILI9341_LIGHTGREY);
        snprintf(buff, sizeof(buff), "%s - %s (%d%%)", rise, set, (int)(result.moon.illumination * 100));
        tft.drawBitmap(5, 25, moonBitmap, 16, 16, ILI9341_LIGHTGREY);
        tft.print(buff);
    }
}

void displayUrl(const char* text) {
    strncpy(currentUrl, text, sizeof(currentUrl) - 1);
    // Wymuś natychmiastowe wyświetlenie URL
    infoMode = 0;
    lastInfoSwitchTime = millis();
    updateText(5, 24, 310, 18, currentUrl, ILI9341_CYAN, 1);
}

void displayStationName(const char* text) {
    // Obszar: x=1, y=44, w=318, h=30
    updateText(5, 47, 310, 28, text, ILI9341_GREEN, 2);
}

// Zmodyfikowana funkcja - tylko ustawia tekst, rysowanie odbywa się w pętli
void displaySongTitle(const char* text) {
    strncpy(currentSongTitle, text, sizeof(currentSongTitle) - 1);
    currentSongTitle[sizeof(currentSongTitle) - 1] = 0;

    if (titleCanvas) {
        titleCanvas->setTextSize(2);
        titleCanvas->setTextWrap(false);
        int16_t x1, y1;
        uint16_t w, h;
        titleCanvas->getTextBounds(currentSongTitle, 0, 0, &x1, &y1, &w, &h);
        titleTextWidth = w;
    }

    titleScrollOffset = 0;
    titleChanged = true; // Flaga wymuszająca odświeżenie tła
}

void drawScrollingTitle() {
    if (!titleCanvas) return;

    // Paleta kolorów dla różnych stacji
    const uint16_t stationColors[] = {
        ILI9341_YELLOW, ILI9341_CYAN, ILI9341_GREEN, ILI9341_MAGENTA, 
        ILI9341_RED, ILI9341_WHITE, ILI9341_ORANGE, 0xAD75 /* Light Blue */
    };
    uint16_t textColor = stationColors[currentStationIndex % (sizeof(stationColors) / sizeof(stationColors[0]))];

    if (titleChanged) {
        tft.fillRect(5, 77, 310, 40, ILI9341_BLACK); // Wyczyść obszar tytułu (zmniejszony)
        titleChanged = false;
    }

    bool scroll = (titleTextWidth > 310);
    
    if (scroll) {
        // Dynamiczna prędkość przewijania: im dłuższy tekst, tym mniejsze opóźnienie (szybciej)
        int scrollDelay = map(titleTextWidth, 310, 1200, 50, 10);
        scrollDelay = constrain(scrollDelay, 10, 50);

        if (millis() - lastTitleScrollTime > scrollDelay) { // Prędkość przewijania
            titleScrollOffset += 2;
            if (titleScrollOffset > titleTextWidth + 40) {
                titleScrollOffset = -310; // Restart z prawej strony
            }
            lastTitleScrollTime = millis();
            
            // Rysowanie na Canvasie
            titleCanvas->fillScreen(ILI9341_BLACK);
            titleCanvas->setTextColor(textColor);
            titleCanvas->setTextSize(2);
            titleCanvas->setCursor(0 - titleScrollOffset, 12); // Wyśrodkowane w pionie (40px)
            titleCanvas->print(currentSongTitle);
            
            // Przut na ekran
            tft.drawRGBBitmap(5, 77, titleCanvas->getBuffer(), 310, 40);
        }
    } else {
        // Jeśli tekst się mieści, rysujemy go statycznie tylko raz (lub gdy się zmienił)
        static int lastStaticDraw = -1;
        static uint16_t lastColor = 0;
        if (titleScrollOffset != lastStaticDraw || textColor != lastColor) {
            titleCanvas->fillScreen(ILI9341_BLACK);
            titleCanvas->setTextColor(textColor);
            titleCanvas->setTextSize(2);
            titleCanvas->setCursor(0, 12);
            titleCanvas->print(currentSongTitle);
            tft.drawRGBBitmap(5, 77, titleCanvas->getBuffer(), 310, 40);
            lastStaticDraw = titleScrollOffset;
            lastColor = textColor;
        }
    }
}

void drawSpectrum() {
    int barWidth = 15;
    int barSpacing = 4;
    int startX = 8;
    int startY = 186; // Dół obszaru spektrum
    int maxH = 65;    // Zwiększona maksymalna wysokość słupka
    
    // Symulacja beatu (rytmu)
    static unsigned long lastBeatTime = 0;
    bool isBeat = false;
    if (millis() - lastBeatTime > 500) { // Co 500ms (120 BPM)
        lastBeatTime = millis();
        isBeat = true;
    }

    // Strefy kolorów (VU meter style)
    int zone1 = maxH * 0.2; // Granica zielony/żółty (obniżona dla większej dynamiki)
    int zone2 = maxH * 0.4; // Granica żółty/czerwony (obniżona dla większej dynamiki)

    for (int i = 0; i < 16; i++) {
        // Symulacja spektrum (biblioteka audio nie udostępnia surowych danych)
        // Generujemy losową wysokość z wygładzaniem, aby wyglądało to naturalnie
        static int lastH[32] = {0};
        int targetH = random(0, maxH);
        
        if (isBeat && i < 5) targetH = maxH; // Podbicie basu przy beacie

        lastH[i] = (lastH[i] * 9 + targetH) / 10; // Mocniejsze wygładzanie (wolniejszy ruch)
        int h = lastH[i];

        if (h > maxH) h = maxH;
        
        // Peak hold
        if (h > fftPeak[i]) fftPeak[i] = h;
        else fftPeak[i] -= 0.5; // Dostosowanie opadania do rytmu
        if (fftPeak[i] < 0) fftPeak[i] = 0;

        // Czyszczenie tła powyżej słupka
        tft.fillRect(startX + i * (barWidth + barSpacing), startY - maxH, barWidth, maxH - h, ILI9341_BLACK);
        
        // Rysowanie słupka z gradientem (strefami)
        int x = startX + i * (barWidth + barSpacing);
        
        // 1. Zielony (dół)
        int h_green = (h > zone1) ? zone1 : h;
        if (h_green > 0) tft.fillRect(x, startY - h_green, barWidth, h_green, ILI9341_GREEN);
        
        // 2. Żółty (środek)
        int h_yellow = (h > zone2) ? (zone2 - zone1) : (h - zone1);
        if (h > zone1) tft.fillRect(x, startY - zone1 - h_yellow, barWidth, h_yellow, ILI9341_YELLOW);
        
        // 3. Czerwony (góra)
        int h_red = h - zone2;
        if (h > zone2) tft.fillRect(x, startY - zone2 - h_red, barWidth, h_red, ILI9341_RED);

        tft.drawFastHLine(startX + i * (barWidth + barSpacing), startY - (int)fftPeak[i], barWidth, ILI9341_RED);
    }
}

void displayFooter(int bufVal, int bufMax) {
    // Wyczyść stopkę
    tft.fillRect(0, 195, 320, 45, ILI9341_BLACK);

    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setTextSize(1);

    // Linia 1: Bufor % i Uptime
    int bufPct = (bufMax > 0) ? (bufVal * 100 / bufMax) : 0;
    unsigned long up = millis() / 1000;
    int d = up / 86400;
    int h = (up % 86400) / 3600;
    int m = (up % 3600) / 60;
    int s = up % 60;

    tft.setCursor(5, 198);
    tft.printf("Buf: %d%%   Uptime: %dd %02d:%02d:%02d", bufPct, d, h, m, s);

    // Linia 2: CPU Load (5/10/15m) i RAM
    tft.setCursor(5, 215);
    tft.printf("CPU: %d/%d/%d%%", (int)cpuLoad5, (int)cpuLoad10, (int)cpuLoad15);

    uint32_t freeRam = ESP.getFreeHeap();
    tft.printf("  RAM: %uKB", freeRam / 1024);
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

void handleInfoArea() {
    if (millis() - lastInfoSwitchTime > 5000) { // Zmieniaj co 5 sekund
        lastInfoSwitchTime = millis();
        infoMode = (infoMode + 1) % 3; // 0->1->2->0

        if (infoMode == 0) {
            // Powrót do URL
            updateText(5, 24, 310, 18, currentUrl, ILI9341_CYAN, 1);
        } else {
            // Słońce lub Księżyc
            drawSunMoonInfo();
        }
    }
}

void initDisplay()
{
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.begin();
    tft.setRotation(3); // WAŻNE: Ustawienie rotacji PRZED rysowaniem
    drawLayout();

    // Inicjalizacja bufora dla przewijanego tekstu (310x60 pikseli)
    if (titleCanvas == nullptr) {
        titleCanvas = new GFXcanvas16(310, 40);
    }
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

    // Konfiguracja czasu (NTP)
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
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
    snprintf(urlBuff, sizeof(urlBuff), "%s", stations[currentStationIndex].url);
    displayUrl(urlBuff);
    displayStationName(stations[currentStationIndex].name);
    displaySongTitle(""); // Reset tytułu
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
    volume = preferences.getInt("volume", volume); // Domyślnie 18
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
    if (millis() - lastDebugTime > 1000) // Aktualizacja co 1s dla zegara
    {
        // Obliczanie obciążenia CPU
        unsigned long interval = millis() - lastDebugTime;
        float currentLoad = (float)cpuWorkTime / (interval * 1000.0) * 100.0;
        if (currentLoad > 100.0) currentLoad = 100.0;
        cpuWorkTime = 0; // Reset licznika pracy

        // Aktualizacja średnich (EMA)
        // Alpha = 2 / (N + 1), gdzie N to liczba próbek (sekund)
        // 5 min = 300s, 10 min = 600s, 15 min = 900s
        cpuLoad5  = (currentLoad * 0.0066) + (cpuLoad5  * 0.9934);
        cpuLoad10 = (currentLoad * 0.0033) + (cpuLoad10 * 0.9967);
        cpuLoad15 = (currentLoad * 0.0022) + (cpuLoad15 * 0.9978);


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

        // 1. Info o dźwięku (lewa strona)
        char headBuff[64];
        snprintf(headBuff, sizeof(headBuff), "%uHz %ukbps", audio.getSampleRate(), audio.getBitRate()/1000);
        updateText(5, 3, 140, 16, headBuff, ILI9341_LIGHTGREY, 1);

        // 2. Zegar i WiFi (prawa strona)
        drawClock(150, 1);      // Zegar na środku/prawej
        drawWifiSignal(290, 3, WiFi.RSSI()); // Ikona WiFi przy krawędzi

        displayFooter(buffFilled, buffTotal);
    }
}
void loop()
{
    unsigned long startLoop = micros(); // Start pomiaru czasu pracy

    audio.loop();
    // vTaskDelay(1); // Przeniesione na koniec

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

        pendingChange = true;
        lastEncoderPos = newEncoderPos;
    }

    if (pendingChange && (millis() - lastRotationTime > 500)) {
        pendingChange = false;
        changeStation(currentStationIndex);
    }

    handleEncoderButton();
    updateDiagnostics();
    drawScrollingTitle(); // Obsługa przewijania w pętli głównej
    handleInfoArea();     // Obsługa cyklicznego wyświetlania info
    drawSpectrum();       // Rysowanie spektrum

    cpuWorkTime += (micros() - startLoop); // Sumowanie czasu pracy
    vTaskDelay(1); // Oddanie czasu procesora (Idle)
}
