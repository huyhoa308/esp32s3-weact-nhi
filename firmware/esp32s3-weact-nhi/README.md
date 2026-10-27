# Board `esp32s3-weact-nhi` – Robot Nhi AI để bàn

Robot trợ lý AI để bàn: màn hình hiển thị đôi mắt RoboEyes + đầu quay bằng 2 servo
(PAN trái/phải, TILT cúi/ngẩng). Không di chuyển, không camera, không cảm biến khoảng cách.

## Sơ đồ chân

Tất cả GPIO nằm trong [`board_pins.h`](board_pins.h). Chọn phần cứng bằng `NHI_HW_OLED`.

### `NHI_HW_OLED 1` (mặc định): mạch đỏ xiaozhi bread-compact

| Linh kiện | Chân linh kiện → GPIO |
|---|---|
| OLED SSD1306 128×64 (cổng `OLED-1.3`) | SDA 41 · SCL 42 · VCC 3.3V |
| Mic INMP441 | WS 4 · SCK 5 · SD 6 · L/R → GND |
| Loa MAX98357A | DIN 7 · BCLK 15 · LRC 16 · GAIN/SD để hở |
| Servo PAN (quay trái/phải) | tín hiệu GPIO 17 |
| Servo TILT (cúi/ngẩng) | tín hiệu GPIO 18 |
| Nút BOOT | GPIO 0 (nhấn để nói / ngắt lời) |
| LED RGB WS2812 | GPIO 48 (trên board) |

Mắt được vẽ như trên TFT rồi chuyển sang đơn sắc 1-bit (20 khung/giây qua I2C). Các hiệu ứng
(!, ?, Zzz, tim, nước mắt) thu nhỏ theo `EYES_FX_PCT`. Màn quá nhỏ để hiện chữ hội thoại nên chỉ hiện
thông báo hệ thống (Wi-Fi, mã kích hoạt). Màn bị ngược thì đổi `DISPLAY_MIRROR_X/Y` trong `config.h`.

### `NHI_HW_OLED 0`: WeAct CoreBoard + TFT

| Linh kiện | Chân linh kiện → GPIO |
|---|---|
| TFT ILI9341 (SPI) | SCK 40 · MOSI 41 · CS 39 · DC 38 · RST 47 · LED + VCC → 3.3V |
| Mic INMP441 | SCK 16 · WS 17 · SD 14 · L/R → GND · VDD 3.3V |
| Loa PCM5100A (WeAct I2S Speaker) | BCK 16 · WS 17 · DIN 15 · MC → GND · SD → 3.3V · VCC 5V |
| Servo PAN / TILT | GPIO 4 / GPIO 5 |

### Chung

- Servo dùng nguồn 5V riêng (≥ 2A), **GND chung** với ESP32. Không lấy 5V servo từ chân 5V của board khi cắm USB.
- Cả 2 servo phải là loại **180°** (SG90/MG90S).

## Biểu cảm mắt + chuyển động đầu

Bảng ánh xạ nằm trong [`nhi_behavior.cc`](nhi_behavior.cc), các chuyển động trong [`head_servos.cc`](head_servos.cc).

| Trạng thái | Mắt | Đầu (motion) |
|---|---|---|
| Bình thường (idle) | chớp mắt, nhìn quanh | thỉnh thoảng quay nhẹ theo hướng nhìn (`Glance`) |
| Vui vẻ (`happy`, `laughing`, `funny`, `silly`) | mắt cười, vàng | lắc trái/phải (`happy_shake`) |
| Buồn (`sad`, `crying`, `embarrassed`) | mắt cụp, xanh | cúi đầu chậm (`sad_droop`) |
| Tức giận (`angry`) | mắt cau, đỏ | lắc đầu ngắn, nhanh (`angry_shake`) |
| Ngạc nhiên (`surprised`, `shocked`) | mở to, "!" | ngẩng lên (`look_up`) |
| Suy nghĩ (`thinking`, `confused`, sau khi nghe xong câu hỏi) | nhìn lệch, "?" | quay lệch một phía, hơi ngẩng (`tilt_side`) |
| Đang nghe | mắt mở to, tập trung | ngẩng nhẹ (`listen_up`) |
| Đang nói | giữ biểu cảm hiện tại | gật nhẹ / đung đưa luân phiên mỗi 3 s (`nod`, `sway`) |
| Buồn ngủ (`sleepy`, hoặc idle 3 phút) | lim dim, Zzz | cúi nhẹ rồi giữ yên (`sleep_droop`) |

Chuyển động mượt: tăng tốc dần lúc bắt đầu (`SERVO_ACCEL_DEG_S2`), giảm tốc ở 12° cuối
(`SERVO_EASE_DEG`), 3 mức tốc độ chậm/thường/nhanh. Mọi góc đều bị kẹp trong giới hạn cơ khí của từng trục.

## Căn chỉnh cơ khí (`nhi_config.h`)

```c
// gpio, trim_deg, invert, min_deg, max_deg, detach_idle
static constexpr AxisConfig kAxisConfig[AXIS_COUNT] = {
    {PIN_SERVO_PAN, 0, false, 50, 130, true},
    {PIN_SERVO_TILT, 0, false, 70, 115, false},
};
```

1. Lắp servo khi nó đang ở 90° (bật robot, chờ đầu về giữa rồi mới bắt cánh tay servo).
2. Đầu lệch khi "nhìn thẳng" → chỉnh `trim_deg` (ví dụ `-5`).
3. Đầu quay ngược chiều (vui thì lắc đúng, nhưng "ngẩng" lại thành "cúi") → đặt `invert = true` cho trục đó.
4. Đầu chạm khung → thu hẹp `min_deg` / `max_deg`.
5. `detach_idle`: PAN tắt xung sau 1.5 s đứng yên (êm, không rung); TILT giữ xung để màn hình không bị trĩu.
6. Thử phần mắt và giọng nói trước khi lắp servo: đặt `NHI_SERVOS_ENABLED 0`.

## Gọi robot

- Nói **"Chào Nhi"** (wake word MultiNet, phiên âm `chao ni`, cấu hình trong `config.json`).
- Hoặc nhấn nút **BOOT**.

## Build

Xem [README chính](../../README.md): áp `patches/*.patch` lên xiaozhi-esp32 @ `c0fe42a`, rồi

```powershell
python scripts/build.py esp32s3-weact-nhi --language vi-VN
```
