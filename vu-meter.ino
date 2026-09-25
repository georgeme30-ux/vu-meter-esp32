/*
 * VU METER FFT - 2 MODOS (BARRAS / ESPECTRO)
 * ESP32-S3-N16R8
 * 
 * INMP441:  WS=4, SCK=5, SD=6, L/R=GND
 * OLED:     SDA=8, SCL=9
 * Botón:    GPIO 10 (a GND, INPUT_PULLUP)
 * 
 * Botón cambia entre:
 *  - Modo 0: BARRAS (verticales)
 *  - Modo 1: ESPECTRO (linea continua)
 */

#include <driver/i2s.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduinoFFT.h>

// ---------- Pines ----------
#define I2S_WS     4
#define I2S_SCK    5
#define I2S_SD     6
#define I2S_PORT   I2S_NUM_0
#define SDA_PIN    8
#define SCL_PIN    9
#define BUTTON_PIN 10

// ---------- OLED ----------
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ---------- FFT ----------
#define SAMPLES              1024
#define SAMPLING_FREQUENCY   16000
#define NUM_BANDS            8

double vReal[SAMPLES];
double vImag[SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLING_FREQUENCY);

// Bandas
const int bandStart[NUM_BANDS] = {  4,   8,  16,  32,  64, 128, 256, 384};
const int bandEnd[NUM_BANDS]   = {  8,  16,  32,  64, 128, 256, 384, 512};
const char* bandLabels[NUM_BANDS] = {"60","125","250","500","1k","2k","4k","6k"};

// ---------- Niveles ----------
float nivelSuave[NUM_BANDS];
float umbralAuto[NUM_BANDS];
float pico[NUM_BANDS];

unsigned long lastPicoTime[NUM_BANDS];
unsigned long lastUmbralDecay = 0;

// ---------- Parametros ----------
#define SUAVIZADO        0.5
#define PICO_HOLD_MS     1000
#define PICO_DECAY       3
#define UMBRAL_MINIMO    50.0
#define UMBRAL_DECAER_MS 3000
#define UMBRAL_DECAER    0.95
#define MARGEN_VISUAL    0.85

// ---------- Estado ----------
int modo = 0;                    // 0=barras, 1=espectro
const int TOTAL_MODOS = 2;
const char* modoNombre[] = {"BARRAS", "ESPECTRO"};

unsigned long lastButtonTime = 0;

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== VU METER 2 MODOS ===");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED no detectada");
    while (true) delay(100);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(20, 28);
  display.println("Iniciando...");
  display.display();

  // Inicializar
  for (int i = 0; i < NUM_BANDS; i++) {
    nivelSuave[i]  = 0;
    umbralAuto[i]  = UMBRAL_MINIMO;
    pico[i]        = 0;
    lastPicoTime[i] = 0;
  }

  // I2S
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLING_FREQUENCY,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_start(I2S_PORT);

  Serial.println("Listo. Pica el boton para cambiar modo.");
}

// ---------- Loop ----------
void loop() {
  // Boton: cambiar modo
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - lastButtonTime > 300) {
      lastButtonTime = millis();
      modo = (modo + 1) % TOTAL_MODOS;
      Serial.print("Modo: ");
      Serial.println(modoNombre[modo]);
    }
  }

  // Leer I2S
  int32_t buffer[SAMPLES];
  size_t bytesRead = 0;
  i2s_read(I2S_PORT, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);

  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (double)(buffer[i] >> 14);
    vImag[i] = 0.0;
  }

  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  // Calcular bandas
  for (int b = 0; b < NUM_BANDS; b++) {
    double suma = 0;
    int count = 0;
    for (int i = bandStart[b]; i < bandEnd[b]; i++) {
      suma += vReal[i];
      count++;
    }
    float crudo = suma / count;
    nivelSuave[b] = nivelSuave[b] * SUAVIZADO + crudo * (1.0 - SUAVIZADO);
  }

  // Auto-gain
  for (int b = 0; b < NUM_BANDS; b++) {
    if (nivelSuave[b] > umbralAuto[b]) umbralAuto[b] = nivelSuave[b];
  }

  if (millis() - lastUmbralDecay > UMBRAL_DECAER_MS) {
    lastUmbralDecay = millis();
    for (int b = 0; b < NUM_BANDS; b++) {
      umbralAuto[b] *= UMBRAL_DECAER;
      if (umbralAuto[b] < UMBRAL_MINIMO) umbralAuto[b] = UMBRAL_MINIMO;
    }
  }

  // Picos
  unsigned long ahora = millis();
  for (int b = 0; b < NUM_BANDS; b++) {
    if (nivelSuave[b] > pico[b]) {
      pico[b] = nivelSuave[b];
      lastPicoTime[b] = ahora;
    } else if (ahora - lastPicoTime[b] > PICO_HOLD_MS) {
      pico[b] -= PICO_DECAY;
      if (pico[b] < 0) pico[b] = 0;
    }
  }

  // Dibujar segun modo
  if (modo == 0) dibujarBarras();
  else           dibujarEspectro();
}

// ---------- Dibujar barras ----------
void dibujarBarras() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("VU - BARRAS");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  int barWidth = 14;
  int spacing  = 2;
  int startX   = 2;
  int topY     = 14;
  int bottomY  = 60;
  int maxH     = bottomY - topY;

  for (int b = 0; b < NUM_BANDS; b++) {
    int x = startX + b * (barWidth + spacing);
    display.drawRect(x, topY, barWidth, maxH, SSD1306_WHITE);

    float umbralAj = umbralAuto[b] * MARGEN_VISUAL;
    if (umbralAj < UMBRAL_MINIMO) umbralAj = UMBRAL_MINIMO;

    int altura = (int)((nivelSuave[b] / umbralAj) * (maxH - 2));
    altura = constrain(altura, 0, maxH - 2);

    if (altura > 0) {
      display.fillRect(x + 1, bottomY - altura, barWidth - 2, altura, SSD1306_WHITE);
    }

    int alturaPico = (int)((pico[b] / umbralAj) * (maxH - 2));
    alturaPico = constrain(alturaPico, 0, maxH - 2);
    if (alturaPico > 2) {
      display.drawFastHLine(x + 1, bottomY - alturaPico, barWidth - 2, SSD1306_WHITE);
    }
  }

  for (int b = 0; b < NUM_BANDS; b++) {
    int x = startX + b * (barWidth + spacing);
    display.setCursor(x + 2, 62);
    display.print(bandLabels[b]);
  }

  display.display();
}

// ---------- Dibujar espectro ----------
void dibujarEspectro() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("VU - ESPECTRO");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  int topY    = 14;
  int bottomY = 60;
  int maxH    = bottomY - topY;

  int xPos[NUM_BANDS];
  int yPos[NUM_BANDS];
  int yPico[NUM_BANDS];

  int anchoTotal = 124;
  int pasoX = anchoTotal / (NUM_BANDS - 1);

  for (int b = 0; b < NUM_BANDS; b++) {
    xPos[b] = 2 + b * pasoX;

    float umbralAj = umbralAuto[b] * MARGEN_VISUAL;
    if (umbralAj < UMBRAL_MINIMO) umbralAj = UMBRAL_MINIMO;

    int altura = (int)((nivelSuave[b] / umbralAj) * (maxH - 2));
    altura = constrain(altura, 0, maxH - 2);
    yPos[b] = bottomY - altura;

    int alturaPico = (int)((pico[b] / umbralAj) * (maxH - 2));
    alturaPico = constrain(alturaPico, 0, maxH - 2);
    yPico[b] = bottomY - alturaPico;
  }

  // Relleno debajo de la linea
  for (int b = 0; b < NUM_BANDS - 1; b++) {
    for (int x = xPos[b]; x < xPos[b+1]; x++) {
      int t = (x - xPos[b]) * 100 / (xPos[b+1] - xPos[b]);
      int y = yPos[b] + (yPos[b+1] - yPos[b]) * t / 100;
      display.drawLine(x, y, x, bottomY, SSD1306_WHITE);
    }
  }

  // Linea superior
  for (int b = 0; b < NUM_BANDS - 1; b++) {
    display.drawLine(xPos[b], yPos[b], xPos[b+1], yPos[b+1], SSD1306_WHITE);
  }

  // Picos
  for (int b = 0; b < NUM_BANDS - 1; b++) {
    display.drawLine(xPos[b], yPico[b], xPos[b+1], yPico[b+1], SSD1306_WHITE);
  }

  // Etiquetas
  for (int b = 0; b < NUM_BANDS; b++) {
    display.setCursor(xPos[b] - 4, 62);
    display.print(bandLabels[b]);
  }

  display.display();
}