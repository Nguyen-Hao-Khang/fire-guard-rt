# Đấu nối phần cứng

Bảng dưới đây khớp với `boards/esp32_devkitc_procpu.overlay` (phân bổ chân trong firmware) và `diagram.json` (mạch mô phỏng). Đổi chân ở một nơi thì cập nhật cả hai.

## 1. Bảng đấu nối

| Linh kiện | Chân linh kiện | ESP32 | Ghi chú |
|---|---|---|---|
| DHT22 | VCC, DATA, GND | 3V3, GPIO4, GND | Điện trở kéo lên 10 kΩ giữa DATA và 3V3. Module 3 chân thường có sẵn điện trở này |
| MQ-2 | VCC, AO, GND | 5V (VIN), GPIO34, GND | AO lên tới 5 V. Dùng cầu phân áp 10 kΩ (trên) và 20 kΩ (dưới) để giới hạn ở 3,3 V |
| LDR | | GPIO35 | Cầu phân áp LDR và 10 kΩ giữa 3V3 và GND. LDR phía GND: sáng thì điện áp cao. LDR phía 3V3: bật `CONFIG_FG_LDR_INVERT` |
| HC-SR04 | VCC, TRIG, ECHO, GND | 5V, GPIO27, GPIO26, GND | ECHO ra 5 V. Dùng cầu phân áp 1 kΩ và 2 kΩ |
| RC522 | 3.3V, SDA, SCK, MOSI, MISO, RST, GND | 3V3, GPIO5, GPIO18, GPIO23, GPIO19, GPIO22, GND | Chỉ cấp 3,3 V. Để trống IRQ |
| Relay 1 kênh | VCC, IN, GND | 5V, GPIO13, GND | Nối tải qua COM và NC để relay kích thì cắt tải. Module kích mức thấp: đổi cờ sang `GPIO_ACTIVE_LOW` trong overlay |
| Quạt 5 V | | GPIO33 qua MOSFET | PWM 25 kHz trên LEDC kênh 0. Dùng MOSFET logic-level (hoặc 2N2222 với 1 kΩ ở cực B), diode 1N4007 mắc ngược song song với quạt, nguồn quạt lấy từ 5 V |
| Còi | +, − | GPIO25, GND | PWM 2,7 kHz trên LEDC kênh 1 cho còi thụ động. Còi chủ động: bật `CONFIG_FG_BUZZER_ACTIVE`. Còi cần trên 20 mA thì đệm qua transistor |
| LED RGB catot chung | R, G, B, K | GPIO16, GPIO17, GPIO21, GND | Điện trở 220–330 Ω cho mỗi kênh. LED anot chung: đổi sang `GPIO_ACTIVE_LOW` |

## 2. Lưu ý

- Nối MQ-2 và LDR vào ADC1 (GPIO32–39). ADC2 không đọc được khi Wi-Fi bật.
- Overlay tắt I2C0 và SPI2 (HSPI) vì các bus này trùng chân với kênh B của LED RGB, chân RST của RC522 và relay.
- GPIO16 và GPIO17 chỉ dùng được trên module không có PSRAM (ESP32-WROOM-32).
- Quạt và relay gây sụt áp lúc khởi động. Đặt tụ 470 µF giữa 5 V và GND, sát tải.
- MQ-2 cần làm nóng khoảng 60 s sau khi cấp nguồn. Firmware ở trạng thái `WARMUP` trong thời gian này (`CONFIG_FG_WARMUP_S`).
