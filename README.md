# FireGuard-RT

**Hệ thống cảnh báo và xử lý đám cháy thời gian thực đa tầng, tích hợp giám sát lỗi, trên Zephyr RTOS**

| | |
|---|---|
| Vi điều khiển | ESP32-WROOM-32 (NodeMCU 30 chân) |
| Hệ điều hành | Zephyr RTOS v4.4.0, Zephyr SDK 1.0.1 |
| Board target | `esp32_devkitc/esp32/procpu` |
| Mô phỏng | Wokwi for VS Code |
| Nền tảng IoT | CoreIoT (MQTT, mô hình dữ liệu ThingsBoard) |

## Mục lục

1. [Tổng quan](#1-tổng-quan)
2. [Phần cứng](#2-phần-cứng)
3. [Cấu trúc mã nguồn](#3-cấu-trúc-mã-nguồn)
4. [Cài đặt môi trường](#4-cài-đặt-môi-trường)
5. [Build](#5-build)
6. [Chạy mô phỏng Wokwi](#6-chạy-mô-phỏng-wokwi)
7. [Nạp và chạy trên phần cứng](#7-nạp-và-chạy-trên-phần-cứng)
8. [Kết nối CoreIoT](#8-kết-nối-coreiot)
9. [Giao diện giám sát web](#9-giao-diện-giám-sát-web)
10. [Shell chẩn đoán](#10-shell-chẩn-đoán)
11. [Kiến trúc phần mềm](#11-kiến-trúc-phần-mềm)
12. [Kiểm thử](#12-kiểm-thử)
13. [Xử lý sự cố](#13-xử-lý-sự-cố)
14. [Tài liệu liên quan](#14-tài-liệu-liên-quan)

## 1. Tổng quan

FireGuard-RT là nút báo cháy tự động cho một khu vực. Hệ thống:

- Tổng hợp dữ liệu từ LDR (ánh sáng ngọn lửa), MQ-2 (khói, gas) và DHT22 (nhiệt) để phân loại **cháy** và **rò gas**, đồng thời loại trừ báo động giả bằng xác nhận đa nguồn.
- Khi có sự cố: ngắt relay điện lưới, bật quạt hút khói, kích còi, LED RGB chỉ dẫn thoát hiểm; HC-SR04 phát hiện người còn chuyển động trong vùng nguy hiểm.
- Chỉ thẻ RFID "Chỉ huy" mới tắt được báo động.
- Mỗi luồng cảm biến được giám sát bằng Task Watchdog; cảm biến lỗi bị cô lập, hệ thống chuyển sang chế độ suy giảm và phát cảnh báo bảo trì.
- Gửi telemetry thích nghi lên CoreIoT và nhận cấu hình, lệnh diễn tập từ xa.

Thời gian phát hiện tuân theo TCVN 5738:2021: lửa ≤ 5 s, khói/gas ≤ 30 s, nhiệt ≤ 120 s. Thời gian từ khi quyết định báo động đến khi cơ cấu chấp hành tác động ≤ 1 s, không phụ thuộc tải mạng.

## 2. Phần cứng

| Thành phần | Vai trò | GPIO |
|---|---|---|
| LDR | Ánh sáng ngọn lửa | 35 (ADC1_CH7) |
| MQ-2 | Khói, gas | 34 (ADC1_CH6), qua cầu phân áp |
| DHT22 | Nhiệt độ, độ ẩm | 4 |
| HC-SR04 | Phát hiện chuyển động | TRIG 27, ECHO 26 (qua cầu phân áp) |
| RC522 | Xác thực thẻ Chỉ huy | SCK 18, MISO 19, MOSI 23, SDA 5, RST 22 |
| Relay 1 kênh | Ngắt điện lưới | 13 |
| Quạt 5 V qua MOSFET | Hút khói, PWM 25 kHz theo mức gas | 33 |
| Buzzer | Còi báo động, PWM 2,7 kHz | 25 |
| LED RGB | Trạng thái, chỉ dẫn thoát hiểm | R 16, G 17, B 21 |

Sơ đồ đấu nối, cầu phân áp và mạch MOSFET: [`docs/wiring.md`](docs/wiring.md). Phân bổ chân được khai báo trong [`boards/esp32_devkitc_procpu.overlay`](boards/esp32_devkitc_procpu.overlay).

## 3. Cấu trúc mã nguồn

```
fire_guard_rt/
├── CMakeLists.txt                 Danh sách nguồn, sinh build/flasher_args.json cho Wokwi
├── Kconfig                        Tham số ứng dụng (CONFIG_FG_*)
├── west.yml                       Manifest cho workspace độc lập và CI
├── .github/workflows/             CI: build và unit test
├── prj.conf                       Cấu hình Zephyr chung
├── sim.conf                       Fragment cho mô phỏng Wokwi
├── rt.conf                        Fragment cho đo thời gian thực trên phần cứng
├── comm.conf                      Fragment Wi-Fi + MQTT
├── secrets.conf.example           Mẫu thông tin đăng nhập (secrets.conf không được commit)
├── boards/                        Devicetree overlay
├── dts/bindings/                  Binding cho RC522
├── diagram.json, wokwi.toml       Mạch và cấu hình mô phỏng
├── wokwi/chips/                   Custom chip RC522 cho Wokwi
├── scripts/                       Chuyển dữ liệu tracing sang Perfetto
├── src/
│   ├── main.c                     Khởi tạo, khởi chạy bảng thread
│   ├── app/                       Kênh zbus, cấu hình, lưu trữ, bảng thread, shell, chẩn đoán, benchmark
│   ├── fusion/                    Tổng hợp cảm biến và máy trạng thái
│   ├── sensors/                   LDR, MQ-2, DHT22, HC-SR04, RFID
│   ├── drivers/                   Driver MFRC522 qua SPI
│   ├── actuators/                 Relay, quạt, còi, LED RGB, đo độ trễ phản ứng
│   ├── safety/                    Task Watchdog, fault manager, nhật ký sự kiện, đo và phân tích thời gian thực
│   └── comm/                      Wi-Fi + MQTT CoreIoT
├── web/                           Giao diện giám sát (HTML, CSS, JavaScript tĩnh)
├── tests/                         Unit test trên native_sim (fusion, storage)
└── docs/                          SRS, đấu nối
```

## 4. Cài đặt môi trường

Dự án dùng một **west workspace** Zephyr v4.4.0. Trong tài liệu này, `<workspace>` là thư mục workspace (thường là `zephyrproject`) và `<zephyr-sdk>` là thư mục cài Zephyr SDK. Nếu chưa có workspace, làm theo [Getting Started Guide](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html) với `west init --mr v4.4.0`.

Mọi lệnh dưới đây chạy trong terminal đã kích hoạt môi trường Python ảo của workspace (dấu nhắc có tiền tố `(.venv)`).

### 4.1 Toolchain cho ESP32

Zephyr SDK cần toolchain `xtensa-espressif_esp32_zephyr-elf`. Trên Windows, `setup.cmd` dùng `7z` để giải nén, nên 7-Zip phải có trong `PATH`.

```bat
cd <zephyr-sdk>
setup.cmd /t xtensa-espressif_esp32_zephyr-elf
```

Kiểm tra: thư mục `<zephyr-sdk>\gnu\xtensa-espressif_esp32_zephyr-elf` tồn tại.

### 4.2 Thư viện của Espressif

Driver ESP32 cần các thư viện nhị phân của `hal_espressif` (bắt buộc kể cả khi không dùng Wi-Fi) và các gói Python đi kèm:

```bat
cd <workspace>
west blobs fetch hal_espressif
west packages pip --install
```

### 4.3 Lấy mã nguồn dự án

Ứng dụng nằm bên trong workspace để `west` tự nhận ra Zephyr:

```bat
cd <workspace>\applications
git clone https://github.com/Nguyen-Hao-Khang/fire-guard-rt.git fire_guard_rt
```

Khi chưa có workspace, `west.yml` của dự án tạo một workspace tối giản (Zephyr v4.4.0 và các module ESP32 cần thiết), giống cách CI dựng môi trường:

```sh
west init -m https://github.com/Nguyen-Hao-Khang/fire-guard-rt.git fireguard-ws
cd fireguard-ws
west update --narrow -o=--depth=1
```

### 4.4 VS Code

| Extension | Công dụng |
|---|---|
| Wokwi Simulator (`wokwi.wokwi-vscode`) | Chạy mô phỏng (cần license miễn phí: `F1` → *Wokwi: Request a New License*) |
| C/C++ (`ms-vscode.cpptools`) | IntelliSense, debug qua GDB |
| DeviceTree (`plorefice.devicetree`) | Tô màu file `.overlay` |
| Kconfig for the Zephyr Project (`nordic-semiconductor.nrf-kconfig`) | Tô màu file `.conf` |

## 5. Build

Chạy từ thư mục `<workspace>/applications/fire_guard_rt`.

| Cấu hình | Lệnh |
|---|---|
| Mô phỏng Wokwi | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf"` |
| Mô phỏng Wokwi + CoreIoT | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf;comm.conf;secrets.conf"` |
| Phần cứng, không kết nối mạng | `west build -p -b esp32_devkitc/esp32/procpu` |
| Phần cứng + CoreIoT | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=comm.conf;secrets.conf"` |
| Phần cứng, đo thời gian thực | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=rt.conf"` (thêm `;comm.conf;secrets.conf` để đo khi có tải mạng) |

Trên PowerShell, tham số `-D...` phải đặt trong dấu ngoặc kép. Nếu không, PowerShell tách `sim.conf` thành `sim` và `.conf`, và dấu `;` kết thúc câu lệnh.

Build thành công sinh ra `build/zephyr/zephyr.elf`, `build/zephyr/zephyr.bin` và `build/flasher_args.json`.

| Fragment | Nội dung |
|---|---|
| `sim.conf` | Đảo chiều đọc LDR theo mô hình Wokwi, làm nóng MQ-2 5 s, log tối giản (in thẳng ra UART kể cả khi lỗi lúc khởi động), stack sentinel, công cụ đo thời gian thực |
| `rt.conf` | Công cụ đo thời gian thực cho phần cứng: tracing, tải giả lập, benchmark, `fg threads` |
| `comm.conf` | Network stack, Wi-Fi, MQTT; giữ thread mạng dưới mọi thread an toàn |
| `secrets.conf` | SSID, mật khẩu Wi-Fi, host và access token CoreIoT |

## 6. Chạy mô phỏng Wokwi

1. Build cấu hình mô phỏng (mục 5).
2. Mở thư mục `fire_guard_rt` trong VS Code, `F1` → **Wokwi: Start Simulator**. Wokwi đọc `wokwi.toml` và `diagram.json`, nạp `zephyr.bin` vào offset 0x1000 qua `build/flasher_args.json` giống chip thật.
3. Kết quả mong đợi trên Serial Monitor:

```
*** Booting Zephyr OS build v4.4.0 ***
I: FireGuard-RT v0.1.0, reset cause 0x00000008
I: 8 threads started
I: INIT -> WARMUP (boot)
I: MFRC522 ready, VersionReg=0x92
I: WARMUP -> NORMAL (warmup_done)
```

LED RGB nháy xanh dương trong giai đoạn làm nóng rồi chuyển xanh lá; LED trắng "MAINS LOAD" sáng (relay chưa ngắt).

Nếu giá trị mặc định của MQ-2 trên Wokwi vượt 2500 mV, hệ thống vào `ALARM_GAS` thay vì `NORMAL`. Giảm **ppm** của MQ-2, sau đó đặt **card** của RC522 = 1 rồi về 0 để về `NORMAL`.

### Tương tác với linh kiện mô phỏng

| Linh kiện | Thông số | Tác dụng |
|---|---|---|
| Photoresistor (LDR) | `lux` | Tăng từ 200 lên 5000: ánh sáng ngọn lửa |
| Gas sensor (MQ-2) | `ppm` | Thay đổi điện áp khói/gas (xem bằng `fg status`) |
| DHT22 | `temperature` | ≥ 57 °C: ngưỡng nhiệt tuyệt đối |
| HC-SR04 | `distance` | Kéo qua lại khi đang báo động: có người trong vùng |
| RC522 (custom chip) | `card` | 0: không có thẻ, 1: thẻ Chỉ huy `DEADBEEF`, 2: thẻ lạ `12345678` |
| RC522 (custom chip) | `disconnected` | 1: đầu đọc không phản hồi |

Buzzer phát âm 2,7 kHz (còi thụ động, kể cả trên Wokwi); với còi chủ động, bật `CONFIG_FG_BUZZER_ACTIVE`. Độ sáng LED "FAN" trên Wokwi thể hiện tốc độ quạt: 50 % khi diễn tập, 100 % khi cháy, 60–100 % theo mức gas khi rò gas.

### Debug bằng GDB

`wokwi.toml` mở GDB server ở cổng 3333. Cấu hình **Wokwi GDB (ESP32 Zephyr)** trong `.vscode/launch.json` gắn gdb của Zephyr SDK vào chip mô phỏng. Cấu hình này đọc vị trí SDK từ biến môi trường `ZEPHYR_SDK_INSTALL_DIR`, cần đặt biến này trước khi mở VS Code.

## 7. Nạp và chạy trên phần cứng

```bat
west build -p -b esp32_devkitc/esp32/procpu
west flash
west espressif monitor
```

Board target `esp32_devkitc` mô tả module ESP32-WROVER-E. Overlay của dự án giải phóng GPIO16/17 (dành cho PSRAM trên WROVER) để dùng cho LED RGB, nên firmware chỉ dùng được trên module **không có PSRAM** như ESP32-WROOM-32.

Trước khi cấp nguồn, kiểm tra cầu phân áp cho MQ-2 và ECHO của HC-SR04, và mạch MOSFET của quạt theo [`docs/wiring.md`](docs/wiring.md).

## 8. Kết nối CoreIoT

1. Trên CoreIoT, tạo một device (profile mặc định) và sao chép **access token**.
2. Sao chép `secrets.conf.example` thành `secrets.conf` và điền SSID, mật khẩu Wi-Fi, token. Mô phỏng Wokwi dùng mạng `Wokwi-GUEST` không mật khẩu.
3. Tạo các shared attribute trên device để cấu hình từ xa:

| Shared attribute | Mặc định | Miền hợp lệ |
|---|---|---|
| `sample_period_ms` | 1000 | 1000–2000 |
| `callin_period_s` | 60 | 10–600 |
| `th_light_delta_mv` | 800 | > 0 |
| `th_gas_rise_mv` | 400 | > 0 |
| `th_gas_warn_mv` | 1500 | > 0 |
| `th_gas_danger_mv` | 2500 | > `th_gas_warn_mv` |
| `th_temp_max_x10` | 570 | > 0 (0,1 °C) |
| `th_temp_rise_x10` | 30 | > 0 (0,1 °C trong 10 s) |
| `confirm_window_ms` | 4000 | 1000–4000 |

4. Build cấu hình có `comm.conf;secrets.conf` (mục 5).

| Hướng | Topic | Nội dung |
|---|---|---|
| Thiết bị → CoreIoT | `v1/devices/me/telemetry` | `state`, `light`, `gas`, `temp`, `hum`, `faults`, `trapped`; sự kiện `event`, `detail` |
| Thiết bị → CoreIoT | `v1/devices/me/attributes` | `fw_version`, `cfg_<khóa>` = `applied` / `rejected` |
| CoreIoT → thiết bị | `v1/devices/me/attributes` | Shared attributes ở bảng trên |
| CoreIoT → thiết bị | `v1/devices/me/rpc/request/+` | `startDrill` (`duration_s`), `stopDrill` |

Telemetry gửi mỗi `callin_period_s` khi bình thường và mỗi `sample_period_ms` khi có bất thường; sự kiện được gửi ngay. Không có lệnh từ xa nào tắt được báo động.

## 9. Giao diện giám sát web

Thư mục `web/` chứa một trang web tĩnh, không cần build hay máy chủ riêng. Trang gọi trực tiếp REST API và WebSocket của CoreIoT nên dùng chung tài khoản và device đã cấu hình ở mục 8.

| Trang | Nội dung |
|---|---|
| Overview | Trạng thái hệ thống, nguyên nhân và thời điểm chuyển trạng thái, số đo bốn cảm biến so với ngưỡng, đáp ứng của cơ cấu chấp hành suy ra từ trạng thái, sức khỏe cảm biến, người trong vùng nguy hiểm, sự kiện gần nhất. Cập nhật thời gian thực qua WebSocket. |
| History | Biểu đồ khí gas, ánh sáng, nhiệt độ, độ ẩm trong 1 giờ, 24 giờ hoặc 7 ngày (trung bình theo khoảng); nhật ký sự kiện có bộ lọc theo loại. |
| Settings | Sửa chín shared attribute với kiểm tra miền giá trị giống firmware, hiển thị kết quả `cfg_<khóa>` do thiết bị báo về và phiên bản firmware; bắt đầu hoặc dừng diễn tập qua RPC. |

Cách dùng:

1. Mở `web/index.html` bằng trình duyệt, hoặc phục vụ thư mục bằng một máy chủ tĩnh bất kỳ:

   ```
   python -m http.server 8080 --directory web
   ```

2. Đăng nhập theo một trong hai cách. Địa chỉ máy chủ mặc định là `https://app.coreiot.io` và có thể đổi cho máy chủ ThingsBoard khác.
   - **Password**: email và mật khẩu CoreIoT.
   - **Access token**: dành cho tài khoản đăng nhập bằng Google hoặc GitHub. Trên CoreIoT, mở *Account* > *Security*, bấm **Copy JWT token** rồi dán vào trang. Token có hiệu lực khoảng 2,5 giờ; muốn tự gia hạn, dán thêm giá trị `refresh_token` trong Local storage của tab CoreIoT.
3. Chọn device ở góc trên bên phải. Lựa chọn device và giao diện sáng/tối được ghi nhớ trên trình duyệt.

Phiên đăng nhập (JWT và refresh token) chỉ lưu trong `sessionStorage` của tab và bị xóa khi đóng tab hoặc đăng xuất. Lệnh RPC chỉ thành công khi thiết bị đang kết nối; nếu thiết bị offline, trang báo lỗi thay vì chờ. Trang cần Internet để tải phông chữ và Chart.js từ CDN.

## 10. Shell chẩn đoán

Shell chạy trên UART0, 115200 baud.

| Lệnh | Tác dụng |
|---|---|
| `fg status` | Trạng thái, giá trị cảm biến, cờ lỗi |
| `fg config [reset]` | Cấu hình đang dùng; `reset` khôi phục mặc định và xoá cấu hình đã lưu |
| `fg set <khóa> <giá trị>` | Đổi cấu hình (có kiểm tra miền) |
| `fg drill [giây\|stop]` | Diễn tập |
| `fg card <uid8hex>` | Giả lập quẹt thẻ |
| `fg hang <adc\|dht\|ultra\|rfid\|fusion\|act>` | Tiêm lỗi treo luồng để kiểm thử Task Watchdog |
| `fg latency [reset]` | Thống kê độ trễ phản ứng |
| `fg log` | Nhật ký sự kiện (kèm số lần khởi động `boot`) |
| `fg threads` | CPU, ưu tiên, trạng thái từng thread (khi bật `CONFIG_FG_DIAG_STATS`) |
| `fg rt [reset]` | Thời gian thực thi C và thời gian đáp ứng R đo được của từng thread an toàn |
| `fg rta` | Phân tích thời gian đáp ứng (RTA) trên C đo được |
| `fg trace start\|stop\|dump` | Ghi dòng thời gian lập lịch (Zephyr tracing) |
| `fg load cpu <%>` / `fg load net <Hz>` | Tạo tải CPU ưu tiên thấp / tải MQTT |
| `fg bench <n>` | Đo tự động độ trễ phát hiện và phản ứng qua n chu kỳ báo động |
| `kernel thread list` | Stack và ưu tiên mọi thread của kernel |

## 11. Kiến trúc phần mềm

Các luồng trao đổi dữ liệu qua kênh **zbus**. Thread nhận được gắn với kênh (`zbus_obs_attach_to_thread`) để thread phát tạm thời được nâng ưu tiên khi thông báo.

| Kênh | Nội dung | Phát | Nhận |
|---|---|---|---|
| `chan_adc` | LDR, MQ-2 | `fg_adc` | fusion |
| `chan_dht` | Nhiệt độ, độ ẩm | `fg_dht` | fusion |
| `chan_presence` | Người trong vùng nguy hiểm | `fg_ultra` | fusion |
| `chan_rfid` | Thẻ vừa quẹt | `fg_rfid`, shell | fusion, log |
| `chan_state` | Trạng thái hệ thống | fusion | actuators, log, comm, ultra |
| `chan_fault` | Lỗi cảm biến | fault manager, cảm biến | fusion, log |
| `chan_config` | Cấu hình (có validator) | shell, comm | fusion, cảm biến |
| `chan_cmd` | Lệnh diễn tập | shell, comm | fusion |

| Thread | Ưu tiên | Kích hoạt | Task Watchdog | Khi hết hạn |
|---|---|---|---|---|
| `fg_act` | 1 | Sự kiện `chan_state`, tick 50 ms | 1000 ms | Khởi động lại hệ thống |
| `fg_fault` | 2 | Hàng đợi lỗi, 250 ms | 1000 ms | Khởi động lại hệ thống |
| `fg_fusion` | 3 | Sự kiện | 1000 ms | Khởi động lại hệ thống |
| `fg_adc` | 4 | `sample_period_ms` | 500 ms | Cô lập cảm biến, khởi động lại thread |
| `fg_dht` | 5 | max(`sample_period_ms`, 2 s) | 500 ms | Cô lập cảm biến, khởi động lại thread |
| `fg_ultra` | 6 | 200 ms khi báo động | 500 ms | Cô lập cảm biến, khởi động lại thread |
| `fg_rfid` | 7 | 200 ms | 500 ms | Cô lập cảm biến, khởi động lại thread |
| `fg_log` | 9 | Sự kiện | | |
| `fg_comm` | 10 | Poll 100 ms | | |

Số ưu tiên nhỏ hơn là ưu tiên cao hơn. Thread của driver Wi-Fi được giới hạn ở `CONFIG_ESP32_WIFI_MAX_THREAD_PRIORITY=12`, dưới mọi thread an toàn.

**Lưu trữ không bay hơi** (`CONFIG_FG_PERSIST`, mặc định bật): cấu hình được chấp nhận và nhật ký 64 sự kiện gần nhất được lưu bằng settings/NVS trên phân vùng `storage_partition`. Sau khi khởi động lại, cấu hình được khôi phục, nhật ký giữ lịch sử của các lần khởi động trước, và các sự kiện chưa gửi được gửi lại lên CoreIoT. Việc ghi flash chỉ diễn ra trong thread `fg_log` (ưu tiên 9) vì thao tác này tạm dừng cache của cả hai nhân.

Máy trạng thái: `INIT → WARMUP → NORMAL ⇄ PRE_ALARM → ALARM_FIRE / ALARM_GAS`, thêm `DRILL` cho diễn tập. Chi tiết luật tổng hợp và hành vi cơ cấu chấp hành theo trạng thái: [`docs/SRS.md`](docs/SRS.md).

### Phân tích thời gian thực

Mỗi thread an toàn đánh dấu đầu và cuối của từng job. **C** là thời gian CPU chính thread đó dùng (bộ đếm của scheduler, không tính thời gian bị chiếm quyền); **R** là thời gian thực từ lúc job được kích hoạt đến lúc xong. Thời gian ghi flash, vốn dừng cache của cả hai nhân, được ghi nhận riêng làm hệ số chặn **B**.

`fg rta` lặp phương trình

```
R_i = C_i + B + Σ_{j ∈ hp(i)} ⌈R_i / T_j⌉ · C_j
```

với C bằng 1,2 lần giá trị lớn nhất đo được, rồi so R_i với hạn chót D_i và với R đo được. Mô hình T/D của từng thread nằm trong `src/safety/rt_stats.c`.

Quy trình đo gợi ý:

1. `fg rt reset`, sau đó `fg load cpu 50` (và `fg load net 20` nếu build có CoreIoT).
2. `fg bench 20`: 20 chu kỳ báo động qua đường dữ liệu thật (ghi đè số đo MQ-2 ở thread ADC, thẻ Chỉ huy để xoá báo động). Kết quả gồm độ trễ phát hiện và độ trễ phản ứng.
3. `fg rt` và `fg rta` để lấy bảng C, R và kết quả phân tích.
4. `fg trace start`, chờ vài giây, `fg trace dump`; lưu log serial rồi chuyển sang dòng thời gian:

```sh
python scripts/trace2perfetto.py capture.log trace.json
```

Mở `trace.json` tại [ui.perfetto.dev](https://ui.perfetto.dev). Số liệu chính thức nên lấy trên phần cứng; Wokwi không mô phỏng chính xác thời gian thực thi.

## 12. Kiểm thử

**CI**: workflow `.github/workflows/build.yml` chạy trên mỗi pull request và mỗi lần push lên `main`. Workflow dựng workspace từ `west.yml`, build ba cấu hình (mô phỏng, phần cứng, phần cứng + CoreIoT), chạy toàn bộ unit test bằng Twister và đính kèm file firmware.

**Unit test** chạy trên `native_sim` (Linux hoặc WSL): `tests/fusion` cho máy trạng thái, `tests/storage` cho lưu trữ cấu hình và nhật ký qua khởi động lại.

```sh
west twister -T tests -p native_sim/native/64
```

## 13. Xử lý sự cố

| Triệu chứng | Nguyên nhân và cách xử lý |
|---|---|
| `xtensa-espressif_esp32_zephyr-elf-gcc not found` | Chưa cài toolchain ESP32 (mục 4.1). Nếu `setup.cmd` dừng sớm, kiểm tra `7z` có trong `PATH`. |
| `Blob ... missing. Update with: west blobs fetch hal_espressif` | Chạy lệnh ở mục 4.2. |
| `File not found: .../sim` | PowerShell tách tham số; đặt `"-DEXTRA_CONF_FILE=..."` trong ngoặc kép. |
| Wokwi lặp `rst:0x3 (SW_RESET)` | Wokwi đang nạp file `.bin` như ảnh ứng dụng. `wokwi.toml` phải trỏ tới `build/flasher_args.json`. |
| `ASSERTION FAIL ... Unsupported pin` lúc khởi động | Chân GPIO nằm trong vùng dành riêng của board; kiểm tra `gpio-reserved-ranges` trong overlay. |
| `sensor dht22 IMPLAUSIBLE FAULT` | Kiểm tra đấu nối DHT22 và điện trở kéo lên; chân dữ liệu khai báo `GPIO_ACTIVE_LOW`. |
| `Wi-Fi: no IP yet` trên Wokwi | Kiểm tra mô phỏng có kết nối Internet (Wokwi IoT Gateway). |

## 14. Tài liệu liên quan

| Tài liệu | Nội dung |
|---|---|
| [`docs/SRS.md`](docs/SRS.md) | Đặc tả yêu cầu: FR, NFR, use case |
| [`docs/wiring.md`](docs/wiring.md) | Đấu nối phần cứng |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Quy ước nhánh, commit, comment |
