# 🚗 TirePressureScreen (TPMS Dashboard)

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-red.svg)](https://github.com/espressif/esp-idf)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Supported-orange.svg)](https://platformio.org/)
[![LVGL](https://img.shields.io/badge/LVGL-v9.5.0-blue.svg)](https://lvgl.io/)
[![Hardware](https://img.shields.io/badge/Board-Waveshare_ESP32--S3--Touch--LCD--7-green.svg)](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
[![RF](https://img.shields.io/badge/RF-CC1101_433.92MHz-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)]()

Hệ thống giám sát và hiển thị áp suất lốp 4 bánh (**TPMS - Tire Pressure Monitoring System**) thời gian thực dành cho ô tô, hoạt động độc lập trên màn hình cảm ứng **7 inch Waveshare ESP32-S3-Touch-LCD-7** (độ phân giải **800 × 480**), phát triển bằng **C** với **ESP-IDF v6** và **LVGL v9.5**.

Hệ thống tích hợp trực tiếp bộ thu sóng RF **TI CC1101 (433.92 MHz)** cùng nhân giải mã nhúng **rtl_433** hỗ trợ hơn **30+ giao thức cảm biến TPMS** phổ biến trên thị trường, đồng thời hỗ trợ kênh thu không dây **ESP-NOW 2.4 GHz**.

---

## 📸 Điểm nổi bật & Tính năng chính

- **Giao diện xe hơi chuyên nghiệp (Automotive UI)**:
  - Thiết kế trên nền **LVGL 9.5**, tối ưu mượt mà với phần cứng gia tốc đồ họa của ESP32-S3.
  - Trung tâm hiển thị đồ họa xe nhìn từ trên (**Car Top View**).
  - 4 khối thẻ hiển thị độc lập cho 4 bánh xe: **FL** (Trước trái), **FR** (Trước phải), **RL** (Sau trái), **RR** (Sau phải).
  - Hiển thị giá trị áp suất sắc nét (đơn vị **bar** và **kPa**), nhiệt độ lốp (°C) và chất lượng sóng thu (**RSSI** dBm).
  - Tích hợp **biểu đồ lịch sử (sparkline)** 12 điểm với trục thời gian thực và tự động tính dải **MIN / MAX**.

- **Hệ thống cảnh báo an toàn đa cấp**:
  - 🔴 **Áp suất thấp (Low Pressure)**: Dưới `2.00 bar` (chữ và biểu đồ đổi màu đỏ).
  - 🟠 **Áp suất cao (High Pressure)**: Trên `2.80 bar` (chữ và biểu đồ đổi màu cam).
  - ⚡ **Tụt áp / Biến động đột ngột (Rapid Change)**: Chênh lệch $\ge$ `0.20 bar` giữa 2 lần phát liên tiếp (cảnh báo duy trì 30 giây).
  - ⚠️ **Mất tín hiệu cảm biến (Sensor Lost / No Signal)**: Không nhận được gói tin quá `150 giây` (thẻ chuyển màu xám mờ, hiển thị `--`).
  - Thanh trạng thái tổng quát: `SYSTEM NORMAL`, `LOW PRESSURE`, `HIGH PRESSURE`, `RAPID CHANGE`, `SENSOR LOST`, `WAITING DATA`, `CHECK TIRES`.

- **Thu sóng trực tiếp 433.92 MHz qua CC1101 & nhúng rtl_433**:
  - Thu nhận tín hiệu không dây OOK/FSK trực tiếp từ cảm biến gắn van lốp qua transceiver **CC1101**.
  - Bắt xung micro giây chính xác cao qua ngắt phần cứng chân **GDO0**.
  - Tích hợp nhân C của thư viện nổi tiếng **rtl_433**, giải mã trực tiếp trên ESP32 mà không cần máy tính hay USB SDR rời.

- **Quản lý & ghi nhớ cảm biến (NVS Persistence)**:
  - Ánh xạ mã định danh cảm biến (**Sensor ID**) cho từng vị trí bánh.
  - Hỗ trợ gán cố định ID hoặc cơ chế **tự động học (auto-bind)** khi nhận được cảm biến mới.
  - Bộ lọc chống trùng lặp gói tin thông minh (chống nghẽn khi cảm biến phát burst).
  - Tự động lưu cấu hình và trạng thái vào bộ nhớ Flash **NVS**, khôi phục tức thì khi bật chìa khóa xe.

- **Kênh thu dự phòng ESP-NOW**:
  - Hỗ trợ giao thức **ESP-NOW (2.4 GHz)** để kết nối với bộ thu trung gian/relay từ xa nếu không nối trực tiếp CC1101 vào màn hình.

---

## 🚗 Danh sách cảm biến TPMS được hỗ trợ

Nhờ nhân giải mã nhúng **rtl_433**, firmware tương thích với cảm biến TPMS của hầu hết các hãng xe và thương hiệu cảm biến phổ biến:

| Hãng xe / Nhà sản xuất | Decoder trong firmware |
|---|---|
| **Toyota / Lexus** | `tpms_toyota.c`, `tpms_pmv107j.c` |
| **Honda / Acura** | `tpms_honda.c` |
| **Hyundai / Kia** | `tpms_hyundai_vdo.c`, `tpms_kia.c`, `tpms_elantra2012.c` |
| **Ford / Lincoln** | `tpms_ford.c` |
| **Nissan / Infiniti** | `tpms_nissan.c` |
| **BMW / Mini** | `tpms_bmw.c`, `tpms_bmw_g3.c` |
| **Mercedes-Benz** | `tpms_mercedes_benz.c` |
| **Porsche** | `tpms_porsche.c` |
| **Audi / VW / Porsche / VDO** | `schraeder.c`, `tpms_schrader_motorcycle.c` |
| **Renault / Dacia / Citroen** | `tpms_renault.c`, `tpms_renault_0435r.c`, `tpms_citroen.c`, `tpms_renault_0435r.c` |
| **GM / Chevrolet / GMC / Buick** | `tpms_gm.c` |
| **Jeep / Chrysler / Dodge / RAM** | `tpms_jeep.c`, `tpms_abarth124.c` |
| **Cảm biến gắn ngoài phổ thông** | `tpms_jansite.c`, `tpms_jansite_solar.c`, `tpms_jansite_ty468.c`, `tpms_jansite_ty588.c` |
| **Hệ thống thương mại & xe tải** | `tpms_truck.c`, `tpms_trw.c`, `tpms_smartire.c`, `tpms_tyreguard400.c`, `tpms_eezrv.c`, `tpms_ave.c`, `tpms_sefis_m3.c`, `tpms_gear_hive.c`, `tpms_airpuxem.c`, `tpms_imars_t240.c` |

---

## 🛠 Kiến trúc hệ thống

```mermaid
flowchart TD
    subgraph SENSORS["Cảm biến bánh xe (433.92 MHz)"]
        S1["FL: Bánh trước trái"]
        S2["FR: Bánh trước phải"]
        S3["RL: Bánh sau trái"]
        S4["RR: Bánh sau phải"]
    end

    subgraph RF_HW["Tầng phần cứng RF"]
        CC["Module CC1101 (SPI Mode 0)"]
        GDO0["Chân GDO0 (Ngắt GPIO6 / Bắt xung us)"]
    end

    subgraph RTL_CORE["Giải mã & Pipeline"]
        SLICER["Pulse Slicer / Analyzer"]
        DEC["Bộ giải mã rtl_433 (30+ Decoders)"]
        INGEST["tpms_espnow_ingest_packet()"]
        MAPPER["Bộ ánh xạ Sensor ID ↔ Bánh xe & Lọc trùng"]
        QUEUE[("Hàng đợi FreeRTOS (Queue)")]
    end

    subgraph SYSTEM_STORAGE["Lưu trữ"]
        NVS[("Flash NVS: Sensor IDs & State")]
    end

    subgraph UI_TASK["Giao diện LVGL 9.5"]
        UI_LOOP["Task LVGL (app_ui_receive)"]
        CAR["Car Top View & Card 4 bánh"]
        CHART["Sparkline Charts & Min/Max"]
        ALERT["Bộ cảnh báo đa cấp"]
        LCD["LCD 800x480 RGB 16-bit"]
    end

    S1 & S2 & S3 & S4 -. Sóng RF 433.92 MHz .-> CC
    CC --> GDO0
    GDO0 --> SLICER --> DEC --> INGEST
    INGEST --> MAPPER
    NVS <--> MAPPER
    MAPPER --> QUEUE --> UI_LOOP
    UI_LOOP --> CAR & CHART & ALERT --> LCD
```

---

## 🔌 Sơ đồ đấu nối phần cứng

Màn hình **Waveshare ESP32-S3-Touch-LCD-7** sử dụng phần lớn GPIO cho bus RGB LCD 16-bit. Dự án sử dụng giải pháp **ghép kênh chân (pin multiplexing)** thông minh để kết nối CC1101 qua các đầu cắm PH2.0 có sẵn trên bo mạch:

> [!IMPORTANT]
> **Cần gạt công tắc `SW1` trên bo mạch sang vị trí `"UART2"`** để mở đường tín hiệu cho connector H3.

| Chân trên CC1101 | Cổng kết nối trên Waveshare | Chân cổng PH2.0 | GPIO ESP32-S3 | Ghi chú |
|---|---|---|---|---|
| **VCC (3.3V)** | **Sensor AD (J8)** | Pin 1 (3V3) | — | Cấp nguồn 3.3V sạch cho RF |
| **GND** | **Sensor AD (J8)** | Pin 2 (GND) | — | Nối mass chung |
| **GDO0** | **Sensor AD (J8)** | Pin 3 (AD) | **GPIO 6** | Tín hiệu ngắt bắt xung micro-giây |
| **CSN (CS)** | **UART2 (H3)** | Pin 4 (TXD) | **GPIO 43** | Chip Select SPI (Tự nhận diện nếu đảo với RXD) |
| **MISO (SO)** | **UART2 (H3)** | Pin 3 (RXD) | **GPIO 44** | SPI Data Out từ CC1101 |
| **MOSI (SI)** | **I2C (H1)** | Pin 3 (SDA) | **GPIO 8** | Chân chia sẻ với I2C (được bảo vệ bằng mutex) |
| **SCLK (SCK)** | **I2C (H1)** | Pin 4 (SCL) | **GPIO 9** | Chân chia sẻ với I2C (được bảo vệ bằng mutex) |

### Cơ chế bảo vệ I2C độc quyền:
Chân GPIO 8 & 9 vốn dùng cho bus I2C (kết nối cảm ứng **GT911** và chip mở rộng **CH422G**). Khi firmware cần giao tiếp SPI với CC1101, hàm `cc1101_spi_begin_locked()` sẽ khóa mutex I2C, tạm chuyển ma trận chân GPIO sang chế độ Push-Pull (đảm bảo kéo SCLK xuống trước khi hạ CSN để **không bao giờ sinh tín hiệu I2C START/STOP giả**), thực hiện giao dịch SPI tốc độ cao và hoàn trả bus I2C nguyên vẹn.

---

## 📊 Bảng thông số kỹ thuật & Cấu hình

| Thành phần | Thông số triển khai |
|---|---|
| **Vi điều khiển** | ESP32-S3 Dual-Core LX7, 240 MHz |
| **Bộ nhớ Flash / PSRAM** | 8 MB Flash / 8 MB Octal PSRAM @ 80 MHz |
| **Màn hình LCD** | 7.0 inch TFT RGB 16-bit, độ phân giải **800 × 480**, PCLK 16 MHz |
| **Bộ đệm đồ họa (Buffer)** | Framebuffer LCD 768.000 bytes trong PSRAM; LVGL buffer 32.000 bytes trong Internal SRAM |
| **Cảm ứng** | Goodix GT911 (I2C `0x5D`, INT GPIO4) |
| **Chip mở rộng I/O** | CH422G (I2C `0x24`): điều khiển Đèn nền, Reset cảm ứng, Reset LCD, SD CS |
| **Tần số sóng RF** | **433.92 MHz** (băng tần chuẩn TPMS toàn cầu) |
| **Thời gian timeout mất tín hiệu** | **150 giây** (tự chuyển trạng thái `No Signal`) |
| **Khoảng cách điểm biểu đồ** | Tối thiểu 30 giây / điểm (cập nhật tức thì nếu có biến động đột ngột) |
| **Ngưỡng áp suất chuẩn** | Thấp: `< 2.00 bar` (200 cbar) \| Cao: `> 2.80 bar` (280 cbar) |
| **Ngưỡng tụt áp nhanh** | $\Delta \ge$ `0.20 bar` (20 cbar) giữa 2 gói tin liên tiếp |

---

## ⚙️ Cài đặt & Nạp Firmware

### 1. Yêu cầu môi trường
- [VS Code](https://code.visualstudio.com/) đã cài đặt extension **PlatformIO IDE** (hoặc đã cài **PlatformIO Core CLI**).
- Dây cáp USB Type-C truyền dữ liệu tốt.

### 2. Cấu hình ID cảm biến (Tùy chọn)
Nếu bạn đã biết mã ID in trên 4 cảm biến TPMS của xe, mở file [`include/tpms_espnow.h`](include/tpms_espnow.h) và cập nhật:
```c
#define TPMS_SENSOR_ID_FL "2DE995"  // Bánh trước trái
#define TPMS_SENSOR_ID_FR "7EA39A"  // Bánh trước phải
#define TPMS_SENSOR_ID_RL "D2F8BD"  // Bánh sau trái
#define TPMS_SENSOR_ID_RR "D31213"  // Bánh sau phải
```
> [!TIP]
> Nếu bạn chưa biết ID cảm biến, hãy để trống `""` (ví dụ `#define TPMS_SENSOR_ID_FL ""`). Firmware sẽ tự động gán lần lượt từng cảm biến mới nhận được vào các bánh xe còn trống (**Auto-bind mode**) và lưu vĩnh viễn vào NVS Flash!

### 3. Build & Nạp chương trình

Mở terminal tại thư mục gốc của project:

```bash
# 1. Biên dịch firmware
pio run -e waveshare_lcd_7

# 2. Kiểm tra cổng kết nối USB
pio device list

# 3. Nạp firmware lên bo mạch (thay /dev/ttyACM0 bằng cổng thực tế của bạn)
pio run -e waveshare_lcd_7 -t upload --upload-port /dev/ttyACM0

# 4. Mở Serial Monitor để theo dõi gói tin và debug
pio device monitor -p /dev/ttyACM0 -b 115200
```

---

## 📂 Cấu trúc thư mục nguồn

```
TirePressureScreen/
├── CMakeLists.txt              # Cấu hình gốc ESP-IDF CMake
├── platformio.ini              # Cấu hình môi trường PlatformIO
├── partitions.csv              # Bảng phân vùng Flash 8MB (Factory App + NVS)
├── sdkconfig.defaults          # Cấu hình Kconfig mặc định (PSRAM, Cache, Heap)
├── include/
│   ├── app_ui.h                # API điều khiển giao diện Dashboard
│   ├── board_display.h         # API khởi tạo màn hình LCD & Bus I2C/SPI
│   ├── tire_data.h             # Định nghĩa ngưỡng áp suất & kiểu dữ liệu bánh
│   ├── tpms_cc1101.h           # Điều khiển phần cứng CC1101 & ngắt bắt xung
│   ├── tpms_espnow.h           # Bộ lọc gói tin, cấu hình ID cảm biến & ESP-NOW
│   └── rtl_433/                # Header giải mã giao thức RF TPMS
├── src/
│   ├── main.c                  # Điểm khởi chạy app_main, luồng FreeRTOS, lưu NVS
│   ├── board_display.c         # Driver ST7262 RGB LCD, GT911 touch, CH422G expander
│   ├── app_ui.c                # Xử lý cập nhật số liệu UI, biểu đồ, cảnh báo
│   ├── tpms_cc1101.c           # Giao tiếp CC1101 SPI, demodulation OOK, ngắt GDO0
│   ├── tpms_espnow.c           # Quản lý hàng đợi, ánh xạ ID cảm biến, chống lặp
│   ├── tire_data.c             # Phân loại áp suất & dữ liệu mô phỏng demo
│   ├── rtl_433/                # Mã nguồn C giải mã các giao thức TPMS (30+ xe)
│   └── UI/                     # Tài nguyên giao diện LVGL
│       ├── fonts/              # Custom font áp suất 52pt, nhãn 18pt, đơn vị 22pt
│       ├── images/             # Đồ họa hình xe nhìn từ trên (Car Top View)
│       └── screens/            # Mã nguồn sinh tự động giao diện dashboard LVGL
```

---

## 🧭 Kế hoạch phát triển tiếp theo (Roadmap)

- [x] Giao diện Dashboard xe hơi 800x480 với LVGL 9.5.
- [x] Tích hợp thu trực tiếp CC1101 433.92 MHz & 30+ decoder rtl_433.
- [x] Lưu trữ cấu hình Sensor ID và trạng thái vào Flash NVS.
- [ ] Giao diện cảm ứng cho phép đảo lốp (Tire Rotation) trực tiếp trên màn hình.
- [ ] Menu cài đặt ngưỡng cảnh báo áp suất/nhiệt độ linh hoạt theo ý người dùng.
- [ ] Hiển thị cảnh báo bằng âm thanh qua còi buzzer hoặc thông báo giọng nói qua loa I2S.
- [ ] Hỗ trợ cảm biến TPMS chuẩn Bluetooth Low Energy (BLE 4.2 / 5.0).

---

## 📄 Bản quyền (License)

Dự án được phát hành theo giấy phép **MIT License**. Phần giải mã vô tuyến sử dụng mã nguồn phái sinh từ dự án mã nguồn mở [rtl_433](https://github.com/merbanan/rtl_433).
