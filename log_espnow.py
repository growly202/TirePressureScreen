#!/home/slowly/.platformio/penv/bin/python3
"""
Script ghi log tín hiệu ESP-NOW trực tiếp từ màn hình ESP32-S3 (hoặc ESP32-C3) ra file.
Tự động lưu:
  1. File log thô đầy đủ timestamp: logs/espnow_raw_<YYYYMMDD_HHMMSS>.log
  2. File CSV thống kê từng gói nhận được: logs/espnow_packets_<YYYYMMDD_HHMMSS>.csv
"""

import argparse
import csv
import datetime
import glob
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    print("Thiếu thư viện pyserial. Cài đặt bằng: pip install pyserial", file=sys.stderr)
    sys.exit(1)

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")

# Ví dụ dòng log ESPNOW_DATA trên ESP32-S3:
# ESPNOW_DATA seq=12 c3_up=45230ms wheel=0 model=Jansite-TY468 id=82f7d4 pressure=2.31 bar (231.0 kPa) temp=28.5 C flags=0x00 rf_rssi=-54 dBm ch=1
DATA_RE = re.compile(
    r"ESPNOW_DATA\s+seq=(?P<seq>\d+)\s+c3_up=(?P<c3_up>\d+)ms\s+"
    r"wheel=(?P<wheel>\d+)\s+model=(?P<model>\S+)\s+id=(?P<sensor_id>\S+)\s+"
    r"pressure=(?P<pressure_bar>[0-9.]+)\s+bar\s+\((?P<pressure_kpa>[0-9.]+)\s+kPa\)\s+"
    r"temp=(?P<temp_c>[-0-9.]+)\s+C\s+flags=(?P<flags>0x[0-9A-Fa-f]+)\s+"
    r"rf_rssi=(?P<rf_rssi>[-0-9]+)\s+dBm\s+ch=(?P<ch>\d+)"
)

# Ví dụ dòng log ESPNOW_HB trên ESP32-S3:
# ESPNOW_HB src=aa:bb:cc:dd:ee:ff ch=1 c3_up=15002ms tx_seq=3 raw:18 rf_rssi=-61 dBm wifi_rssi=-42 dBm
HB_RE = re.compile(
    r"ESPNOW_HB\s+src=(?P<src>[0-9a-fA-F:]+)\s+ch=(?P<ch>\d+)\s+"
    r"c3_up=(?P<c3_up>\d+)ms\s+tx_seq=(?P<seq>\d+)\s+raw:(?P<raw_count>\d+)\s+"
    r"rf_rssi=(?P<rf_rssi>[-0-9]+)\s+dBm\s+wifi_rssi=(?P<wifi_rssi>[-0-9]+)\s+dBm"
)

# Ví dụ dòng log trực tiếp trên ESP32-C3:
# [ESP-NOW] Sent TPMS #1 -> Wheel=0 (FL) Pressure=2.31 bar (231.0 kPa) Temp=28.5 C ID=82f7d4 Model=Jansite-TY468 RSSI=-54 dBm Ch=1
C3_DATA_RE = re.compile(
    r"\[ESP-NOW\]\s+(?:Sent|Failed)\s+TPMS\s+#(?P<seq>\d+)\s+->\s+"
    r"Wheel=(?P<wheel>\d+)\s+\(\S+\)\s+"
    r"Pressure=(?P<pressure_bar>[0-9.]+)\s+bar\s+\((?P<pressure_kpa>[0-9.]+)\s+kPa\)\s+"
    r"Temp=(?P<temp_c>[-0-9.]+)\s+C\s+ID=(?P<sensor_id>\S+)\s+Model=(?P<model>\S+)\s+"
    r"RSSI=(?P<rf_rssi>[-0-9]+)\s+dBm\s+Ch=(?P<ch>\d+)"
)


def auto_detect_port() -> str:
    candidates = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if not candidates:
        return ""
    return candidates[0]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Ghi log tín hiệu ESP-NOW nhận được ra file (.log và .csv) để kiểm tra độ ổn định."
    )
    parser.add_argument(
        "-p", "--port",
        default="",
        help="Cổng Serial (mặc định tự tìm /dev/ttyACM0 hoặc /dev/ttyUSB0)",
    )
    parser.add_argument(
        "-b", "--baud",
        type=int,
        default=115200,
        help="Tốc độ baud (mặc định 115200 cho ESP32-S3; dùng 921600 nếu cắm trực tiếp ESP32-C3)",
    )
    parser.add_argument(
        "-o", "--outdir",
        default="logs",
        help="Thư mục lưu file log (mặc định: ./logs)",
    )
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    ts_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    raw_log_path = os.path.join(args.outdir, f"espnow_raw_{ts_str}.log")
    csv_log_path = os.path.join(args.outdir, f"espnow_packets_{ts_str}.csv")

    port = args.port or auto_detect_port()
    if not port:
        print("Đang chờ cắm cáp USB (ESP32-S3 / ESP32-C3)...")
        while not port:
            time.sleep(1.0)
            port = auto_detect_port()

    print(f"=== BẮT ĐẦU GHI LOG ESP-NOW ===")
    print(f"  Cổng Serial : {port} @ {args.baud} baud")
    print(f"  File Raw Log: {os.path.abspath(raw_log_path)}")
    print(f"  File CSV    : {os.path.abspath(csv_log_path)}")
    print("  Nhấn Ctrl+C để dừng và xem tổng kết.\n")

    total_data_pkts = 0
    total_hb_pkts = 0
    missed_data_seq = 0
    last_data_seq = None
    last_data_time = None

    with open(raw_log_path, "a", encoding="utf-8", buffering=1) as raw_file, \
         open(csv_log_path, "w", newline="", encoding="utf-8", buffering=1) as csv_file:

        writer = csv.writer(csv_file)
        writer.writerow([
            "pc_timestamp",
            "pkt_type",
            "seq",
            "c3_uptime_ms",
            "delta_s",
            "missed_seq",
            "wheel",
            "model",
            "sensor_id",
            "pressure_bar",
            "pressure_kpa",
            "temp_c",
            "flags",
            "rf_rssi_dbm",
            "wifi_rssi_dbm",
            "channel",
            "c3_raw_rf_count",
        ])

        try:
            while True:
                try:
                    with serial.Serial(port, args.baud, timeout=1.0) as ser:
                        while True:
                            raw_bytes = ser.readline()
                            if not raw_bytes:
                                continue
                            now = datetime.datetime.now()
                            now_str = now.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                            line = raw_bytes.decode("utf-8", errors="replace").rstrip("\r\n")
                            clean_line = ANSI_RE.sub("", line)

                            raw_file.write(f"[{now_str}] {clean_line}\n")
                            print(f"[{now_str}] {clean_line}")

                            m_data = DATA_RE.search(clean_line) or C3_DATA_RE.search(clean_line)
                            if m_data:
                                seq = int(m_data.group("seq"))
                                gd = m_data.groupdict()
                                c3_up = gd.get("c3_up", "")
                                flags = gd.get("flags", "0x00")
                                delta_s = ""
                                gap = 0
                                if last_data_time is not None:
                                    delta_s = f"{(now - last_data_time).total_seconds():.2f}"
                                if last_data_seq is not None and seq > last_data_seq + 1:
                                    gap = seq - last_data_seq - 1
                                    missed_data_seq += gap

                                last_data_time = now
                                last_data_seq = seq
                                total_data_pkts += 1

                                writer.writerow([
                                    now_str,
                                    "SENSOR_DATA",
                                    seq,
                                    c3_up,
                                    delta_s,
                                    gap,
                                    gd["wheel"],
                                    gd["model"],
                                    gd["sensor_id"],
                                    gd["pressure_bar"],
                                    gd["pressure_kpa"],
                                    gd["temp_c"],
                                    flags,
                                    gd["rf_rssi"],
                                    "",
                                    gd["ch"],
                                    "",
                                ])
                                print(
                                    f"  >>> [THỐNG KÊ TPMS] Đã nhận={total_data_pkts} gói | "
                                    f"Rớt gói={missed_data_seq} | Δt={delta_s or '0.00'}s | "
                                    f"P={m_data.group('pressure_bar')} bar | T={m_data.group('temp_c')}°C | "
                                    f"RF_RSSI={m_data.group('rf_rssi')} dBm"
                                )
                                continue

                            m_hb = HB_RE.search(clean_line)
                            if m_hb:
                                total_hb_pkts += 1
                                writer.writerow([
                                    now_str,
                                    "HEARTBEAT",
                                    m_hb.group("seq"),
                                    m_hb.group("c3_up"),
                                    "",
                                    0,
                                    "",
                                    "ESP32-C3_CC1101",
                                    m_hb.group("src"),
                                    "",
                                    "",
                                    "",
                                    "",
                                    m_hb.group("rf_rssi"),
                                    m_hb.group("wifi_rssi"),
                                    m_hb.group("ch"),
                                    m_hb.group("raw_count"),
                                ])
                except serial.SerialException as e:
                    print(f"[Cảnh báo] Mất kết nối Serial ({e}), thử kết nối lại sau 2s...")
                    time.sleep(2.0)
                    if not args.port:
                        new_port = auto_detect_port()
                        if new_port:
                            port = new_port
        except KeyboardInterrupt:
            print("\n=== TỔNG KẾT PHIÊN GHI LOG ESP-NOW ===")
            print(f"  Tổng gói TPMS nhận được : {total_data_pkts}")
            print(f"  Tổng gói Heartbeat nhận : {total_hb_pkts}")
            print(f"  Số gói TPMS bị hụt (seq): {missed_data_seq}")
            print(f"  File Raw Log đã lưu     : {os.path.abspath(raw_log_path)}")
            print(f"  File CSV đã lưu         : {os.path.abspath(csv_log_path)}")


if __name__ == "__main__":
    main()
