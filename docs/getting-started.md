# Bắt đầu

Tài liệu này dẫn bạn từ máy trống tới firmware chạy trên Wokwi và trên board thật. Các lệnh dùng cú pháp chung cho Windows (PowerShell, cmd) và Linux; chỗ nào khác nhau đều có ghi chú.

Quy ước:

- `<workspace>`: thư mục west workspace của Zephyr, thường tên `zephyrproject`.
- `<zephyr-sdk>`: thư mục cài Zephyr SDK.
- Mọi lệnh chạy trong terminal đã kích hoạt môi trường Python ảo của workspace (dấu nhắc có `(.venv)`).

## 1. Cài đặt

### 1.1 Zephyr workspace

Làm theo [Getting Started Guide của Zephyr 4.4.0](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html), dùng `west init --mr v4.4.0` để cố định phiên bản.

Nếu bạn chỉ cần build FireGuard-RT, `west.yml` của dự án tạo một workspace gọn hơn, chỉ gồm Zephyr và các module ESP32 cần thiết. CI dựng môi trường theo cách này:

```sh
west init -m https://github.com/Nguyen-Hao-Khang/fire-guard-rt.git fireguard-ws
cd fireguard-ws
west update --narrow -o=--depth=1
```

### 1.2 Toolchain ESP32

Zephyr SDK 1.0.1 cần thêm toolchain `xtensa-espressif_esp32_zephyr-elf`:

```bat
cd <zephyr-sdk>
setup.cmd /t xtensa-espressif_esp32_zephyr-elf
```

Trên Linux, chạy `./setup.sh -t xtensa-espressif_esp32_zephyr-elf`. Trên Windows, `setup.cmd` gọi `7z` để giải nén, nên bạn cần cài 7-Zip và thêm nó vào `PATH` trước. Cài xong, thư mục `<zephyr-sdk>/gnu/xtensa-espressif_esp32_zephyr-elf` phải tồn tại.

### 1.3 Thư viện Espressif

Driver ESP32 cần blob nhị phân của `hal_espressif`, kể cả khi bạn không dùng Wi-Fi:

```sh
cd <workspace>
west blobs fetch hal_espressif
west packages pip --install
```

### 1.4 Mã nguồn

Đặt dự án bên trong workspace để `west` tìm thấy Zephyr:

```sh
cd <workspace>/applications
git clone https://github.com/Nguyen-Hao-Khang/fire-guard-rt.git fire_guard_rt
```

### 1.5 VS Code

| Extension | Công dụng |
|---|---|
| Wokwi Simulator (`wokwi.wokwi-vscode`) | Chạy mô phỏng. Lần đầu, chọn `F1` > *Wokwi: Request a New License* để lấy license miễn phí |
| C/C++ (`ms-vscode.cpptools`) | IntelliSense, debug qua GDB |
| DeviceTree (`plorefice.devicetree`) | Tô màu file `.overlay` |
| Kconfig for the Zephyr Project (`nordic-semiconductor.nrf-kconfig`) | Tô màu file `.conf` |

## 2. Build

Chạy trong thư mục `<workspace>/applications/fire_guard_rt`:

| Cấu hình | Lệnh |
|---|---|
| Mô phỏng | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf"` |
| Mô phỏng + CoreIoT | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf;comm.conf;secrets.conf"` |
| Phần cứng | `west build -p -b esp32_devkitc/esp32/procpu` |
| Phần cứng + CoreIoT | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=comm.conf;secrets.conf"` |
| Phần cứng, đo thời gian thực | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=rt.conf"` |

Giữ nguyên dấu ngoặc kép quanh `-DEXTRA_CONF_FILE=...`. Thiếu chúng, PowerShell tách `sim.conf` thành hai đối số và coi dấu `;` là hết câu lệnh.

Build thành công tạo ra `build/zephyr/zephyr.elf`, `build/zephyr/zephyr.bin` và `build/flasher_args.json`.

| Fragment | Nội dung |
|---|---|
| `prj.conf` | Cấu hình chung: zbus, Task Watchdog, settings/NVS, PWM, shell |
| `sim.conf` | Đảo chiều đọc LDR theo mô hình Wokwi, làm nóng MQ-2 5 s thay vì 60 s, log tối giản in thẳng ra UART, stack sentinel, công cụ đo thời gian thực |
| `rt.conf` | Công cụ đo thời gian thực cho phần cứng: tracing, tải giả lập, benchmark, `fg threads` |
| `comm.conf` | Network stack, Wi-Fi, MQTT, kích thước heap; đặt luồng mạng dưới mọi luồng an toàn |
| `secrets.conf` | SSID, mật khẩu Wi-Fi, host và access token CoreIoT. Tạo từ `secrets.conf.example`, không commit |

Tham số ứng dụng (`CONFIG_FG_*`) nằm trong `Kconfig`, ví dụ `CONFIG_FG_COMMANDER_UIDS` cho danh sách thẻ Chỉ huy và `CONFIG_FG_BUZZER_ACTIVE` cho còi chủ động.

## 3. Chạy trên Wokwi

1. Build cấu hình mô phỏng.
2. Mở thư mục `fire_guard_rt` trong VS Code, chọn `F1` > **Wokwi: Start Simulator**. Wokwi đọc `wokwi.toml` và `diagram.json`, nạp firmware theo `build/flasher_args.json` giống chip thật.
3. Serial Monitor hiển thị:

```
*** Booting Zephyr OS build v4.4.0 ***
I: FireGuard-RT v0.1.0, reset cause 0x00000008
I: storage ready, boot #1, 0 events restored
I: 8 threads started
I: INIT -> WARMUP (boot)
I: MFRC522 ready, VersionReg=0x92
I: WARMUP -> NORMAL (warmup_done)
```

LED RGB nháy xanh dương trong lúc làm nóng rồi chuyển xanh lá. LED trắng "MAINS LOAD" sáng nghĩa là relay chưa cắt điện.

Nếu MQ-2 mô phỏng xuất phát trên 2500 mV, hệ thống vào `ALARM_GAS` ngay. Giảm `ppm` của MQ-2, rồi đặt `card` của RC522 thành 1 và trở về 0 để quay lại `NORMAL`.

### 3.1 Tương tác với linh kiện

Bấm vào linh kiện trên sơ đồ để chỉnh thông số:

| Linh kiện | Thông số | Tác dụng |
|---|---|---|
| Photoresistor (LDR) | `lux` | Tăng từ 200 lên 5000 để giả lập ánh sáng ngọn lửa |
| Gas sensor (MQ-2) | `ppm` | Thay đổi điện áp khói/gas, xem bằng `fg status` |
| DHT22 | `temperature` | Từ 57 °C trở lên: vượt ngưỡng nhiệt tuyệt đối |
| HC-SR04 | `distance` | Kéo qua lại khi đang báo động để giả lập người chuyển động |
| RC522 (custom chip) | `card` | 0: không thẻ, 1: thẻ Chỉ huy `DEADBEEF`, 2: thẻ lạ `12345678` |
| RC522 (custom chip) | `disconnected` | 1: đầu đọc mất kết nối |

Độ sáng LED "FAN" phản ánh duty PWM của quạt. Còi thụ động phát âm khoảng 2,7 kHz.

### 3.2 Debug bằng GDB

`wokwi.toml` mở GDB server ở cổng 3333. Cấu hình **Wokwi GDB (ESP32 Zephyr)** trong `.vscode/launch.json` dùng gdb của Zephyr SDK và đọc đường dẫn SDK từ biến môi trường `ZEPHYR_SDK_INSTALL_DIR`. Đặt biến này trước khi mở VS Code. Wokwi không cung cấp thanh ghi Xtensa cho GDB, nên bạn đặt được breakpoint và xem biến, còn backtrace thì không.

## 4. Chạy trên phần cứng

```sh
west build -p -b esp32_devkitc/esp32/procpu
west flash
west espressif monitor
```

Board target `esp32_devkitc` mô tả module ESP32-WROVER-E. Overlay của dự án giải phóng GPIO16/17 (vốn dành cho PSRAM) để điều khiển LED RGB, nên firmware này chỉ chạy trên module không có PSRAM như ESP32-WROOM-32.

Trước khi cấp nguồn, kiểm tra cầu phân áp của MQ-2 và chân ECHO của HC-SR04, cùng mạch MOSFET của quạt theo [`wiring.md`](wiring.md).

## 5. Unit test

Unit test chạy trên `native_sim`, cần Linux hoặc WSL:

```sh
west twister -T tests -p native_sim/native/64
```

| Bộ test | Phạm vi | Số test |
|---|---|---|
| `tests/fusion` | Luật phát hiện, cửa sổ xác nhận, chế độ suy giảm, thẻ Chỉ huy | 12 |
| `tests/storage` | Lưu và khôi phục cấu hình, nhật ký sự kiện, số lần khởi động | 5 |

## 6. Xử lý sự cố

| Triệu chứng | Cách xử lý |
|---|---|
| `xtensa-espressif_esp32_zephyr-elf-gcc not found` | Cài toolchain theo mục 1.2. Nếu `setup.cmd` dừng mà không báo lỗi, kiểm tra `7z` trong `PATH`. |
| `Blob ... missing. Update with: west blobs fetch hal_espressif` | Chạy lệnh ở mục 1.3. |
| `File not found: .../sim` | Đặt `"-DEXTRA_CONF_FILE=..."` trong ngoặc kép. |
| Wokwi lặp lại `rst:0x3 (SW_RESET)` | Wokwi đang nạp `.bin` như ảnh ứng dụng. `wokwi.toml` phải trỏ tới `build/flasher_args.json`. |
| `ASSERTION FAIL ... Unsupported pin` khi khởi động | Chân GPIO nằm trong vùng dành riêng của board. Kiểm tra `gpio-reserved-ranges` trong overlay. |
| `sensor dht22 IMPLAUSIBLE FAULT` | Kiểm tra dây DHT22 và điện trở kéo lên. Chân dữ liệu khai báo `GPIO_ACTIVE_LOW`. |
| `Wi-Fi: no IP yet` trên Wokwi | Wokwi cần Internet qua IoT Gateway. Phần phát hiện và ứng phó tại chỗ vẫn chạy bình thường. |
| Giá trị cảm biến hiển thị đúng nhưng web không cập nhật | Kiểm tra log có `MQTT connected to CoreIoT` và token trong `secrets.conf` khớp device đang chọn trên web. |
