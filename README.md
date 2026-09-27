# TirePressureScreen

Firmware hiển thị áp suất bốn bánh trên **Waveshare ESP32-S3-Touch-LCD-7**, màn hình **800 × 480**, viết bằng C với **ESP-IDF và LVGL**.

Hiện project chạy trên màn hình thật với **dữ liệu áp suất mô phỏng**. Chưa tích hợp bộ thu cảm biến TPMS. Cảm ứng GT911 đã được kết nối với LVGL và ghi tọa độ chạm ra Serial; giao diện hiện chỉ hiển thị thông tin, chưa có nút thao tác.

## Chức năng hiện có

- Hiển thị áp suất theo đơn vị **bar** cho FL (trước trái), FR (trước phải), RL (sau trái), RR (sau phải).
- Mỗi bánh có biểu đồ, giá trị **MIN / MAX** và trạng thái riêng.
- Cảnh báo áp suất thấp, cao, thay đổi đột ngột và mất tín hiệu.
- Tự phục hồi hiển thị khi nhận lại dữ liệu của bánh bị mất tín hiệu.

### Biểu đồ và cảnh báo

Số áp suất cập nhật mỗi khi nhận dữ liệu. Biểu đồ thêm điểm khi có gói mới và đã đủ **30 giây tính từ điểm gần nhất**. Mẫu đầu tiên, mẫu sau khi phục hồi tín hiệu và mẫu có biến động đột ngột được thêm ngay. Nếu chưa nhận gói mới, hệ thống không tạo thêm điểm áp suất từ giá trị cũ.

| Điều kiện | Hiển thị / xử lý |
|---|---|
| Áp suất < 2,00 bar | Số và biểu đồ màu đỏ; trạng thái chung `LOW PRESSURE` |
| Áp suất > 2,80 bar | Số và biểu đồ màu cam; trạng thái chung `HIGH PRESSURE` |
| Có cả bánh áp suất thấp và bánh áp suất cao | Trạng thái chung `CHECK TIRES` |
| Chênh lệch ≥ 0,20 bar giữa hai gói liên tiếp của cùng bánh | Thêm điểm ngay; hiện `Rapid change` tại bánh trong 30 giây từ lần biến động gần nhất |
| Một bánh không nhận dữ liệu trong 150 giây | Hiện `No Signal`, thay số bằng `--`, chuyển màu xám và ngắt đường biểu đồ |
| Nhận lại dữ liệu sau mất tín hiệu | Hiện số và thêm điểm ngay; không so sánh biến động với mẫu trước khi mất tín hiệu |

Trạng thái chung ưu tiên cảnh báo áp suất, sau đó `RAPID CHANGE`, `SENSOR LOST`, `WAITING DATA`, cuối cùng `SYSTEM NORMAL`. Nhãn `No Signal` của từng bánh vẫn hiển thị khi trạng thái chung đang báo một lỗi ưu tiên hơn. Bộ kiểm tra mất tín hiệu chạy mỗi **250 ms**, kể cả khi không có gói nào; bánh chưa từng nhận dữ liệu cũng chuyển sang `No Signal` sau 150 giây.

Biểu đồ có tối đa **12 vị trí lịch sử**, gồm cả dấu ngắt khi mất tín hiệu, trong cửa sổ **5 phút 30 giây** tính đến lần ghi biểu đồ mới nhất. Trục ngang dùng thời gian thực của các điểm, với mốc `-5m30 / -2m45 / 0s`; **0s là lần ghi biểu đồ mới nhất**, không phải đồng hồ chạy liên tục. Trục dọc cố định **1,5–3,3 bar**. MIN / MAX tính trên các giá trị áp suất còn hiển thị; lịch sử bắt đầu trống và mất khi khởi động lại.

Các ngưỡng dưới đây là cấu hình thử nghiệm, nằm trong [include/tire_data.h](include/tire_data.h):

| Hằng số | Giá trị | Ý nghĩa |
|---|---|---|
| `TIRE_LOW_LIMIT` | `200` | Ngưỡng thấp, đơn vị 0,01 bar |
| `TIRE_HIGH_LIMIT` | `280` | Ngưỡng cao, đơn vị 0,01 bar |
| `TIRE_CHART_INTERVAL_MS` | `30000` | Khoảng cách tối thiểu giữa các điểm thông thường |
| `TIRE_SUDDEN_CHANGE` | `20` | Chênh lệch kích hoạt cảnh báo nhanh, đơn vị 0,01 bar |
| `TIRE_CHANGE_ALERT_MS` | `30000` | Thời gian giữ cảnh báo thay đổi nhanh |
| `TIRE_SIGNAL_TIMEOUT_MS` | `150000` | Thời gian chờ trước khi báo mất tín hiệu |

### Dữ liệu mô phỏng

[src/main.c](src/main.c) gửi đủ bốn giá trị mỗi giây từ bộ sinh dữ liệu trong [src/tire_data.c](src/tire_data.c). Chu kỳ 60 giây lặp lại:

| Thời gian trong chu kỳ | Áp suất mô phỏng |
|---|---|
| 0–14 giây | Cả bốn bánh dao động quanh 2,3 bar |
| 15–29 giây | Bánh FL giảm từ 1,90 xuống 1,62 bar |
| 30–44 giây | FL trở về bình thường; bánh RR tăng từ 2,90 lên 3,18 bar |
| 45–59 giây | Cả bốn bánh trở về quanh 2,3 bar |

Cảnh báo `Rapid change` có thể còn giữ khi áp suất đã về bình thường, kể cả đầu chu kỳ tiếp theo. FR và RL giúp quan sát nhịp biểu đồ 30 giây; FL và RR có thêm điểm đột xuất tại các lần chuyển trạng thái.

Mô phỏng luôn gửi đủ bốn bánh nên không tự tạo tình huống `No Signal`. Serial có dòng `SIMULATION enabled` và các mẫu `SIM`; trên dashboard không có nhãn phân biệt dữ liệu mô phỏng.

## Build và nạp firmware

Cần **PlatformIO Core**, hoặc **VS Code với extension PlatformIO IDE**, cùng cáp USB có truyền dữ liệu để kết nối mạch. Chạy các lệnh từ thư mục gốc project; lần build đầu cần tải toolchain và thư viện.

```bash
pio run -e waveshare_lcd_7
pio device list
pio run -e waveshare_lcd_7 -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

Thay `/dev/ttyACM0` bằng cổng thực tế từ `pio device list`. Nếu terminal chưa nhận lệnh `pio`, dùng PlatformIO terminal trong VS Code; với bản cài mặc định trên Linux có thể thay bằng `~/.platformio/penv/bin/pio`.


## Cấu hình phần cứng và thư viện

Các giá trị dưới đây là cấu hình đang dùng trong project:

| Thành phần | Cấu hình |
|---|---|
| Mạch mục tiêu | Waveshare ESP32-S3-Touch-LCD-7 |
| PlatformIO environment | `waveshare_lcd_7` |
| Platform | `espressif32@7.0.1` |
| Board nền trong PlatformIO | `esp32-s3-devkitc-1` |
| Framework | ESP-IDF; `dependencies.lock` hiện ghi 6.0.1 |
| LVGL | `9.5.0`, RGB565 |
| Flash | Cấu hình 8 MB |
| Phân vùng | NVS, PHY và một ứng dụng factory; chưa có OTA hoặc filesystem |
| PSRAM | Octal, 80 MHz; kiểm tra và in dung lượng khi khởi động |
| LCD | RGB 16 bit, 800 × 480, PCLK 16 MHz |
| Bộ đệm LVGL | 32.000 byte trong RAM nội |
| Framebuffer LCD | 768.000 byte trong PSRAM |
| Stack main task / FreeRTOS tick | 8.192 byte / 1.000 Hz |
| Tốc độ nạp / Serial Monitor | 460800 / 115200 baud |

[src/board_display.c](src/board_display.c) cấu hình các chân của mạch:

| Tín hiệu | GPIO / kết nối |
|---|---|
| RGB D0…D15, theo thứ tự | 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 |
| HSYNC / VSYNC / DE / PCLK | 46 / 3 / 5 / 7 |
| I2C SDA / SCL | 8 / 9, tốc độ 400 kHz |
| GT911 INT / địa chỉ I2C | GPIO4 / `0x5d` |
| CH422G | Địa chỉ cấu hình `0x24`, xuất tín hiệu `0x38` |
| Reset cảm ứng / đèn nền / reset LCD | CH422G EXIO1 / EXIO2 / EXIO3 |
| SD CS / chọn USB-CAN | EXIO4 HIGH / EXIO5 LOW |
