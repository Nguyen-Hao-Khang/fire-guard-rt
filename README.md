# FireGuard-RT

Hệ thống cảnh báo và xử lý đám cháy thời gian thực trên **Zephyr RTOS 4.4** cho ESP32 (NodeMCU 30 chân).
Đặc tả yêu cầu: [`docs/SRS.md`](docs/SRS.md). Sơ đồ đấu nối phần cứng thật: [`docs/wiring.md`](docs/wiring.md).

## Cấu trúc

```
fire_guard_rt/
├── CMakeLists.txt, Kconfig, prj.conf      cấu hình chung
├── sim.conf                               fragment cho bản mô phỏng Wokwi
├── comm.conf, secrets.conf.example        fragment Wi-Fi + MQTT CoreIoT
├── boards/esp32_devkitc_procpu.overlay    phân bổ chân (devicetree)
├── dts/bindings/nxp,mfrc522.yaml          binding cho RC522
├── diagram.json, wokwi.toml               mạch mô phỏng Wokwi
├── wokwi/chips/rc522.chip.{c,json,wasm}   custom chip RC522 cho Wokwi
├── src/
│   ├── main.c
│   ├── app/        channels (zbus), config, bảng thread, shell "fg"
│   ├── fusion/     fusion.c (C thuần, test được) + fusion_thread.c
│   ├── sensors/    ADC (LDR, MQ-2), DHT22, HC-SR04, RFID
│   ├── drivers/    rc522.c (driver SPI tối giản)
│   ├── actuators/  relay, quạt, còi, LED RGB, đo độ trễ phản ứng
│   ├── safety/     task watchdog + fault manager, event log
│   └── comm/       Wi-Fi + MQTT CoreIoT (chỉ build khi CONFIG_FG_COMM=y)
└── tests/fusion/   ztest cho máy trạng thái (native_sim)
```

## Chuẩn bị môi trường (Windows, làm một lần)

Các lệnh chạy trong thư mục `C:\EmbeddedLabs\zephyrproject` với venv đã kích hoạt.

```bat
:: 1. Toolchain Xtensa cho ESP32 (SDK hiện chỉ có arm-zephyr-eabi)
cd C:\EmbeddedLabs\zephyr-sdk-1.0.1
setup.cmd /t xtensa-espressif_esp32_zephyr-elf
:: hoặc: west sdk install -t xtensa-espressif_esp32_zephyr-elf

:: 2. Thư viện nhị phân của Espressif (bắt buộc, kể cả khi không dùng Wi-Fi)
cd C:\EmbeddedLabs\zephyrproject
west blobs fetch hal_espressif

:: 3. Gói Python của hal_espressif (esptool, ...)
west packages pip --install
```

## Build

Từ thư mục `zephyrproject\applications\fire_guard_rt`:

| Mục đích | Lệnh |
|---|---|
| Mô phỏng Wokwi | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf"` |
| Phần cứng thật, không mạng | `west build -p -b esp32_devkitc/esp32/procpu` |
| Phần cứng thật + CoreIoT | sao chép `secrets.conf.example` thành `secrets.conf`, điền token, rồi `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=comm.conf;secrets.conf"` |
| Unit test fusion (cần WSL/Linux) | `west twister -T tests -p native_sim/native/64` |

Trên PowerShell phải đặt tham số `-D...` trong dấu ngoặc kép, nếu không PowerShell sẽ tách `sim.conf` thành `sim` và `.conf`.

Nạp phần cứng thật: `west flash`, xem log: `west espressif monitor`.

## Chạy mô phỏng Wokwi (VS Code)

1. Build bản `sim.conf` (lệnh trên). Build sinh ra `build/flasher_args.json`, file này chỉ cho Wokwi nạp `zephyr.bin` vào offset 0x1000 giống chip thật.
2. Mở thư mục `fire_guard_rt` trong VS Code, `F1` → **Wokwi: Start Simulator** (đọc `wokwi.toml` và `diagram.json`).
3. Khởi động đúng sẽ in `*** Booting Zephyr OS ***`, `8 threads started`, `MFRC522 ready`. Sau 5 s làm nóng, với giá trị mặc định của MQ-2 trên Wokwi (khoảng 2,5 V) hệ thống vào `ALARM_GAS`: giảm ppm rồi quẹt thẻ Chỉ huy để về `NORMAL`.
4. Tương tác:

| Thử nghiệm | Thao tác trên Wokwi |
|---|---|
| Cháy bùng | Tăng **lux** của LDR (ví dụ 200 → 5000) và tăng **ppm** của MQ-2 cùng lúc |
| Báo giả (chỉ ánh sáng) | Chỉ tăng lux → PRE_ALARM rồi tự về NORMAL |
| Rò gas | Tăng ppm MQ-2 lên mức cao, không đổi lux |
| Nhiệt | Tăng nhiệt độ DHT22 lên trên 57 °C |
| Người mắc kẹt | Khi đang ALARM, kéo thanh distance của HC-SR04 qua lại nhiều lần |
| Thẻ Chỉ huy | Chỉnh `card` của RC522 = 1 (DEADBEEF), sau đó về 0 |
| Thẻ lạ | `card` = 2 (12345678) |
| Hỏng đầu đọc | `disconnected` = 1 |

Ghi chú:

- Buzzer của Wokwi là loại thụ động nên mức DC không phát ra tiếng; trạng thái còi xem qua log hoặc logic analyzer của Wokwi.
- Bản `sim.conf` dùng log tối giản (in thẳng ra UART, kể cả khi lỗi lúc khởi động) và bật `fg threads`.
- `wokwi.toml` mở GDB server ở cổng 3333; cấu hình "Wokwi GDB (ESP32 Zephyr)" trong `.vscode/launch.json` gắn gdb của Zephyr SDK vào chip mô phỏng.

## Shell chẩn đoán (UART 115200)

| Lệnh | Tác dụng |
|---|---|
| `fg status` | Trạng thái, giá trị cảm biến, cờ lỗi |
| `fg config` / `fg set <key> <value>` | Xem / đổi cấu hình (có kiểm tra miền) |
| `fg hang <adc\|dht\|ultra\|rfid\|fusion\|act>` | Tiêm lỗi treo luồng (kiểm thử Task Watchdog) |
| `fg card <uid8hex>` | Giả lập quẹt thẻ |
| `fg drill [giây\|stop]` | Diễn tập |
| `fg latency [reset]` | Thống kê độ trễ phản ứng (quyết định → chấp hành) |
| `fg log` | Nhật ký sự kiện |
| `fg threads` | CPU, ưu tiên, trạng thái từng thread và dữ liệu cảm biến mới nhất (bản `sim.conf`) |
| `kernel thread list` | Ưu tiên, stack của mọi thread (đầu vào RTA) |

## Kênh dữ liệu (zbus)

| Kênh | Kiểu | Phát | Nhận |
|---|---|---|---|
| `chan_adc` | `fg_adc_msg` | `fg_adc` | fusion |
| `chan_dht` | `fg_dht_msg` | `fg_dht` | fusion |
| `chan_presence` | `fg_presence_msg` | `fg_ultra` | fusion, log |
| `chan_rfid` | `fg_rfid_msg` | `fg_rfid`, shell | fusion, log |
| `chan_state` | `fg_state_msg` | fusion | actuators, log, comm, ultra |
| `chan_fault` | `fg_fault_msg` | fault manager, sensors | fusion, log |
| `chan_config` | `fg_config` (có validator) | shell, comm | fusion, sensors |
| `chan_cmd` | `fg_cmd_msg` | shell, comm (RPC) | fusion |

## Thread và Task Watchdog

| Thread | Ưu tiên | Kích hoạt | Task WDT | Khi hết hạn |
|---|---|---|---|---|
| `fg_act` | 1 | sự kiện `chan_state` + tick 50 ms | 1000 ms | reset hệ thống |
| `fg_fault` | 2 | hàng đợi lỗi + 250 ms | 1000 ms | reset hệ thống |
| `fg_fusion` | 3 | sự kiện | 1000 ms | reset hệ thống |
| `fg_adc` | 4 | `sample_period_ms` | 500 ms | cô lập + khởi động lại thread |
| `fg_dht` | 5 | max(sample, 2 s) | 500 ms | cô lập + khởi động lại thread |
| `fg_ultra` | 6 | 200 ms khi ALARM | 500 ms | cô lập + khởi động lại thread |
| `fg_rfid` | 7 | 200 ms | 500 ms | cô lập + khởi động lại thread |
| `fg_log` | 9 | sự kiện | — | — |
| `fg_comm` | 10 | 100 ms poll | — | — |

Driver Wi-Fi tạo thread theo `CONFIG_ESP32_WIFI_MAX_THREAD_PRIORITY=12` (dưới mọi thread an toàn).
Workqueue hệ thống (ưu tiên −1, cooperative) cần được đo như thành phần chặn B trong RTA.

## Trạng thái hiện thực

| Hạng mục | Trạng thái |
|---|---|
| Khung dự án, bảng thread, zbus, shell | Xong, build sạch (Zephyr 4.4.0) |
| Fusion + máy trạng thái | Xong, 12/12 unit test qua (`tests/fusion`) |
| Mô phỏng Wokwi | Khởi động, phát hiện rò gas, DHT22, RC522 và override bằng thẻ đã chạy |
| Task Watchdog + fault manager + tiêm lỗi | Xong, cần kiểm tra bằng `fg hang` trên Wokwi/HW |
| Driver RC522 + custom chip Wokwi | Xong, đã chạy trên Wokwi |
| MQTT CoreIoT (telemetry thích nghi, attributes, RPC) | Build được, chưa chạy với broker thật |
| Lưu cấu hình (NVS) và nhật ký trên flash | TODO |
| Zephyr Tracing + bảng RTA | TODO |
| AMP: chuyển lõi an toàn sang APP_CPU (cầu nối zbus ↔ IPM) | TODO (giai đoạn 2) |
| PWM quạt, mẫu còi theo tần số | TODO |
