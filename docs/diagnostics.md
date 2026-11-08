# Chẩn đoán và phân tích thời gian thực

Firmware có sẵn shell trên UART0 (115200 baud) để xem trạng thái, tiêm lỗi và đo thời gian thực. Trên Wokwi, bạn gõ lệnh vào Serial Monitor; trên board thật, dùng `west espressif monitor` hoặc bất kỳ terminal nối tiếp nào.

## 1. Lệnh shell

### 1.1 Vận hành

| Lệnh | Tác dụng |
|---|---|
| `fg status` | Trạng thái, giá trị cảm biến, cờ lỗi |
| `fg config` | Cấu hình đang dùng |
| `fg config reset` | Khôi phục cấu hình mặc định và ghi đè bản trên flash |
| `fg set <khoá> <giá trị>` | Đổi một tham số, có kiểm tra miền |
| `fg drill [giây\|stop]` | Bắt đầu diễn tập (mặc định 30 s) hoặc dừng |
| `fg card <uid>` | Giả lập quẹt thẻ, UID gồm 8 chữ số hex, ví dụ `DEADBEEF` |
| `fg log` | Nhật ký sự kiện kèm số thứ tự và số lần khởi động |
| `fg hang <adc\|dht\|ultra\|rfid\|fusion\|act>` | Làm treo một luồng để kiểm thử Task Watchdog |
| `kernel thread list` | Stack và ưu tiên của mọi luồng trong kernel |

### 1.2 Đo đạc

| Lệnh | Tác dụng | Cấu hình cần bật |
|---|---|---|
| `fg latency [reset]` | Độ trễ từ lúc fusion quyết định tới lúc relay, quạt và còi đều đã tác động | Mặc định |
| `fg rt [reset]` | Số job, C và R của từng luồng an toàn | `CONFIG_FG_RT_STATS` (mặc định) |
| `fg rta` | Phân tích thời gian đáp ứng trên số đo | `CONFIG_FG_RT_STATS` |
| `fg threads` | Mức dùng CPU, ưu tiên, trạng thái từng luồng | `CONFIG_FG_DIAG_STATS` |
| `fg trace start\|stop\|dump` | Ghi dòng thời gian lập lịch | `CONFIG_FG_TRACE` |
| `fg load cpu <%>` | Tạo tải CPU ở ưu tiên 12 | `CONFIG_FG_BENCH` |
| `fg load net <Hz>` | Gửi thêm bản tin MQTT mỗi giây | `CONFIG_FG_BENCH` và `comm.conf` |
| `fg bench <n>` | Chạy n chu kỳ báo động tự động và đo độ trễ | `CONFIG_FG_BENCH` |

`sim.conf` và `rt.conf` bật toàn bộ nhóm đo đạc.

## 2. Đo C và R

Mỗi luồng an toàn đánh dấu đầu và cuối của từng job:

- **C** (thời gian thực thi): thời gian CPU mà chính luồng đó dùng, lấy từ bộ đếm của scheduler. Thời gian luồng bị luồng khác chiếm quyền không tính vào C.
- **R** (thời gian đáp ứng): thời gian thực từ lúc job được kích hoạt tới lúc job xong. Với luồng chạy theo chu kỳ, thời điểm kích hoạt là thời điểm thức dậy đã hẹn, nên R bao gồm cả độ trễ đánh thức.
- **B** (thời gian chặn): thời gian ghi flash dài nhất. Ghi flash tạm dừng cache, chặn mọi luồng bất kể ưu tiên.

```
fg rt reset
(chờ, tạo sự cố, quẹt thẻ ...)
fg rt
```

## 3. Phân tích RTA

`fg rta` lặp phương trình RTA cho lập lịch ưu tiên cố định:

```
R_i = C_i + B + Σ_{j ∈ hp(i)} ⌈R_i / T_j⌉ · C_j
```

trong đó `hp(i)` là tập luồng ưu tiên cao hơn luồng i. Lệnh dùng C bằng 120 % giá trị lớn nhất đo được để chừa biên, lặp tới khi R hội tụ hoặc vượt hạn chót D, rồi in R tính được cạnh R đo được.

Mô hình chu kỳ T (khoảng cách nhỏ nhất giữa hai job) và hạn chót D nằm trong `src/safety/rt_stats.c`:

| Luồng | T (ms) | D (ms) | Ghi chú |
|---|---|---|---|
| `fg_act` | 50 | 50 | Sự kiện trạng thái và tick hiển thị 50 ms |
| `fg_fault` | 250 | 250 | Hàng đợi lỗi và tick 250 ms |
| `fg_fusion` | 100 | 100 | Tối đa một bản tin cảm biến mỗi 100 ms |
| `fg_adc` | 250 | 250 | Luồng cảm biến thức ít nhất mỗi 250 ms để báo watchdog |
| `fg_dht` | 250 | 250 | |
| `fg_ultra` | 200 | 200 | |
| `fg_rfid` | 200 | 200 | |

Mỗi dòng kết quả kết thúc bằng `ok`, `ok (R_meas > bound)` hoặc `MISS`. Dòng cuối in hệ số sử dụng U và kết luận `all deadlines met` hoặc `NOT met`.

## 4. Tracing

Zephyr gọi các hook tracing mỗi lần chuyển luồng và mỗi lần vào, ra ngắt. Firmware ghi các sự kiện này vào bộ đệm RAM (`CONFIG_FG_TRACE_EVENTS`, mặc định 2048) chỉ sau khi bạn bật ghi bằng shell, nên hook không tốn thời gian khi không đo.

1. `fg trace start`, tạo tình huống cần quan sát, chờ vài giây.
2. `fg trace stop`. Shell in số sự kiện đã ghi và ngắt dài nhất.
3. `fg trace dump`. Lưu toàn bộ output serial vào một file, ví dụ `capture.log`.
4. Chuyển sang định dạng Perfetto:

```sh
python scripts/trace2perfetto.py capture.log trace.json
```

5. Mở `trace.json` tại [ui.perfetto.dev](https://ui.perfetto.dev). Mỗi luồng là một hàng; ngắt nằm trên hàng `isr`.

## 5. Benchmark

`fg bench <n>` chạy n chu kỳ báo động qua đường dữ liệu thật:

1. Ghi đè số đo MQ-2 bên trong luồng ADC về mức thấp, quẹt thẻ Chỉ huy và chờ hệ thống về `NORMAL`.
2. Sau 1,5 s, ghi đè MQ-2 lên trên ngưỡng nguy hiểm và ghi thời điểm.
3. Đo độ trễ phát hiện (tới lúc fusion quyết định `ALARM_GAS`) và độ trễ phản ứng (tới lúc mọi cơ cấu chấp hành đã tác động).

Kết thúc, lệnh in giá trị nhỏ nhất, trung bình và lớn nhất cho cả hai độ trễ, kèm giới hạn yêu cầu.

Quy trình đo đầy đủ:

```
fg rt reset
fg load cpu 50
fg load net 20        (chỉ khi build có comm.conf)
fg bench 20
fg rt
fg rta
fg load cpu 0
fg load net 0
```

So sánh kết quả có tải và không tải để kiểm chứng NFR-02. Wokwi không mô phỏng đúng thời gian thực thi của ESP32, nên số liệu chính thức cần đo trên board thật với `rt.conf`.
