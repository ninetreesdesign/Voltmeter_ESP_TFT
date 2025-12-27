# Voltmeter_kV_TFT (v1.0.0)

A high-precision dual-mode voltmeter firmware designed specifically for the **Adafruit ESP32-S3 Feather TFT**. This instrument features a standard **3.3V Direct Mode** and a high-gain **kV Mode** designed for attenuated high-voltage measurements.



## Hardware Configuration

| Component | Pin / Assignment | Notes |
| :--- | :--- | :--- |
| **MCU** | ESP32-S3 | Feather TFT Variant |
| **Display** | Integrated ST7789 | 240x135 Color TFT |
| **Volt Input** | `A0` (GPIO 18) | Main measurement pin |
| **Mode Switch** | `A5` (GPIO 8) | Connect to **GND** for kV mode; leave open for V mode |
| **Status LED** | Pin 13 | Heartbeat LED |
| **NeoPixel** | Pin 40 | Set to Black (0,0,0) for power efficiency |

## 🚀 Key Features & Strategies

### 1. Dynamic Attenuation (Precision Gain)
The firmware automatically adjusts the internal ESP32-S3 ADC Programmable Gain Amplifier (PGA) based on the selected mode:
* **V Mode (Amber)**: Uses `ADC_11db` attenuation (0–3.3V range) for standard logic levels.
* **kV Mode (Orange)**: Uses `ADC_0db` (0–1.1V range) to maximize resolution for small attenuated signals from high-voltage dividers.

### 2. Zero-Flicker Overprinting
Instead of full-screen refreshes, the UI uses:
* **Background Overprinting**: The `u8g2` library draws characters with a solid black background, replacing pixels atomically.
* **Targeted Clears**: `tft.fillRect()` is used locally on units and headers during mode shifts to prevent ghosting.

### 3. Signal Processing
* **EMA Filtering**: Employs an Exponential Moving Average ($\alpha=0.50$) combined with 8x oversampling to ensure stable readings and suppress high-frequency noise.

## 📐 Conversion Equations

The firmware calculates the value based on the voltage present at `A0`.

| Mode | Input Range | Math Scaling | Display Unit |
| :--- | :--- | :--- | :--- |
| **Direct V** | 0.0 – 3.3V | $V_{out} = V_{pin}$ | **V** (Amber) |
| **kV Mode** | 0.0 – 0.5V | $V_{out} = V_{pin} \times 10$ | **kV** (Orange) |

*Note: The kV math assumes a 10,000:1 external resistor divider ($500V$ at the source becomes $0.05V$ at the pin).*



## Installation
1. Install **Adafruit_ST7735 and ST7789 Library**.
2. Install **U8g2_for_Adafruit_GFX**.
3. Install **Adafruit_NeoPixel**.
4. Set **Tools > USB CDC On Boot** to **Enabled** in the Arduino IDE.
5. Upload `Voltmeter_kV_TFT.ino`.