# RFID UID Copier (ESP32-S3 + RC522)

Sao chép UID thẻ RFID MIFARE 13.56MHz bằng ESP32-S3 và module RC522, điều khiển hoàn toàn qua giao diện web với **captive portal**. Hỗ trợ quét mạng WiFi, kết nối WiFi ngoài và lưu cấu hình vào bộ nhớ để tự kết nối lại.

> ⚠️ **Chỉ sử dụng để sao chép thẻ của chính bạn hoặc thẻ bạn được phép sao chép.** Việc sao chép thẻ của người khác hoặc của hệ thống không được phép có thể vi phạm pháp luật.

## Tính năng

- 📇 Đọc UID thẻ MIFARE gốc và ghi (clone) sang **magic card** (UID changeable / Gen1a).
- 🌐 Điều khiển qua web, tự bật trang nhờ **captive portal** (DNS server bắt mọi truy vấn).
- 📶 **Quét mạng WiFi** bất đồng bộ, hiển thị tên mạng + cường độ tín hiệu.
- 💾 **Lưu WiFi vào NVS** (Preferences) → tự kết nối lại sau khi khởi động.
- 🔁 **Quản lý hotspot tự động**:
  - Kết nối được WiFi → **tắt hotspot** (chỉ chạy STA).
  - Không kết nối được → **bật hotspot sau 30 giây** kể từ khi khởi động.
  - Lần đầu chưa có WiFi lưu → bật hotspot ngay để cấu hình.
- 💡 LED RGB onboard báo trạng thái: xanh dương = đang chờ thẻ, xanh lá = thành công, đỏ = thất bại, vàng = hết thời gian.

## Phần cứng

| Thiết bị | Ghi chú |
|----------|---------|
| ESP32-S3-DevKitC-1 (8MB) **hoặc** ESP32-S3 Super Mini (4MB) | RGB LED onboard ở GPIO48 |
| Module RFID-RC522 | 13.56MHz, giao tiếp SPI, **cấp nguồn 3.3V** |
| Magic card (UID changeable / Gen1a) | Bắt buộc nếu muốn clone UID |

### Sơ đồ đấu dây RC522

| RC522 | ESP32-S3 (GPIO) |
|-------|-----------------|
| SDA (SS) | GPIO10 |
| SCK | GPIO12 |
| MOSI | GPIO11 |
| MISO | GPIO13 |
| RST | GPIO9 |
| IRQ | (không nối) |
| GND | GND |
| **VCC** | **3.3V** ⚠️ KHÔNG dùng 5V |

## Build & Nạp (PlatformIO)

```bash
# ESP32-S3-DevKitC-1 (mặc định)
pio run --target upload

# ESP32-S3 Super Mini (4MB)
pio run -e esp32-s3-supermini --target upload
```

Thư viện phụ thuộc (tự động cài bởi PlatformIO):

- `adafruit/Adafruit NeoPixel`
- `miguelbalboa/MFRC522`

> Board dùng USB-Serial/JTAG native nên đã bật sẵn `ARDUINO_USB_MODE=1` và `ARDUINO_USB_CDC_ON_BOOT=1` để Serial xuất qua cổng USB.

## Cách sử dụng

1. Cấp nguồn cho board. Nếu chưa cấu hình WiFi, hotspot sẽ bật ngay.
2. Kết nối vào WiFi:
   - **SSID:** `RFID-Copier`
   - **Mật khẩu:** `12345678`
3. Trang điều khiển tự bật (hoặc mở `http://192.168.4.1`, hoặc `http://rfid.local` qua mDNS).

### Tab "Copy thẻ"

1. Bấm **Đọc thẻ GỐC** → áp thẻ gốc vào đầu đọc → UID hiển thị.
2. Đặt **thẻ magic** lên đầu đọc.
3. Bấm **Ghi sang thẻ MAGIC**:
   - Xanh lá = clone thành công.
   - Đỏ = thất bại (thẻ không phải magic card).

### Tab "WiFi"

1. Bấm **Quét mạng WiFi** → chọn mạng → nhập mật khẩu → **Kết nối & Lưu**.
2. Khi kết nối thành công, hotspot tắt; truy cập board qua IP do router cấp.
3. Nút **Quên WiFi đã lưu** để xóa cấu hình khỏi bộ nhớ.

## Giới hạn

- Chỉ đọc/ghi thẻ **13.56MHz (MIFARE / ISO14443A)**. Thẻ **125kHz (EM4100)** cần module khác (RDM6300, EM-18...).
- Clone UID hỗ trợ **UID 4 byte** (MIFARE Classic 1K phổ biến).
- Clone toàn bộ dữ liệu (1KB) chỉ khả thi nếu thẻ dùng key mặc định; thẻ mã hóa cần thiết bị chuyên dụng (Proxmark).
- Khi board kết nối WiFi ngoài, hotspot và STA dùng chung 1 radio nên có thể đổi kênh trong giây lát.

## Cấu hình

Có thể chỉnh trong `src/main.cpp`:

| Hằng số | Mặc định | Ý nghĩa |
|---------|----------|---------|
| `AP_SSID` / `AP_PASS` | `RFID-Copier` / `12345678` | Tên & mật khẩu hotspot (mật khẩu ≥ 8 ký tự) |
| `WAIT_CARD_MS` | `10000` | Thời gian chờ thẻ (ms) |
| `AP_FALLBACK_MS` | `30000` | Thời gian chờ trước khi bật hotspot fallback (ms) |
| `LED_PIN` | `48` | Chân RGB LED onboard |

## License

MIT
