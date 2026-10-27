// "Nhi" AI desk robot on an ESP32-S3 N16R8. RoboEyes + 2 head servos (PAN/TILT),
// wake word "Chào Nhi" or the BOOT button. Hardware variant in board_pins.h:
//  - NHI_HW_OLED 1: red bread-compact PCB, SSD1306 OLED, INMP441 + MAX98357 (simplex)
//  - NHI_HW_OLED 0: WeAct CoreBoard, ILI9341 TFT, INMP441 + PCM5100A (one I2S bus)
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <driver/spi_common.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <mutex>
#include <vector>

#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "eyes_display.h"
#include "head_servos.h"
#include "led/single_led.h"
#include "nhi_behavior.h"
#include "nhi_config.h"
#include "wifi_board.h"

#define TAG "WeactNhiBoard"

#if !NHI_HW_OLED
// INMP441 + PCM5100A share BCLK/WS: one I2S port, TX and RX at the same rate,
// 32-bit slots (INMP441 needs 64 BCLK per frame). Unlike NoAudioCodecDuplex the
// speaker side sends the mono stream to both slots (PCM5100A L+R outputs).
class NhiAudioCodec : public NoAudioCodec {
public:
    NhiAudioCodec() {
        duplex_ = true;
        input_sample_rate_ = AUDIO_INPUT_SAMPLE_RATE;
        output_sample_rate_ = AUDIO_OUTPUT_SAMPLE_RATE;
        output_volume_ = AUDIO_DEFAULT_OUTPUT_VOLUME;  // NVS value wins if the user changed it

        i2s_chan_config_t chan_cfg = {
            .id = XIAOZHI_I2S_PORT(0),
            .role = I2S_ROLE_MASTER,
            .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
            .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
            .auto_clear_after_cb = true,
            .auto_clear_before_cb = false,
            .intr_priority = 0,
        };
        ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

        i2s_std_config_t std_cfg = {
            .clk_cfg = {
                .sample_rate_hz = (uint32_t)AUDIO_OUTPUT_SAMPLE_RATE,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_MONO,
                .slot_mask = I2S_STD_SLOT_BOTH,  // speaker: same sample on L and R
                .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false,
            },
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,  // PCM5100A uses its internal PLL (MC -> GND)
                .bclk = PIN_I2S_BCLK,
                .ws = PIN_I2S_WS,
                .dout = PIN_I2S_DOUT,
                .din = PIN_I2S_DIN,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
        // Mic: read both slots and use whichever carries the INMP441 (L/R -> GND
        // = left, L/R -> VDD or floating = right), so a wiring change cannot mute it.
        std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
        std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
        ESP_LOGI(TAG, "Full-duplex I2S: BCLK %d, WS %d, DIN %d, DOUT %d @ %d Hz", PIN_I2S_BCLK,
                 PIN_I2S_WS, PIN_I2S_DIN, PIN_I2S_DOUT, AUDIO_OUTPUT_SAMPLE_RATE);
    }

protected:
    int Read(int16_t* dest, int samples) override {
        std::lock_guard<std::mutex> lock(data_if_mutex_);
        stereo_.resize((size_t)samples * 2);
        size_t bytes_read = 0;
        if (i2s_channel_read(rx_handle_, stereo_.data(), stereo_.size() * sizeof(int32_t),
                             &bytes_read, 200) != ESP_OK) {
            return 0;
        }
        int frames = (int)(bytes_read / (2 * sizeof(int32_t)));
        int64_t sum[2] = {0, 0};
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c) {
                int32_t v = stereo_[i * 2 + c] >> 12;  // same gain as NoAudioCodec::Read
                sum[c] += v < 0 ? -v : v;
            }
            int32_t v = stereo_[i * 2 + mic_slot_] >> 12;
            dest[i] = (v > INT16_MAX) ? INT16_MAX : (v < -INT16_MAX) ? -INT16_MAX : (int16_t)v;
        }
        if (frames > 0) {
            for (int c = 0; c < 2; ++c) level_[c] += ((float)sum[c] / frames - level_[c]) * 0.05f;
            // Switch slot only when the other one is clearly louder.
            int other = mic_slot_ ^ 1;
            if (level_[other] > 4.0f * level_[mic_slot_] + 20.0f) {
                mic_slot_ = other;
                ESP_LOGW(TAG, "mic signal found on the %s slot, switching", other ? "RIGHT" : "LEFT");
            }
        }
        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_log_ms_ > 3000) {
            last_log_ms_ = now;
            ESP_LOGI(TAG, "mic level L %d / R %d (using %s)%s", (int)level_[0], (int)level_[1],
                     mic_slot_ ? "R" : "L",
                     (level_[0] < 3.0f && level_[1] < 3.0f) ? "  <- NO SIGNAL: check mic SD/SCK/WS/VDD wiring" : "");
        }
        return frames;
    }

private:
    std::vector<int32_t> stereo_;
    float level_[2] = {0, 0};
    int mic_slot_ = 0;
    int64_t last_log_ms_ = 0;
};
#endif  // !NHI_HW_OLED

class WeactNhiBoard : public WifiBoard {
private:
    Button boot_button_;
    EyesDisplay* display_ = nullptr;
    HeadServos head_;
    NhiBehavior* behavior_ = nullptr;

    static void LogResetReason() {
        const char* name = "unknown";
        switch (esp_reset_reason()) {
            case ESP_RST_POWERON: name = "power-on"; break;
            case ESP_RST_SW: name = "software"; break;
            case ESP_RST_PANIC: name = "panic"; break;
            case ESP_RST_INT_WDT: name = "interrupt watchdog"; break;
            case ESP_RST_TASK_WDT: name = "task watchdog"; break;
            case ESP_RST_WDT: name = "other watchdog"; break;
            case ESP_RST_DEEPSLEEP: name = "deep sleep"; break;
            case ESP_RST_BROWNOUT: name = "BROWNOUT (check USB cable / power supply)"; break;
            case ESP_RST_USB: name = "usb"; break;
            case ESP_RST_EXT: name = "external pin"; break;
            default: break;
        }
        if (esp_reset_reason() == ESP_RST_BROWNOUT) {
            ESP_LOGE(TAG, "Reset reason: %s", name);
        } else {
            ESP_LOGI(TAG, "Reset reason: %s", name);
        }
    }

#if !NHI_SERVOS_ENABLED
    // Servo signal lines LOW: no pulses, servos stay limp and quiet.
    static void ParkServoPins() {
        gpio_config_t io = {};
        io.pin_bit_mask = (1ULL << PIN_SERVO_PAN) | (1ULL << PIN_SERVO_TILT);
        io.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io);
        gpio_set_level(PIN_SERVO_PAN, 0);
        gpio_set_level(PIN_SERVO_TILT, 0);
        ESP_LOGI(TAG, "Head servos disabled (NHI_SERVOS_ENABLED = 0)");
    }
#endif

#if NHI_HW_OLED
    // Opens an I2C bus on sda/scl and looks for an SSD1306 at 0x3C or 0x3D.
    // Returns the address and keeps the bus open, or 0 and deletes the bus.
    static int ProbeOled(gpio_num_t sda, gpio_num_t scl, i2c_master_bus_handle_t* bus) {
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = (i2c_port_t)0;
        bus_config.sda_io_num = sda;
        bus_config.scl_io_num = scl;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = 1;
        if (i2c_new_master_bus(&bus_config, bus) != ESP_OK) return 0;
        for (int addr : {DISPLAY_I2C_ADDR, 0x3D}) {
            if (i2c_master_probe(*bus, addr, 20) == ESP_OK) return addr;
        }
        i2c_del_master_bus(*bus);
        *bus = nullptr;
        return 0;
    }

    // The silkscreen says SDA 41 / SCL 42; also accept a cable with SDA/SCL swapped.
    static int FindOled(gpio_num_t* sda, gpio_num_t* scl, i2c_master_bus_handle_t* bus) {
        *sda = PIN_OLED_SDA;
        *scl = PIN_OLED_SCL;
        int addr = ProbeOled(*sda, *scl, bus);
        if (addr != 0) return addr;
        addr = ProbeOled(PIN_OLED_SCL, PIN_OLED_SDA, bus);
        if (addr != 0) {
            *sda = PIN_OLED_SCL;
            *scl = PIN_OLED_SDA;
            ESP_LOGW(TAG, "OLED answers with SDA/SCL swapped (SDA %d / SCL %d)", *sda, *scl);
        }
        return addr;
    }

    void InitializeDisplay() {
        gpio_num_t sda, scl;
        i2c_master_bus_handle_t bus = nullptr;
        int addr = FindOled(&sda, &scl, &bus);
        if (addr == 0) {
            ESP_LOGE(TAG, "OLED not found at 0x3C/0x3D on SDA %d / SCL %d: no power (battery "
                          "switch?) or cable", PIN_OLED_SDA, PIN_OLED_SCL);
            // Keep a bus on the silkscreen pins so the eyes code still runs (output off).
            ProbeOled(PIN_OLED_SDA, PIN_OLED_SCL, &bus);
            if (bus == nullptr) {
                i2c_master_bus_config_t bus_config = {};
                bus_config.i2c_port = (i2c_port_t)0;
                bus_config.sda_io_num = PIN_OLED_SDA;
                bus_config.scl_io_num = PIN_OLED_SCL;
                bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
                bus_config.glitch_ignore_cnt = 7;
                bus_config.flags.enable_internal_pullup = 1;
                ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));
            }
            addr = DISPLAY_I2C_ADDR;
        }

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_io_i2c_config_t io_config = {};
        io_config.dev_addr = addr;
        io_config.scl_speed_hz = DISPLAY_I2C_HZ;
        io_config.control_phase_bytes = 1;
        io_config.dc_bit_offset = 6;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_config, &panel_io));

        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {};
        ssd1306_config.height = DISPLAY_HEIGHT;
        panel_config.vendor_config = &ssd1306_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io, &panel_config, &panel));

        bool ok = esp_lcd_panel_reset(panel) == ESP_OK && esp_lcd_panel_init(panel) == ESP_OK;
        if (ok) {
            esp_lcd_panel_invert_color(panel, false);
            esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
            esp_lcd_panel_disp_on_off(panel, true);
            ESP_LOGI(TAG, "OLED SSD1306 %dx%d at 0x%02X on SDA %d / SCL %d", DISPLAY_WIDTH,
                     DISPLAY_HEIGHT, addr, sda, scl);
        }

        display_ = new EyesDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        if (!ok) display_->DisableOutput();  // eyes logic keeps running, no I2C spam
    }
#else
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = PIN_TFT_MOSI;
        buscfg.miso_io_num = PIN_TFT_MISO;
        buscfg.sclk_io_num = PIN_TFT_SCLK;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * EYES_PUSH_LINES * sizeof(uint16_t) + 64;
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = PIN_TFT_CS;
        io_config.dc_gpio_num = PIN_TFT_DC;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_HZ;
        io_config.trans_queue_depth = 4;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = PIN_TFT_RST;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new EyesDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
#endif  // NHI_HW_OLED

    void InitializeButtons() {
        // Push-to-talk: a click toggles listening (upstream behaviour).
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    WeactNhiBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        LogResetReason();
#if !NHI_HW_OLED
        InitializeSpi();
#endif
        InitializeDisplay();
        InitializeButtons();
#ifdef CONFIG_NHI_EYES_DEBUG_CYCLE
        display_->StartDebugCycle(2500);  // bench test: cycle moods
#endif
#if NHI_SERVOS_ENABLED
        head_.Begin();  // task only; servos attach after Wi-Fi + audio are up
#else
        ParkServoPins();  // head disabled: Play()/Glance()/AttachSequential() are no-ops
#endif
        behavior_ = new NhiBehavior(display_, &head_);
        behavior_->Start();
    }

    // USB powered desk robot: keep the radio fully awake. Modem sleep between
    // DTIM beacons adds latency/jitter to the voice stream and makes some
    // routers drop the station, which is the main cause of disconnects.
    void SetPowerSaveLevel(PowerSaveLevel level) override {
        (void)level;
        WifiBoard::SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    }

    Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    AudioCodec* GetAudioCodec() override {
#if NHI_HW_OLED
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               PIN_SPK_BCLK, PIN_SPK_LRC, PIN_SPK_DIN, PIN_MIC_SCK,
                                               PIN_MIC_WS, PIN_MIC_SD);
#else
        static NhiAudioCodec audio_codec;
#endif
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }
};

DECLARE_BOARD(WeactNhiBoard);
