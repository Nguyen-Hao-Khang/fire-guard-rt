# Kế hoạch kiểm thử

Tài liệu quy định cách nhóm kiểm thử FireGuard-RT: kiểm thử những gì, trong môi trường nào, khi nào một kịch bản được coi là đạt, và ghi nhận kết quả, lỗi ra sao. Mỗi kịch bản truy ngược về yêu cầu trong [`SRS.md`](SRS.md).

Tài liệu này chỉ mô tả kịch bản và tiêu chí. Kết quả từng lần chạy ghi trong sheet *Kết quả kiểm thử* của file kế hoạch dự án; lỗi ghi thành GitHub Issue (mục 5).

## 1. Phạm vi

| Trong phạm vi | Ngoài phạm vi |
|---|---|
| Firmware trên Wokwi và trên ESP32-WROOM-32 thật | Chứng nhận PCCC, thử nghiệm với đám cháy thật quy mô lớn |
| Kết nối CoreIoT: telemetry, sự kiện, cấu hình, RPC | Hiệu năng của máy chủ CoreIoT |
| Giao diện web trên Chrome, Edge, Firefox bản mới | Trình duyệt cũ, ứng dụng di động |
| Unit test, CI | Kiểm thử bảo mật |

## 2. Môi trường và vai trò

| Môi trường | Dùng cho | Yêu cầu |
|---|---|---|
| Wokwi | Nhóm A–H, J | VS Code, extension Wokwi, bản build ở mục 3.1 |
| Linux hoặc WSL | Nhóm U | Zephyr workspace, Twister |
| GitHub | Nhóm I | Quyền push nhánh |
| Phần cứng | Nhóm W | Mạch lắp theo [`wiring.md`](wiring.md), cáp USB, nguồn 5 V |
| CoreIoT và web | Nhóm G, J, W6 | Tài khoản CoreIoT, device và access token riêng (xem [`coreiot-web.md`](coreiot-web.md)) |

| Vai trò | Trách nhiệm |
|---|---|
| Người kiểm thử | Chạy kịch bản được giao trong kế hoạch dự án, ghi kết quả, tạo issue khi không đạt |
| Người sửa lỗi | Mặc định là người phát hiện lỗi; nhờ chủ repository hỗ trợ khi không tự khắc phục được |
| Chủ repository | Review và merge bản sửa, theo dõi tiêu chí kết thúc ở mục 4 |

## 3. Chuẩn bị

### 3.1 Bản build

| Nhóm kịch bản | Lệnh build |
|---|---|
| A–F (cục bộ) | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf"` |
| G, J, H5 (CoreIoT) | `west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf;comm.conf;secrets.conf"` |
| P, H1–H4 | Bản build A–F (`sim.conf` đã bật tracing và benchmark) |
| U (unit test) | `west twister -T tests -p native_sim/native/64` |

Sau mỗi lần build: `F1` → **Wokwi: Start Simulator**. Lệnh `fg ...` gõ trong cửa sổ Serial Monitor của Wokwi.

### 3.2 Hiệu chuẩn giá trị nền (làm một lần mỗi phiên)

Điện áp ra của cảm biến mô phỏng phụ thuộc vào thông số Wokwi, nên trước khi kiểm thử cần đưa hệ thống về trạng thái nền:

1. Chỉnh **ppm** của MQ-2 sao cho `fg status` hiển thị `gas` trong khoảng **900–1300 mV** (dưới ngưỡng cảnh báo 1500 mV).
2. Giữ **lux** của LDR = 200, **temperature** của DHT22 = 28 °C, **card** của RC522 = 0, **disconnected** = 0.
3. Nếu hệ thống đang ở ALARM, đặt **card** = 1 rồi về 0 để về NORMAL.
4. Đợi khoảng 30 s để giá trị nền thích nghi ổn định.

Ghi lại giá trị `light` và `gas` nền để so sánh ở các bước sau.

### 3.3 Ngưỡng mặc định

| Tham số | Giá trị | Ý nghĩa |
|---|---|---|
| `th_light_delta_mv` | 800 | Ánh sáng tăng so với nền để coi là có lửa |
| `th_gas_rise_mv` | 400 | MQ-2 tăng so với nền để coi là có khói |
| `th_gas_warn_mv` | 1500 | Ngưỡng cảnh báo rò gas (cần 3 mẫu liên tiếp) |
| `th_gas_danger_mv` | 2500 | Ngưỡng nguy hiểm, vào ALARM_GAS ngay |
| `th_temp_max_x10` | 570 | 57,0 °C, vào ALARM_FIRE ngay |
| `th_temp_rise_x10` | 30 | Nhiệt độ tăng 3,0 °C trong 10 s |
| `confirm_window_ms` | 4000 | Cửa sổ xác nhận đa nguồn |

Với LDR mô phỏng, tăng **lux** từ 200 lên **5000** cho độ tăng khoảng 1200 mV (vượt 800 mV).

### 3.4 Hành vi cơ cấu chấp hành theo trạng thái

| Trạng thái | MAINS LOAD (LED trắng) | FAN (LED xanh dương, PWM) | LED RGB |
|---|---|---|---|
| WARMUP | Sáng | Tắt | Xanh dương nháy 1 Hz |
| NORMAL | Sáng | Tắt | Xanh lá sáng liên tục |
| NORMAL có lỗi cảm biến | Sáng | Tắt | Vàng nháy 1 Hz |
| PRE_ALARM | Sáng | Tắt | Vàng nháy 2 Hz |
| ALARM_FIRE | **Tắt** (relay ngắt) | **Sáng 100 %** | Đỏ / xanh lá xen kẽ 2 Hz |
| ALARM_GAS | **Tắt** (relay ngắt) | **Sáng 60–100 %** theo nồng độ gas | Đỏ / xanh lá xen kẽ 2 Hz |
| ALARM, có người trong vùng | Tắt | Sáng | Đỏ / xanh lá xen kẽ 5 Hz |
| DRILL | Sáng | Sáng 50 % | Tím nháy 1 Hz |

Buzzer thụ động được điều khiển bằng PWM khoảng 2,7 kHz, độ rộng xung 50 %. Mẫu âm theo trạng thái: ALARM_FIRE liên tục, ALARM_GAS ngắt quãng 1 Hz, DRILL tiếng ngắn mỗi giây, PRE_ALARM tiếng ngắn mỗi 5 s, NORMAL có lỗi cảm biến tiếng ngắn mỗi 30 s.

## 4. Tiêu chí

| Kết quả | Khi nào ghi |
|---|---|
| **Đạt** | Mọi log, hành vi và tiêu chí trong kịch bản đều khớp |
| **Không đạt** | Ít nhất một điểm không khớp. Bắt buộc tạo issue |
| **Bị chặn** | Không chạy được vì lỗi khác hoặc thiếu điều kiện đầu. Ghi số issue hoặc lý do |
| **Chưa chạy** | Chưa thực hiện |

Thời gian và giá trị trong log có thể lệch nhỏ (vài trăm ms, vài chục mV). Thứ tự các dòng log và trạng thái cuối cùng phải đúng.

Đợt kiểm thử kết thúc khi:

- mọi kịch bản bắt buộc ở mục 6 ở trạng thái **Đạt**;
- không còn issue mức **Nghiêm trọng** hoặc **Cao** đang mở;
- kịch bản liên quan tới mỗi bản sửa đã được chạy lại trên commit chứa bản sửa đó.

## 5. Quy trình

1. Nhận kịch bản trong sheet *Kế hoạch* của file kế hoạch dự án.
2. Cập nhật `main`, build theo mục 3.1, ghi commit đang dùng (`git log --oneline -1`).
3. Chạy kịch bản. Ghi vào sheet *Kết quả kiểm thử*: người chạy, ngày, commit, môi trường, kết quả.
4. Kịch bản **Không đạt**: tạo issue bằng mẫu *Test failure* trên GitHub, dán log và ảnh chụp, ghi số issue vào sheet.
5. Người phát hiện thử sửa trên nhánh `<tên>/fix-<mô-tả>` và mở pull request có dòng `Fixes #<số issue>`. Nếu bế tắc, gắn chủ repository vào issue.
6. Sau khi bản sửa được merge, chạy lại kịch bản và cập nhật kết quả.

| Mức độ | Ví dụ |
|---|---|
| Nghiêm trọng | Không báo động khi có sự cố; báo động không tắt được bằng thẻ Chỉ huy; thiết bị treo hoặc khởi động lại liên tục |
| Cao | Phân loại sai cháy và rò gas; báo động giả trong kịch bản nhóm C; cấu hình sai được chấp nhận |
| Trung bình | Telemetry, sự kiện hoặc web hiển thị sai nhưng thiết bị phản ứng đúng |
| Thấp | Giao diện, chính tả, log thừa |

## 6. Ma trận kịch bản

| ID | Kịch bản | Yêu cầu | Môi trường | Bắt buộc |
|---|---|---|---|---|
| A1 | Khởi động và làm nóng | FR-01, NFR-05 | Wokwi | Có |
| A2 | Shell chẩn đoán và tải CPU | NFR-05 | Wokwi | Có |
| B1 | Cháy bùng: ánh sáng + khói | FR-02, FR-03, NFR-01 | Wokwi | Có |
| B2 | Cháy bùng: ánh sáng + nhiệt tăng nhanh | FR-02, NFR-01 | Wokwi | Có |
| B3 | Nhiệt độ vượt ngưỡng tuyệt đối | FR-02, NFR-01 | Wokwi | Có |
| B4 | Rò gas mức cảnh báo | FR-02, NFR-01 | Wokwi | Có |
| B5 | Rò gas mức nguy hiểm | FR-02, NFR-01 | Wokwi | Có |
| B6 | Rò gas chuyển thành cháy | FR-02 | Wokwi | Có |
| C1 | Báo giả: chớp sáng ngắn | FR-02, NFR-04 | Wokwi | Có |
| C2 | Báo giả: bật đèn và để nguyên | FR-02, NFR-04 | Wokwi | Có |
| C3 | Báo giả: chỉ nhiệt tăng | FR-02, NFR-04 | Wokwi | Có |
| D1 | Người mắc kẹt trong vùng nguy hiểm | FR-04 | Wokwi | Có |
| D2 | Tắt báo động bằng thẻ Chỉ huy | FR-05 | Wokwi | Có |
| D3 | Báo động lại khi điều kiện còn tồn tại | FR-05 | Wokwi | Có |
| D4 | Thẻ không hợp lệ | FR-05 | Wokwi | Có |
| D5 | Thời gian phản ứng | NFR-02 | Wokwi | Có |
| E1 | Treo luồng cảm biến, tự phục hồi | FR-06, NFR-03 | Wokwi | Có |
| E2 | Hết ngân sách khởi động lại, chế độ suy giảm | FR-06, NFR-03 | Wokwi | Có |
| E3 | Đầu đọc RFID mất kết nối | FR-06 | Wokwi | Có |
| E4 | Treo luồng lõi an toàn | FR-06, NFR-03 | Wokwi | Có |
| F1 | Cấu hình hợp lệ và ngoài miền | FR-08 | Wokwi | Có |
| F2 | Diễn tập | FR-08 | Wokwi | Có |
| F3 | Nhật ký sự kiện | FR-07 | Wokwi | Có |
| F4 | Cấu hình và nhật ký giữ qua khởi động lại | FR-07, FR-08 | Wokwi | Có |
| G1 | Kết nối CoreIoT | FR-07 | Wokwi + CoreIoT | Có |
| G2 | Telemetry thích nghi | FR-07 | Wokwi + CoreIoT | Có |
| G3 | Đồng bộ shared attributes | FR-08 | Wokwi + CoreIoT | Có |
| G4 | RPC diễn tập từ xa | FR-08 | Wokwi + CoreIoT | Có |
| P1 | Tốc độ quạt theo trạng thái | FR-03 | Wokwi | Có |
| P2 | Âm báo của còi | FR-03 | Wokwi | Có |
| U1 | Unit test fusion và storage | NFR-05 | Linux/WSL | Có |
| H1 | Thống kê thời gian đáp ứng | NFR-01, NFR-02 | Wokwi | Có |
| H2 | Phân tích RTA | NFR-01, NFR-02 | Wokwi | Có |
| H3 | Tracing và Perfetto | NFR-02 | Wokwi | Không |
| H4 | Benchmark dưới tải CPU | NFR-01, NFR-02 | Wokwi | Có |
| H5 | Benchmark dưới tải mạng | NFR-02 | Wokwi + CoreIoT | Có |
| I1 | CI trên GitHub Actions | NFR-05 | GitHub | Có |
| J1 | Web: đăng nhập bằng mật khẩu | FR-07 | Web + CoreIoT | Có |
| J1b | Web: đăng nhập bằng access token | FR-07 | Web + CoreIoT | Có |
| J2 | Web: Overview thời gian thực | FR-07 | Web + CoreIoT | Có |
| J3 | Web: lỗi cảm biến và người mắc kẹt | FR-04, FR-06, FR-07 | Web + CoreIoT | Có |
| J4 | Web: History và nhật ký sự kiện | FR-07 | Web + CoreIoT | Có |
| J5 | Web: chỉnh cấu hình | FR-08 | Web + CoreIoT | Có |
| J6 | Web: diễn tập | FR-08 | Web + CoreIoT | Có |
| J7 | Web: giao diện sáng/tối và màn hình hẹp | NFR-05 | Web + CoreIoT | Không |
| J8 | Web: hết phiên và đăng xuất | FR-08 | Web + CoreIoT | Có |
| J9 | Web: trang hướng dẫn | NFR-05 | Web + CoreIoT | Không |
| J10 | Web: chế độ khách | FR-08 | Web + CoreIoT | Có |
| W1 | Lắp ráp và kiểm tra từng linh kiện | FR-01, FR-03 | Phần cứng | Có |
| W2 | Lặp lại nhóm B–F với tác nhân thật | FR-02 tới FR-06 | Phần cứng | Có |
| W3 | Thực nghiệm độ chính xác và thời gian phát hiện | NFR-01, NFR-04 | Phần cứng | Có |
| W4 | Đo thời gian thực chính thức | NFR-01, NFR-02 | Phần cứng | Có |
| W5 | Vận hành liên tục 24 giờ | NFR-03 | Phần cứng | Có |
| W6 | Mất kết nối mạng | NFR-03, FR-07 | Phần cứng + CoreIoT | Có |

Kịch bản không bắt buộc vẫn nên chạy; kết quả của chúng không chặn việc kết thúc đợt kiểm thử.

## 7. Kịch bản chi tiết

Trong các bảng dưới, dòng log được viết không kèm dấu thời gian; thứ tự xuất hiện là quan trọng. Khi một bước có đánh dấu "trong ≤ x s", thời gian tính từ lúc thay đổi giá trị trên Wokwi.

### A. Khởi động và nền tảng

#### A1. Khởi động và làm nóng

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Bản build A–F, giá trị cảm biến theo mục 3.2 |
| Bước | 1. Start Simulator. 2. Quan sát log và LED trong 10 s đầu. |
| Log kỳ vọng | `*** Booting Zephyr OS build v4.4.0 ***`<br>`I: FireGuard-RT v0.1.0, reset cause ...`<br>`I: 8 threads started`<br>`I: INIT -> WARMUP (boot)`<br>`I: MFRC522 ready, VersionReg=0x92`<br>khoảng 5 s sau: `I: WARMUP -> NORMAL (warmup_done)` |
| Hành vi kỳ vọng | LED RGB xanh dương nháy trong WARMUP, sau đó xanh lá. MAINS LOAD sáng, FAN tắt. Không có dòng `FAULT`. |
| Ghi chú | Nếu MQ-2 vượt 2500 mV, hệ thống vào `ALARM_GAS (gas_danger)` thay vì NORMAL; khi đó hiệu chuẩn lại theo mục 3.2. |

#### A2. Shell chẩn đoán và tải CPU

| Mục | Nội dung |
|---|---|
| Bước | 1. `fg status`. 2. `fg config`. 3. `fg threads`. |
| Kết quả kỳ vọng | `fg status`: `state : NORMAL`, giá trị `light`, `gas`, `temp/hum` hợp lý, mọi cảm biến `ok`.<br>`fg config`: 9 tham số đúng bảng 3.3, `sample_period_ms 1000`, `callin_period_s 60`.<br>`fg threads`: đủ 8 thread `fg_*` và `idle`; `idle` ≥ 90 % CPU. |

### B. Phát hiện và phân loại sự cố

#### B1. Cháy bùng: ánh sáng + khói

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | NORMAL |
| Bước | Trong vòng 4 s: tăng **lux** lên 5000 và tăng **ppm** MQ-2 sao cho `gas` tăng thêm ≥ 400 mV so với nền (vẫn dưới 2500 mV). |
| Log kỳ vọng | Có thể có `I: NORMAL -> PRE_ALARM (light_surge)` hoặc `(gas_rise)`, sau đó<br>`I: ... -> ALARM_FIRE (fire_confirmed)` trong **≤ 5 s** |
| Hành vi kỳ vọng | MAINS LOAD tắt, FAN sáng, LED RGB đỏ/xanh lá xen kẽ. |
| Khôi phục | Trả lux = 200, ppm về nền, đặt card = 1 rồi 0. |

#### B2. Cháy bùng: ánh sáng + nhiệt tăng nhanh

| Mục | Nội dung |
|---|---|
| Bước | Trong vòng 4 s: tăng **lux** lên 5000 và tăng **temperature** DHT22 từ 28 lên 35 °C. |
| Log kỳ vọng | `I: NORMAL -> PRE_ALARM (light_surge)`<br>`I: PRE_ALARM -> ALARM_FIRE (fire_confirmed)` trong **≤ 5 s** |
| Ghi chú | DHT22 được đọc mỗi 2 s nên xác nhận bằng nhiệt có thể chậm hơn B1 khoảng 2 s. |

#### B3. Nhiệt độ vượt ngưỡng tuyệt đối

| Mục | Nội dung |
|---|---|
| Bước | Chỉ tăng **temperature** lên 60 °C. |
| Log kỳ vọng | `I: NORMAL -> ALARM_FIRE (heat_absolute)` trong **≤ 4 s** (yêu cầu TCVN với đầu báo nhiệt: ≤ 120 s) |
| Ghi chú | Nếu bước nhảy nhiệt lớn, có thể thấy `PRE_ALARM (heat_rise)` trước. |

#### B4. Rò gas mức cảnh báo

| Mục | Nội dung |
|---|---|
| Bước | Chỉ tăng **ppm** sao cho `gas` nằm trong **1500–2500 mV**, giữ nguyên lux. |
| Log kỳ vọng | `I: NORMAL -> PRE_ALARM (gas_rise)`<br>`I: PRE_ALARM -> ALARM_GAS (gas_confirmed)` sau 3 mẫu, trong **≤ 5 s** (TCVN đầu báo khói: ≤ 30 s) |

#### B5. Rò gas mức nguy hiểm

| Mục | Nội dung |
|---|---|
| Bước | Tăng **ppm** sao cho `gas` > 2500 mV. |
| Log kỳ vọng | `I: NORMAL -> ALARM_GAS (gas_danger)` trong **≤ 2 s** |

#### B6. Rò gas chuyển thành cháy

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Đang ALARM_GAS (sau B4 hoặc B5) |
| Bước | Tăng **lux** lên 5000 và tăng thêm **ppm** (gas tăng ≥ 400 mV) trong vòng 4 s. |
| Log kỳ vọng | `I: ALARM_GAS -> ALARM_FIRE (fire_confirmed)` |

### C. Chống báo động giả

#### C1. Chớp sáng ngắn

| Mục | Nội dung |
|---|---|
| Bước | Tăng **lux** lên 5000 rồi trả về 200 trong khoảng 1–2 s. |
| Log kỳ vọng | `I: NORMAL -> PRE_ALARM (light_surge)`<br>khoảng 4 s sau: `I: PRE_ALARM -> NORMAL (not_confirmed)` |
| Tiêu chí đạt | Không xuất hiện `ALARM_FIRE` hoặc `ALARM_GAS`. |

#### C2. Bật đèn và để nguyên

| Mục | Nội dung |
|---|---|
| Bước | Tăng **lux** lên 5000 và giữ nguyên trong 60 s. |
| Log kỳ vọng | `I: NORMAL -> PRE_ALARM (light_surge)`<br>trong khoảng 15–40 s: `I: PRE_ALARM -> NORMAL (not_confirmed)` (giá trị nền đã thích nghi với mức sáng mới) |
| Tiêu chí đạt | Không có ALARM; cuối kịch bản ở NORMAL. |

#### C3. Chỉ nhiệt tăng

| Mục | Nội dung |
|---|---|
| Bước | Tăng **temperature** từ 28 lên 35 °C, giữ nguyên lux và ppm. |
| Log kỳ vọng | `I: NORMAL -> PRE_ALARM (heat_rise)`<br>khi nhiệt ổn định: `I: PRE_ALARM -> NORMAL (not_confirmed)` |
| Tiêu chí đạt | Không có ALARM (35 °C dưới ngưỡng tuyệt đối 57 °C). |

### D. Ứng phó, cứu hộ và xác thực

#### D1. Người mắc kẹt trong vùng nguy hiểm

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Đang ALARM (ví dụ sau B5) |
| Bước | Kéo **distance** của HC-SR04 qua lại liên tục 100 ↔ 300 cm, ít nhất 3 lần trong 3 s. Sau đó dừng hẳn 10 s. |
| Log kỳ vọng | `W: person in danger zone: YES`<br>sau khi dừng khoảng 10 s: `W: person in danger zone: no` |
| Hành vi kỳ vọng | LED RGB chuyển nháy nhanh (5 Hz) khi có người, trở lại 2 Hz khi hết. `fg status` hiển thị `[PERSON IN ZONE]`. |
| Ghi chú | Một dòng `E: Echo signal was not received` lúc mới vào ALARM là bình thường. |

#### D2. Tắt báo động bằng thẻ Chỉ huy

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Đang ALARM; trả các cảm biến về nền trước khi quẹt thẻ |
| Bước | Đặt **card** = 1, sau 1 s đặt lại 0. |
| Log kỳ vọng | `I: card DEADBEEF`<br>`I: ALARM_xxx -> NORMAL (commander_override)` |
| Hành vi kỳ vọng | MAINS LOAD sáng lại, FAN tắt, LED RGB xanh lá. Hệ thống ở NORMAL. |

#### D3. Báo động lại khi điều kiện còn tồn tại

| Mục | Nội dung |
|---|---|
| Bước | Gây B5 (gas > 2500 mV), **giữ nguyên** ppm, đặt card = 1 rồi 0. |
| Log kỳ vọng | `I: ALARM_GAS -> NORMAL (commander_override)`<br>trong **≤ 2 s**: `I: NORMAL -> ALARM_GAS (gas_danger)` |

#### D4. Thẻ không hợp lệ

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Đang ALARM |
| Bước | Đặt **card** = 2, sau 1 s đặt lại 0. |
| Log kỳ vọng | `I: card 12345678`<br>`W: unauthorized card rejected` |
| Tiêu chí đạt | Trạng thái giữ nguyên ALARM. `fg log` có sự kiện `card a=0`. |

#### D5. Thời gian phản ứng

| Mục | Nội dung |
|---|---|
| Bước | Sau khi đã thực hiện ít nhất 3 lần vào ALARM (B1–B6), gõ `fg latency`. |
| Kết quả kỳ vọng | `reaction latency (decision -> outputs): n=… min=… max=… avg=…` với `n` bằng số lần chuyển từ trạng thái không báo động vào ALARM và **`max` < 1 000 000 us** (NFR-02). Giá trị điển hình dưới 1 000 us. |

### E. Giám sát lỗi và chịu lỗi

#### E1. Treo luồng cảm biến, tự phục hồi

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | NORMAL, không có lỗi |
| Bước | `fg hang dht` |
| Log kỳ vọng | `W: [inject] fg_dht hangs now`<br>trong **≤ 1 s**: `E: task watchdog: fg_dht missed its deadline`<br>`W: sensor dht22 HANG FAULT`<br>`W: restarting fg_dht (1/3)`<br>khoảng 2 s sau: `W: sensor dht22 HANG recovered` |
| Hành vi kỳ vọng | LED RGB vàng nháy trong lúc có lỗi, sau đó xanh lá. Hệ thống không khởi động lại. |
| Lặp lại | Thực hiện tương tự với `fg hang adc` (lỗi báo cho cả `light` và `gas`), `fg hang ultra`, `fg hang rfid`. |

#### E2. Hết ngân sách khởi động lại, chế độ suy giảm

| Mục | Nội dung |
|---|---|
| Bước | 1. Gõ `fg hang dht` 4 lần, mỗi lần cách nhau khoảng 3 s (cả 4 lần trong vòng 60 s). 2. Thực hiện lại B1. |
| Log kỳ vọng | Lần 1–3: `restarting fg_dht (n/3)`.<br>Lần 4: `E: fg_dht: restart budget exhausted, sensor stays isolated` và không có `recovered`. |
| Hành vi kỳ vọng | `fg status` báo `sensor dht22 : FAULT`. LED vàng nháy ở NORMAL. Bước 2 vẫn cho `ALARM_FIRE (fire_confirmed)` nhờ ánh sáng + khói. |
| Khôi phục | Khởi động lại mô phỏng. |

#### E3. Đầu đọc RFID mất kết nối

| Mục | Nội dung |
|---|---|
| Bước | Đặt **disconnected** của RC522 = 1, đợi 10 s, đặt lại 0. |
| Log kỳ vọng | trong **≤ 5 s**: `W: sensor rfid IMPLAUSIBLE FAULT`<br>sau khi đặt lại 0, trong ≤ 5 s: `I: MFRC522 ready, VersionReg=0x92` và `W: sensor rfid IMPLAUSIBLE recovered` |

#### E4. Treo luồng lõi an toàn

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Đang ALARM_GAS (giữ gas > 2500 mV) |
| Bước | `fg hang fusion` |
| Log kỳ vọng | `W: [inject] fg_fusion hangs now`<br>trong **≤ 2 s** hệ thống khởi động lại: log ROM `rst:0xc (SW_CPU_RESET)` rồi `*** Booting Zephyr OS ...`<br>`W: restoring ALARM_GAS after reset` |
| Hành vi kỳ vọng | Sau khởi động lại, relay ngắt và FAN sáng ngay, không cần chờ làm nóng. |
| Lặp lại | `fg hang act` cho kết quả tương tự. |
| Ghi chú | Dòng `restoring` phụ thuộc việc Wokwi giữ RAM khi reset mềm; nếu không có, ghi nhận và kiểm lại trên phần cứng thật. |

### F. Cấu hình, diễn tập và nhật ký

#### F1. Cấu hình hợp lệ và ngoài miền

| Mục | Nội dung |
|---|---|
| Bước | 1. `fg set sample_period_ms 2000`. 2. `fg config`. 3. `fg set sample_period_ms 3000`. 4. `fg set callin_period_s 5`. 5. `fg set unknown_key 1`. 6. `fg set sample_period_ms 1000`. |
| Kết quả kỳ vọng | 1: `I: config sample_period_ms=2000`.<br>2: `sample_period_ms  2000`.<br>3, 4: `W: config ... rejected (...)` và `value rejected (out of range)`; giá trị không đổi.<br>5: `unknown key`.<br>6: trở về 1000. |

#### F2. Diễn tập

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | NORMAL |
| Bước | 1. `fg drill 10`, đợi 12 s. 2. `fg drill 60`, sau 5 s gây B5. |
| Log kỳ vọng | 1: `I: NORMAL -> DRILL (drill_start)`, khoảng 10 s sau `I: DRILL -> NORMAL (drill_end)`.<br>2: `I: DRILL -> ALARM_GAS (gas_danger)` |
| Hành vi kỳ vọng | Trong DRILL: LED tím nháy, FAN sáng, MAINS LOAD vẫn sáng (không cắt điện). Sự cố thật được ưu tiên hơn diễn tập. |

#### F3. Nhật ký sự kiện

| Mục | Nội dung |
|---|---|
| Bước | Sau các kịch bản trên, gõ `fg log`. |
| Kết quả kỳ vọng | Danh sách sự kiện có số thứ tự tăng dần, gồm `boot`, `state` (mỗi lần chuyển trạng thái), `fault`, `card`, `trapped`, khớp với các thao tác đã làm. Tối đa 64 sự kiện gần nhất. |

#### F4. Cấu hình và nhật ký giữ qua khởi động lại

| Mục | Nội dung |
|---|---|
| Bước | 1. `fg set callin_period_s 120`. 2. `fg hang fusion` để hệ thống tự khởi động lại. 3. `fg config` và `fg log`. 4. `fg config reset`. |
| Kết quả kỳ vọng | Khi khởi động lại: `I: storage ready, boot #N, M events restored` với N tăng thêm 1.<br>3: `callin_period_s   120`; `fg log` còn các sự kiện của lần khởi động trước (cột `boot` nhỏ hơn) và sự kiện `boot` mới.<br>4: `I: config reset to defaults`, `fg config` trở về `callin_period_s   60`. |
| Ghi chú | Flash mô phỏng chỉ được giữ khi chip tự khởi động lại; dừng và bật lại Wokwi sẽ xoá flash. |

### G. Kết nối CoreIoT

Điều kiện chung: bản build có CoreIoT, `secrets.conf` chứa access token của device trên CoreIoT, mô phỏng có Internet qua mạng `Wokwi-GUEST`.

#### G1. Kết nối

| Mục | Nội dung |
|---|---|
| Log kỳ vọng | `I: Wi-Fi: connecting to "Wokwi-GUEST"`<br>`I: MQTT connected to CoreIoT` trong ≤ 60 s<br>`I: rx v1/devices/me/attributes/response/1 {"shared":{...}}`<br>các dòng `I: config <key>=<value>` khớp shared attributes |
| Kết quả trên CoreIoT | Device chuyển **Active**. Tab *Latest telemetry* có `state`, `light`, `gas`, `temp`, `hum`, `faults`, `trapped`. Tab *Client attributes* có `fw_version = 0.1.0`. |
| Ghi chú | Nếu chỉ thấy `Wi-Fi: no IP yet, retry ...`, kiểm tra cấu hình mạng của Wokwi (IoT Gateway). Phần phát hiện và ứng phó cục bộ vẫn phải hoạt động bình thường (NFR-03). |

#### G2. Telemetry thích nghi

| Mục | Nội dung |
|---|---|
| Bước | 1. Ở NORMAL quan sát 2 phút. 2. Gây B5. 3. Quẹt thẻ về NORMAL. |
| Kết quả kỳ vọng | 1: telemetry mới mỗi khoảng **60 s**.<br>2: có ngay `event = state`, `detail = ALARM_GAS`; telemetry mỗi khoảng **1 s**.<br>3: sự kiện `card`, `state = NORMAL`; telemetry trở lại 60 s. |

#### G3. Đồng bộ shared attributes

| Mục | Nội dung |
|---|---|
| Bước | Trên CoreIoT, sửa shared attribute `callin_period_s` = 30, sau đó = 5. |
| Kết quả kỳ vọng | 30: log `I: rx v1/devices/me/attributes {"callin_period_s":30}`, `I: config callin_period_s=30`; client attribute `cfg_callin_period_s = applied`; telemetry ở NORMAL mỗi 30 s.<br>5: client attribute `cfg_callin_period_s = rejected`, chu kỳ giữ 30 s. |

#### G4. RPC diễn tập từ xa

| Mục | Nội dung |
|---|---|
| Bước | Gửi RPC hai chiều tới device: method `startDrill`, params `{"duration_s": 15}`; sau đó `stopDrill`. |
| Kết quả kỳ vọng | Phản hồi `{"result":"ok"}`. Log `I: NORMAL -> DRILL (drill_start)`, sau `stopDrill`: `I: DRILL -> NORMAL (drill_end)`. |
| Tiêu chí bổ sung | Không có lệnh từ xa nào tắt được ALARM (FR-08). |

### P. Cơ cấu chấp hành PWM

#### P1. Tốc độ quạt theo trạng thái

| Mục | Nội dung |
|---|---|
| Bước | 1. Gây B1 (ALARM_FIRE), quan sát FAN, quẹt thẻ về NORMAL. 2. Chỉnh MQ-2 để `gas` khoảng 1700 mV rồi chờ ALARM_GAS. 3. Tăng `gas` lên trên 2500 mV. 4. Quẹt thẻ, `fg drill 10`. |
| Kết quả kỳ vọng | 1: FAN sáng tối đa (100 %).<br>2: FAN sáng mờ hơn rõ rệt (khoảng 68 %).<br>3: FAN sáng tối đa.<br>4: FAN sáng khoảng 50 %, MAINS LOAD vẫn sáng.<br>Ở NORMAL, PRE_ALARM, WARMUP: FAN tắt. |

#### P2. Âm báo của còi

| Mục | Nội dung |
|---|---|
| Bước | Lặp lại các trạng thái ở P1, thêm E3 (lỗi RFID ở NORMAL) và D4 (thẻ không hợp lệ). |
| Kết quả kỳ vọng | Mẫu âm đúng bảng ở mục 3.4. Thẻ hợp lệ: một tiếng ngắn; thẻ không hợp lệ: hai tiếng ngắn. |
| Ghi chú | Nếu Wokwi không phát âm thanh trong VS Code, đánh dấu "Không đánh giá được" và kiểm tra lại trên phần cứng thật. |

### U. Unit test

#### U1. Unit test fusion và storage

| Mục | Nội dung |
|---|---|
| Bước | Chạy lệnh build U ở mục 3.1 trên Linux hoặc WSL. |
| Kết quả kỳ vọng | `tests/fusion`: 12/12 đạt; `tests/storage`: 5/5 đạt; Twister báo `2 of 2 executed test configurations passed`, 0 lỗi, 0 cảnh báo build. |

### H. Phân tích thời gian thực

Điều kiện chung: bản build A–F, hệ thống ở NORMAL sau hiệu chuẩn 3.2.

#### H1. Thống kê thời gian đáp ứng

| Mục | Nội dung |
|---|---|
| Bước | 1. `fg rt reset`. 2. Đợi 60 s, gây B5 rồi quẹt thẻ về NORMAL. 3. `fg rt`. |
| Kết quả kỳ vọng | 1: `statistics cleared`.<br>3: bảng các cột `thread prio jobs C_avg_us ... `; mỗi luồng lõi (act, fault, fusion, adc, dht, ultra, rfid) có `jobs` > 0; R_max của mỗi luồng nhỏ hơn chu kỳ của nó (act 50 ms, fusion 100 ms, fault 250 ms, cảm biến 200–250 ms).<br>Dòng cuối `blocking (flash write) max: N us`, N > 0 do sự kiện được ghi flash. |

#### H2. Phân tích RTA

| Mục | Nội dung |
|---|---|
| Bước | Sau H1, `fg rta`. |
| Kết quả kỳ vọng | Dòng đầu `Fixed-priority RTA, C = 120% of measured max, B = ... us`.<br>Mỗi luồng có cột `result` = `ok` (chấp nhận `ok (R_meas > bound)` nếu có ghi chú); không có `MISS`.<br>Dòng cuối `utilisation U = x.xx %, all deadlines met`, U < 100 %. |

#### H3. Tracing và Perfetto

| Mục | Nội dung |
|---|---|
| Bước | 1. `fg trace start`, gây B5 trong vòng 2 s. 2. `fg trace stop`. 3. `fg trace dump`, lưu các dòng từ `TR,us,event,name` đến `TR,end,N` vào tệp `trace.csv`. 4. `python scripts/trace2perfetto.py trace.csv trace.json`, mở `trace.json` tại ui.perfetto.dev. |
| Kết quả kỳ vọng | 1: `capturing up to 2048 events`.<br>2: `N events captured, longest ISR X us`, N > 0.<br>4: Perfetto hiển thị một hàng cho mỗi luồng; quanh thời điểm gây sự cố thấy fg_adc → fg_fusion → fg_act chạy liên tiếp, fg_act bắt đầu trong vòng vài ms sau fg_fusion. |

#### H4. Benchmark dưới tải CPU

| Mục | Nội dung |
|---|---|
| Bước | 1. `fg bench 5`. 2. `fg load cpu 80`, `fg bench 5`. 3. `fg load cpu 0`. |
| Kết quả kỳ vọng | 1: `[bench] 5 alarm cycles`, mỗi chu kỳ `[bench] cycle i: detection N ms`; kết thúc `[bench] done: 5/5 cycles`, dòng `detection ms: ... max` ≤ 5000 và `reaction us: ... max` < 1000000.<br>2: `cpu load 80 % at priority 12`; kết quả benchmark vẫn đạt 5/5, max detection và max reaction cùng ngưỡng như bước 1 (chênh lệch so với bước 1 chỉ vài ms).<br>Không có `no alarm within 10 s` hoặc `did not return to NORMAL`. |

#### H5. Benchmark dưới tải mạng

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Bản build G, đã có `MQTT connected to CoreIoT`. |
| Bước | 1. `fg load net 20`. 2. `fg bench 5`. 3. `fg load net 0`. |
| Kết quả kỳ vọng | 1: `extra MQTT publications: 20 Hz`; tab Latest telemetry trên CoreIoT cập nhật liên tục.<br>2: `done: 5/5 cycles`, max detection ≤ 5000 ms, max reaction < 1000000 us.<br>Trên bản build không có `comm.conf`: `fg load net 20` báo `requires a build with comm.conf`. |

### I. Tích hợp liên tục

#### I1. CI trên GitHub Actions

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Đã push nhánh lên GitHub. |
| Bước | Mở tab Actions, chọn lượt chạy workflow `build` của nhánh vừa push. |
| Kết quả kỳ vọng | Tất cả bước xanh: build `sim`, `hw`, `comm` và Twister. Mục Artifacts có các tệp firmware (`zephyr.elf`, `zephyr.bin`) và kết quả Twister. Thời gian chạy dưới 30 phút. |
| Bổ sung | Tạo một lỗi biên dịch có chủ đích trên nhánh thử và push: workflow phải đỏ ở bước build. Xoá nhánh thử sau khi kiểm tra. |

### J. Giao diện giám sát web

Điều kiện chung: bản build G đang chạy và kết nối CoreIoT; mở `web/index.html` trong Chrome (hoặc `python -m http.server 8080 --directory web` rồi mở `http://localhost:8080`).

#### J1. Đăng nhập bằng mật khẩu

| Mục | Nội dung |
|---|---|
| Bước | 1. Bấm Sign in khi để trống. 2. Nhập sai mật khẩu. 3. Đổi server thành `https://khong-ton-tai.example`. 4. Nhập đúng tài khoản CoreIoT. |
| Kết quả kỳ vọng | 1: báo `Fill in the server, email and password.`, ô trống viền đỏ.<br>2: `Email or password is incorrect.`<br>3: `Cannot reach ... Check the server address and your connection.`<br>4: vào trang Overview, ô chọn device hiển thị `FireGuard-RT-01`. |

#### J1b. Đăng nhập bằng access token

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Đã đăng nhập CoreIoT bằng Google hoặc GitHub trong một tab khác. |
| Bước | 1. Chọn **Access token**, bấm Sign in khi để trống. 2. Dán chuỗi `abc`. 3. Bấm liên kết *Account, Security*. 4. Bấm **Copy JWT token** trên CoreIoT, dán vào trang (có hoặc không có tiền tố `Bearer`), bấm Sign in. 5. Đăng xuất, tải lại trang. 6. Đăng nhập lại bằng token và dán thêm `refresh_token` (DevTools > Application > Local storage của tab CoreIoT) vào mục *Stay signed in longer*. |
| Kết quả kỳ vọng | 1: `Fill in the server and paste a JWT token.`<br>2: `This does not look like a JWT token.`<br>3: mở trang Security của CoreIoT ở tab mới.<br>4: vào Overview như J1.<br>5: màn hình đăng nhập vẫn ở chế độ **Access token**.<br>6: đăng nhập thành công. |
| Bổ sung | Không dán refresh token: 5 phút trước khi token hết hạn có thông báo đỏ `Your token expires in a few minutes...`; khi hết hạn quay về màn hình đăng nhập với `Your session has expired. Sign in again.`. Có refresh token: trang tiếp tục hoạt động sau thời điểm hết hạn của JWT ban đầu. Dán token đã hết hạn: `This token has expired. Copy a new one from CoreIoT.` |

#### J2. Overview thời gian thực

| Mục | Nội dung |
|---|---|
| Bước | 1. Ở NORMAL quan sát trang. 2. Gây B5 trên Wokwi. 3. Quẹt thẻ về NORMAL. |
| Kết quả kỳ vọng | 1: vòng trạng thái màu xanh lá `Normal`; bốn ô cảm biến khớp `fg status`; Response: Mains power `On`, Smoke fan `Off`, Siren `Off`; chấm cạnh tên device màu xanh; Last report tăng dần mỗi giây.<br>2: không cần tải lại trang, trong vòng khoảng 2 s vòng chuyển đỏ `Gas alarm`, nguyên nhân `Gas above danger level`, nền trang ngả đỏ nhạt, vòng nhấp nháy; Mains power `Cut`, Smoke fan 60–100 %, Siren `Pulsed 1 Hz`; ô Gas đỏ `Above danger level`; Recent events có dòng mới; tiêu đề tab bắt đầu bằng `Gas alarm`.<br>3: trở về `Normal`, có sự kiện `Authorized card` và `Normal · Card override`. |

#### J3. Lỗi cảm biến và người mắc kẹt

| Mục | Nội dung |
|---|---|
| Bước | 1. Gây E3 (RFID mất kết nối). 2. Khôi phục. 3. Trong ALARM, gây D1. |
| Kết quả kỳ vọng | 1: Sensor health: `Card reader` chấm vàng, `Fault`; Status light `Yellow blink`; Siren `Fault chirp`; sự kiện `Card reader: implausible reading`.<br>2: trở lại `OK`, sự kiện `Card reader recovered`.<br>3: hiện hộp đỏ `Person detected in the danger zone`; Status light `Red / green fast`. |

#### J4. History và nhật ký sự kiện

| Mục | Nội dung |
|---|---|
| Bước | 1. Mở History, chọn lần lượt 1 h, 24 h, 7 d. 2. Bấm từng bộ lọc State, Faults, Cards, Presence, Boot, All. 3. Rê chuột trên biểu đồ. |
| Kết quả kỳ vọng | 1: hai biểu đồ (Gas and light, Temperature and humidity) vẽ lại theo khoảng; khoảng không có dữ liệu hiển thị `No readings in this range.`.<br>2: bảng chỉ còn đúng loại sự kiện; số `Seq` giảm dần theo thời gian; bộ lọc không có kết quả hiển thị `No events in this range.`.<br>3: tooltip hiển thị thời điểm và giá trị từng đường. |

#### J5. Chỉnh cấu hình

| Mục | Nội dung |
|---|---|
| Bước | 1. Mở Settings. 2. Nhập `Sample period` = 3000. 3. Nhập `Gas danger level` = 1400 (nhỏ hơn mức cảnh báo 1500). 4. Bấm Revert. 5. Đặt `Call-in period` = 30, bấm Save changes. |
| Kết quả kỳ vọng | 1: các ô hiển thị giá trị shared attribute hiện tại, góc phải hiển thị `Firmware 0.1.0`.<br>2: dòng gợi ý đỏ `Must be 1000 to 2000 ms`, nút Save bị khoá.<br>3: `Must be above the warning level`.<br>4: các ô trở về giá trị ban đầu.<br>5: `Saved. Waiting for the device to confirm.`, khoảng 4 s sau `Saved.`; nhãn cạnh `Call-in period` là `Applied ... ago`; log Wokwi `I: config callin_period_s=30`. Đặt lại 60 sau khi kiểm tra. |

#### J6. Diễn tập

| Mục | Nội dung |
|---|---|
| Bước | 1. Kéo Duration = 15 s, bấm Start drill. 2. Bấm Stop trước khi hết giờ. 3. Dừng mô phỏng Wokwi, bấm Start drill. |
| Kết quả kỳ vọng | 1: `Drill running for 15 s.`; Overview chuyển `Drill` màu tím, Smoke fan `50 %`, Mains power `On`.<br>2: `Drill stopped.`; trở về `Normal`.<br>3: sau tối đa 10 s báo đỏ `The device is offline.` hoặc `The device did not answer. It may be offline.`; nút trở lại dùng được. |

#### J7. Giao diện sáng/tối và màn hình hẹp

| Mục | Nội dung |
|---|---|
| Bước | 1. Bấm nút mặt trăng/mặt trời. 2. Tải lại trang. 3. Thu cửa sổ còn khoảng 390 px (DevTools, chế độ thiết bị). |
| Kết quả kỳ vọng | 1: toàn bộ trang và biểu đồ đổi bảng màu, chữ vẫn đọc rõ.<br>2: giữ chủ đề đã chọn.<br>3: các khối xếp một cột, thanh trang Overview / History / Settings chiếm hết chiều ngang, không có thanh cuộn ngang; bảng sự kiện chỉ còn cột Time và Detail. |

#### J8. Hết phiên và đăng xuất

| Mục | Nội dung |
|---|---|
| Bước | 1. Bấm nút đăng xuất. 2. Đăng nhập lại, đóng tab rồi mở lại `index.html`. 3. Đăng nhập, mở DevTools > Console, chạy `CoreIoT.Api.token = 'x'; CoreIoT.Api.refreshToken = 'x'`, rồi chuyển sang trang History. |
| Kết quả kỳ vọng | 1: về màn hình đăng nhập.<br>2: yêu cầu đăng nhập lại (phiên chỉ tồn tại trong tab).<br>3: quay về màn hình đăng nhập với thông báo `Your session has expired. Sign in again.`. |

#### J9. Trang hướng dẫn

| Mục | Nội dung |
|---|---|
| Bước | 1. Ở màn hình đăng nhập, bấm *How to use this panel*. 2. Bấm từng mục trong Contents. 3. Bấm *Back to dashboard*, đăng nhập, bấm nút dấu hỏi trên thanh trên cùng. 4. Trên trang Overview, bấm *About faults*; trên Settings, bấm *What these settings mean* và *How drills work*. 5. Đổi giao diện sáng/tối trên trang hướng dẫn rồi quay lại dashboard. 6. Thu cửa sổ còn khoảng 390 px. |
| Kết quả kỳ vọng | 1, 3: mở `help.html` mà không cần đăng nhập.<br>2: trang cuộn tới đúng mục, mục đang đọc in đậm trong Contents.<br>4: mở đúng mục 5, 7, 8.<br>5: dashboard giữ chủ đề vừa chọn.<br>6: Contents nằm trên nội dung, không có thanh cuộn ngang.<br>Nội dung: bảng màu trạng thái khớp màu vòng trạng thái trên Overview; giá trị mặc định ở mục 7 khớp `fg config`. |

#### J10. Chế độ khách

| Mục | Nội dung |
|---|---|
| Điều kiện đầu | Device FireGuard-RT-01 đã được công khai trên CoreIoT. Mở trang bằng cửa sổ ẩn danh. |
| Bước | 1. Bấm **View as guest**. 2. Mở History, đổi khoảng thời gian. 3. Gõ `#/settings` vào cuối địa chỉ trang. 4. Gây B5 trên Wokwi. 5. Bấm nút thoát ở góc phải. |
| Kết quả kỳ vọng | 1: vào Overview, thanh trên cùng có nhãn `Guest view`, chỉ có Overview và History, ô chọn device chỉ có `FireGuard-RT-01`.<br>2: biểu đồ và nhật ký hiển thị như khi đăng nhập.<br>3: trang quay về Overview.<br>4: Overview chuyển `Gas alarm` không cần tải lại.<br>5: về màn hình đăng nhập. |
| Bổ sung | Khi device chưa công khai: bấm **View as guest** báo `No device is shared for guests yet.` |

### W. Phần cứng

Điều kiện chung: mạch lắp đúng [`wiring.md`](wiring.md), bản build phần cứng (hoặc `rt.conf` cho W4), mở `west espressif monitor`.

**An toàn khi tạo tác nhân thật**: làm ở nơi thoáng, có người giám sát, có bình chữa cháy hoặc xô nước gần đó. Chỉ dùng ngọn lửa nhỏ (nến, bật lửa) và giữ khoảng cách. Thử gas bằng bật lửa gas *không bật lửa*, xả từng nhịp ngắn. Không thử gas và lửa cùng lúc.

| Tác nhân | Thay cho thao tác Wokwi |
|---|---|
| Nến hoặc bật lửa cách LDR 20–30 cm, phòng tối | Tăng `lux` |
| Khói nhang trước MQ-2 | Tăng `ppm` nhẹ (khói) |
| Xả gas từ bật lửa không bật, cách MQ-2 5–10 cm | Tăng `ppm` mạnh (gas) |
| Máy sấy tóc thổi vào DHT22 | Tăng `temperature` |
| Người đi lại trước HC-SR04 | Kéo `distance` |
| Thẻ RFID có UID trong `CONFIG_FG_COMMANDER_UIDS` | `card` = 1 |

#### W1. Lắp ráp và kiểm tra từng linh kiện

| Mục | Nội dung |
|---|---|
| Bước | 1. Trước khi cắm ESP32, đo điện áp ra của cầu phân áp MQ-2 và ECHO: không vượt 3,3 V. 2. Nạp firmware, quan sát log khởi động. 3. `fg status` sau khi làm nóng. 4. `fg drill 10`. 5. Quẹt thẻ Chỉ huy và một thẻ lạ. |
| Kết quả kỳ vọng | 1: ≤ 3,3 V.<br>2: giống A1, `MFRC522 ready`.<br>3: mọi cảm biến `ok`, giá trị hợp lý với phòng thí nghiệm.<br>4: quạt quay khoảng nửa tốc độ, còi kêu ngắn mỗi giây, LED tím, relay không cắt.<br>5: một tiếng bíp và `card ...`; thẻ lạ hai tiếng bíp và `unauthorized card rejected`. |

#### W2. Lặp lại nhóm B–F với tác nhân thật

| Mục | Nội dung |
|---|---|
| Bước | Chạy lại B1–B6, C1–C3, D1–D5, E1–E4, F1–F4 với tác nhân ở bảng trên. Với E3, rút dây SDA của RC522 rồi cắm lại. |
| Kết quả kỳ vọng | Như trên Wokwi. Relay đóng cắt nghe được tiếng click, quạt quay theo duty ở mục 3.4, còi phát đúng mẫu âm. |

#### W3. Thực nghiệm độ chính xác và thời gian phát hiện

| Mục | Nội dung |
|---|---|
| Bước | 1. Mỗi loại sự cố (lửa, khói, gas, nhiệt) thực hiện ít nhất 20 lần; ghi thời điểm đưa tác nhân vào và thời điểm log báo ALARM. 2. Mỗi kịch bản nhiễu thực hiện ít nhất 20 lần: bật/tắt đèn phòng, đèn pin chiếu thoáng qua LDR, thổi hơi thở vào DHT22. |
| Kết quả kỳ vọng | 1: tỷ lệ phát hiện đúng ≥ 95 %; thời gian phát hiện lửa ≤ 5 s, khói/gas ≤ 30 s, nhiệt ≤ 120 s; phân loại đúng cháy hay rò gas.<br>2: không có ALARM nào. |

#### W4. Đo thời gian thực chính thức

| Mục | Nội dung |
|---|---|
| Bước | Build với `rt.conf` (và `rt.conf;comm.conf;secrets.conf` cho phần có tải mạng). Chạy H1–H5 theo [`diagnostics.md`](diagnostics.md) mục 5, mỗi điều kiện `fg bench 20`. |
| Kết quả kỳ vọng | `done: 20/20 cycles` ở mọi điều kiện; độ trễ phản ứng lớn nhất < 1 000 000 us cả khi có tải CPU 50 % và tải mạng 20 Hz; `fg rta` kết luận `all deadlines met`. Lưu toàn bộ output để đưa vào báo cáo. |

#### W5. Vận hành liên tục 24 giờ

| Mục | Nội dung |
|---|---|
| Bước | Để thiết bị chạy 24 giờ ở NORMAL, có kết nối CoreIoT. Ghi log serial ra file. Sau 24 giờ, `fg log` và `fg status`. |
| Kết quả kỳ vọng | Không có sự kiện `boot` mới, không có dòng `FATAL` hoặc `task watchdog`; telemetry trên CoreIoT đều đặn theo `callin_period_s`. |

#### W6. Mất kết nối mạng

| Mục | Nội dung |
|---|---|
| Bước | 1. Tắt Wi-Fi của router. 2. Gây sự cố khói hoặc gas, quẹt thẻ Chỉ huy. 3. Bật lại Wi-Fi, chờ 2 phút. |
| Kết quả kỳ vọng | 2: thiết bị vẫn báo động và tắt bằng thẻ như bình thường.<br>3: log có `MQTT connected to CoreIoT`; các sự kiện xảy ra lúc mất mạng xuất hiện trên CoreIoT và trang History theo đúng thứ tự `seq`. |
