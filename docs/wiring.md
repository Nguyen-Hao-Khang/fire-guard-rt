# Đấu nối phần cứng thật

Phân bổ chân được định nghĩa trong `boards/esp32_devkitc_procpu.overlay`. Mạch mô phỏng tương ứng: `diagram.json`.

| Linh kiện | Chân linh kiện | ESP32 | Ghi chú |
|---|---|---|---|
| DHT22 | VCC / DATA / GND | 3V3 / GPIO4 / GND | Điện trở kéo lên 10 kΩ giữa DATA và 3V3 (module 3 chân thường có sẵn) |
| MQ-2 | VCC / AO / GND | 5V (VIN) / GPIO34 / GND | AO ra tới 5 V: cầu phân áp 10 kΩ (trên) + 20 kΩ (dưới) → tối đa 3,3 V |
| LDR | — | GPIO35 | Cầu phân áp LDR + 10 kΩ giữa 3V3 và GND. LDR ở phía GND thì sáng → điện áp cao; LDR ở phía 3V3 thì bật `CONFIG_FG_LDR_INVERT` |
| HC-SR04 | VCC / TRIG / ECHO / GND | 5V / GPIO27 / GPIO26 / GND | ECHO 5 V: phân áp 1 kΩ + 2 kΩ |
| RC522 | 3.3V / SDA / SCK / MOSI / MISO / RST / GND | 3V3 / GPIO5 / GPIO18 / GPIO23 / GPIO19 / GPIO22 / GND | Chỉ cấp 3,3 V. IRQ để trống |
| Relay 1 kênh | VCC / IN / GND | 5V / GPIO13 / GND | Tải nối qua COM–NC: relay kích thì cắt tải. Module kích mức thấp: đổi cờ thành `GPIO_ACTIVE_LOW` trong overlay |
| Quạt 5 V | — | GPIO33 → MOSFET | PWM 25 kHz (LEDC kênh 0). MOSFET logic-level (hoặc 2N2222 + 1 kΩ ở cực B); diode 1N4007 song song ngược với quạt; nguồn quạt lấy từ 5 V |
| Buzzer | + / − | GPIO25 / GND | PWM 2,7 kHz (LEDC kênh 1) cho còi thụ động; còi chủ động thì bật `CONFIG_FG_BUZZER_ACTIVE`. Dòng > 20 mA thì đệm qua transistor |
| LED RGB (catot chung) | R / G / B / K | GPIO16 / GPIO17 / GPIO21 / GND | Mỗi kênh 220–330 Ω. Anot chung: đổi sang `GPIO_ACTIVE_LOW` |

Lưu ý:

- MQ-2 và LDR phải dùng ADC1 (GPIO32–39). ADC2 không dùng được khi bật Wi-Fi.
- I2C0 và SPI2 (HSPI) bị tắt trong overlay vì trùng chân với RGB-B, RC522-RST và relay.
- Quạt và relay khởi động gây sụt áp: thêm tụ 470 µF giữa 5 V và GND gần tải.
