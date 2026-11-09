# FireGuard-RT

Nút báo cháy tự động chạy Zephyr RTOS trên ESP32. FireGuard-RT tổng hợp ánh sáng, khói/gas và nhiệt để phân biệt cháy với rò gas, ngắt điện và kích hoạt thiết bị ứng phó trong vòng 1 giây, tự giám sát lỗi của từng cảm biến và báo cáo về CoreIoT.

[![build](https://github.com/Nguyen-Hao-Khang/fire-guard-rt/actions/workflows/build.yml/badge.svg)](https://github.com/Nguyen-Hao-Khang/fire-guard-rt/actions/workflows/build.yml)

| | |
|---|---|
| Vi điều khiển | ESP32-WROOM-32 (NodeMCU 30 chân, không PSRAM) |
| Hệ điều hành | Zephyr RTOS v4.4.0, Zephyr SDK 1.0.1 |
| Board target | `esp32_devkitc/esp32/procpu` |
| Mô phỏng | Wokwi for VS Code |
| Nền tảng IoT | CoreIoT (ThingsBoard 4.2, MQTT) |
| Giao diện web | [nguyen-hao-khang.github.io/fire-guard-rt](https://nguyen-hao-khang.github.io/fire-guard-rt/), mã nguồn trong `web/` |

## Tính năng

- **Phát hiện và phân loại.** LDR, MQ-2 và DHT22 đóng vai đầu báo lửa, khói/gas và nhiệt. Firmware chỉ báo cháy khi hai nguồn xác nhận nhau trong cửa sổ 4 s, trừ khi nhiệt hoặc gas vượt ngưỡng tuyệt đối.
- **Ứng phó.** Khi báo động, relay cắt điện khu vực, quạt hút khói chạy theo PWM (100 % khi cháy, 60–100 % theo nồng độ gas), còi phát âm theo loại sự cố và LED RGB chỉ hướng thoát hiểm.
- **Hỗ trợ cứu hộ.** HC-SR04 quét vùng nguy hiểm khi có báo động và gắn cờ khi phát hiện người còn chuyển động.
- **Xác thực Chỉ huy.** Chỉ thẻ RFID nằm trong danh sách Chỉ huy mới tắt được báo động. Không lệnh từ xa nào tắt được báo động.
- **Chịu lỗi.** Task Watchdog giám sát từng luồng. Luồng cảm biến bị treo được cô lập và khởi động lại; luồng lõi bị treo khiến chip khởi động lại và khôi phục trạng thái báo động.
- **Lưu trữ.** Cấu hình và 64 sự kiện gần nhất nằm trên flash (NVS) và còn nguyên sau khi khởi động lại.
- **Thời gian thực.** Firmware đo thời gian thực thi và thời gian đáp ứng của từng luồng, chạy phân tích RTA trên số đo, ghi trace lập lịch và tự đo độ trễ dưới tải CPU hoặc tải mạng.
- **Giám sát từ xa.** Telemetry gửi thưa khi bình thường, dày khi có bất thường. Trang web đọc dữ liệu trực tiếp từ CoreIoT, cho phép chỉnh ngưỡng và chạy diễn tập.

Yêu cầu chi tiết và tiêu chí nghiệm thu: [`docs/SRS.md`](docs/SRS.md).

## Bắt đầu nhanh

Bạn cần một west workspace Zephyr v4.4.0, toolchain ESP32 của Zephyr SDK và extension Wokwi cho VS Code. [`docs/getting-started.md`](docs/getting-started.md) hướng dẫn cài đặt từng bước.

```sh
cd <workspace>/applications
git clone https://github.com/Nguyen-Hao-Khang/fire-guard-rt.git fire_guard_rt
cd fire_guard_rt
west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf"
```

Mở thư mục `fire_guard_rt` trong VS Code, chọn `F1` > **Wokwi: Start Simulator**. Sau khoảng 5 s làm nóng, Serial Monitor in `WARMUP -> NORMAL` và LED RGB chuyển xanh lá.

## Cấu hình build

| Mục đích | Fragment thêm vào `EXTRA_CONF_FILE` |
|---|---|
| Mô phỏng Wokwi | `sim.conf` |
| Mô phỏng Wokwi kết nối CoreIoT | `sim.conf;comm.conf;secrets.conf` |
| Phần cứng | (không cần) |
| Phần cứng kết nối CoreIoT | `comm.conf;secrets.conf` |
| Phần cứng, đo thời gian thực | `rt.conf`, thêm `;comm.conf;secrets.conf` để đo khi có tải mạng |

`secrets.conf` chứa SSID, mật khẩu Wi-Fi và access token của device. Tạo file này từ `secrets.conf.example`; git bỏ qua nó.

## Cấu trúc thư mục

```
fire_guard_rt/
├── src/
│   ├── main.c          Khởi tạo và khởi chạy các luồng
│   ├── app/            Kênh zbus, cấu hình, lưu trữ, shell, chẩn đoán, benchmark
│   ├── fusion/         Luật tổng hợp cảm biến và máy trạng thái
│   ├── sensors/        LDR, MQ-2, DHT22, HC-SR04, RFID
│   ├── drivers/        Driver MFRC522 qua SPI
│   ├── actuators/      Relay, quạt, còi, LED RGB
│   ├── safety/         Watchdog, quản lý lỗi, nhật ký sự kiện, đo thời gian thực
│   └── comm/           Wi-Fi và MQTT tới CoreIoT
├── boards/             Devicetree overlay (phân bổ chân)
├── dts/bindings/       Binding cho RC522
├── tests/              Unit test trên native_sim
├── web/                Giao diện giám sát
├── wokwi/, diagram.json, wokwi.toml   Mạch mô phỏng và custom chip RC522
├── scripts/            Chuyển trace sang định dạng Perfetto
├── docs/               Tài liệu
├── .github/workflows/  CI
├── prj.conf, sim.conf, rt.conf, comm.conf, Kconfig
└── west.yml            Manifest cho workspace độc lập và CI
```

## Tài liệu

| Tài liệu | Nội dung |
|---|---|
| [`docs/getting-started.md`](docs/getting-started.md) | Cài đặt, build, chạy mô phỏng, nạp phần cứng, xử lý sự cố |
| [`docs/architecture.md`](docs/architecture.md) | Luồng, kênh dữ liệu, máy trạng thái, chịu lỗi, lưu trữ |
| [`docs/diagnostics.md`](docs/diagnostics.md) | Shell chẩn đoán, đo thời gian thực, RTA, tracing, benchmark |
| [`docs/coreiot-web.md`](docs/coreiot-web.md) | Thiết lập CoreIoT, giao thức dữ liệu, giao diện web |
| [`docs/wiring.md`](docs/wiring.md) | Đấu nối phần cứng thật |
| [`docs/SRS.md`](docs/SRS.md) | Đặc tả yêu cầu: FR, NFR, use case |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Quy ước nhánh, commit, pull request |

## Kiểm thử

Workflow `build` của GitHub Actions build ba cấu hình (mô phỏng, phần cứng, phần cứng kèm CoreIoT) và chạy unit test trên mỗi pull request và mỗi lần push lên `main`. Chạy unit test tại máy (Linux hoặc WSL):

```sh
west twister -T tests -p native_sim/native/64
```

`tests/fusion` kiểm tra luật phát hiện và máy trạng thái; `tests/storage` kiểm tra việc lưu cấu hình và nhật ký qua lần khởi động lại.

Workflow `pages` đưa thư mục `web/` lên GitHub Pages mỗi khi `main` có thay đổi trong `web/`.

## Trạng thái dự án

| Hạng mục | Trạng thái |
|---|---|
| Firmware đơn nhân (UP), mô phỏng Wokwi | Hoàn thành |
| Lưu trữ flash, PWM, CI, đo thời gian thực | Hoàn thành, đang kiểm thử |
| Giao diện web, trang hướng dẫn, GitHub Pages | Hoàn thành, đang kiểm thử |
| Chạy trên phần cứng thật, đo số liệu chính thức | Chưa thực hiện |
| Tách lõi an toàn sang APP_CPU (AMP) | Kế hoạch |

Phiên bản hiện tại chạy toàn bộ luồng trên PRO_CPU vì driver Wi-Fi của Zephyr chưa hỗ trợ SMP. Firmware cách ly lõi an toàn với mạng bằng độ ưu tiên; [`docs/architecture.md`](docs/architecture.md) mô tả chi tiết và hướng chuyển sang AMP.
