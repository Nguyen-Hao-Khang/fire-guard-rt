# Đóng góp

Tài liệu này quy định cách nhóm đặt tên nhánh, viết commit, viết comment và mở pull request cho FireGuard-RT.

## 1. Nhánh

`main` luôn build được. Mỗi việc làm trên một nhánh ngắn hạn và vào `main` qua pull request.

Tên nhánh: `<người-phụ-trách>/<việc>`

| Phần | Quy tắc |
|---|---|
| `người-phụ-trách` | Tên định danh của thành viên chịu trách nhiệm, ví dụ `khangnh` |
| `việc` | Tính năng hoặc việc đang làm: 2 đến 5 từ tiếng Anh viết thường, nối bằng dấu gạch ngang, chỉ ký tự ASCII |

Ví dụ: `khangnh/coreiot-rpc`, `khangnh/rfid-card-debounce`, `khangnh/fusion-degraded-tests`.

## 2. Commit

Commit theo [Conventional Commits](https://www.conventionalcommits.org/), viết bằng tiếng Anh:

```
<type>(<scope>): <summary>

<body: what and why, wrapped at 72 columns>

Refs: <issue or work item>
```

- `summary`: thể mệnh lệnh, chữ thường, không dấu chấm cuối, tối đa 50 ký tự.
- `body`: tuỳ chọn, giải thích lý do thay đổi thay vì kể lại diff.
- Mỗi commit chứa một thay đổi logic. Cây mã phải build được sau mỗi commit.
- Chỉ dùng ký tự ASCII.

| `type` | Dùng khi |
|---|---|
| `feat` | Thêm tính năng |
| `fix` | Sửa lỗi |
| `test` | Thêm hoặc sửa test |
| `docs` | Chỉ đổi tài liệu |
| `build` | Đổi hệ thống build, Kconfig, CI, mô phỏng |
| `refactor` | Đổi cấu trúc mã, không đổi hành vi |

Scope thường dùng: `fusion`, `sensors`, `rfid`, `actuators`, `safety`, `storage`, `rt`, `comm`, `web`, `sim`.

Ví dụ:

```
feat(fusion): confirm flame with smoke or heat rise
fix(rfid): ignore missed REQA while card stays in field
test(fusion): cover degraded mode without LDR
build(sim): generate wokwi flash image after link
```

## 3. Comment trong mã

- Viết bằng tiếng Anh, ngắn, nêu ý định hoặc ràng buộc thay vì lặp lại mã.
- Dùng khối `/* */`. Đầu file có một dòng mô tả module, thêm chi tiết bên dưới khi cần.
- Không dùng dòng trang trí, banner hay dãy gạch ngang dài.
- Việc dự định làm: `TODO(<scope>): <action>`.

## 4. Pull request

Trước khi mở pull request, chạy:

```sh
west build -p -b esp32_devkitc/esp32/procpu -- "-DEXTRA_CONF_FILE=sim.conf"
west twister -T tests -p native_sim/native/64
```

Quy trình:

1. Cập nhật `main` và tạo nhánh mới từ đó.
2. Commit theo mục 2, push nhánh của bạn lên GitHub.
3. Mở pull request vào `main`. Mô tả nêu việc đã làm, cách kiểm thử, kịch bản kiểm thử liên quan.
4. Chờ CI xanh và ít nhất một thành viên khác review.
5. Chủ repository merge. Xoá nhánh sau khi merge.

Không commit `secrets.conf`, thư mục `build/` hay file cá nhân của IDE. `.gitignore` đã chặn các file này.
