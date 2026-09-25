# 🎵 Analizador de Espectro con ESP32-S3

Sistema de análisis de audio en tiempo real con FFT, pantalla OLED y transmisión WiFi al celular.

## ⚙️ Hardware

- ESP32-S3-N16R8
- Micrófono I2S INMP441
- Pantalla OLED SSD1306 (128x64)
- Botón para cambio de modo

## 🔌 Conexiones

| Componente | Pin ESP32-S3 |
|------------|--------------|
| INMP441 WS | GPIO 4 |
| INMP441 SCK | GPIO 5 |
| INMP441 SD | GPIO 6 |
| OLED SDA | GPIO 8 |
| OLED SCL | GPIO 9 |
| Botón | GPIO 10 |

## ✨ Funciones

- FFT de 1024 muestras con 8 bandas de frecuencia
- Visualización en OLED (modo barras y espectro)
- Auto-ganancia por banda
- Cambio de modo con botón físico

## 🚀 Cómo usar

1. Cargar el código en el ESP32-S3
2. Conectar los componentes según la tabla
3. Poner música y ver el espectro en la pantalla OLED
4. Pulsar el botón para cambiar entre modo barras y espectro

## 🛠️ Tecnologías

- C++ / Arduino
- ESP32-S3
- I2S
- FFT (arduinoFFT)
- Pantalla OLED SSD1306
