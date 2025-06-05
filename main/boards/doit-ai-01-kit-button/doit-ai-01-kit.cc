#include "wifi_board.h"
#include "audio_codecs/vb6824_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/circular_strip.h"
#include "led/gpio_led.h"
#include "led/single_led.h"
#include "iot/thing_manager.h"
#include <esp_sleep.h>
#include "power_save_timer.h"
#include <driver/rtc_io.h>
#include "driver/gpio.h"
#include <wifi_station.h>
#include <esp_log.h>
#include "assets/lang_config.h"

#include <esp_lcd_panel_vendor.h>
#include <driver/spi_common.h>

#define TAG "CustomBoard"

class CustomBoard : public WifiBoard {
private:
    Button boot_button_;
    Button rec_button_;
    PowerSaveTimer* power_save_timer_;
    VbAduioCodec audio_codec;
    bool sleep_flag_ = false;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60 * 1, 60 * 2);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
        });
        power_save_timer_->OnExitSleepMode([this]() {
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            run_sleep_mode(false);
        });
        power_save_timer_->SetEnabled(true);
    }

    void run_sleep_mode(bool need_delay = true){
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateIdle);
        application.PlaySound(Lang::Sounds::P3_SLEEP);
        if(need_delay){
            vTaskDelay(pdMS_TO_TICKS(3000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        // 配置唤醒源
        esp_deep_sleep_enable_gpio_wakeup(1ULL << BOOT_BUTTON_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
        esp_deep_sleep_start();
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
#ifdef CONFIG_UES_CHAT_MODE_BUTTON
            ESP_LOGI(TAG, "Button chat mode");
#else
            auto &app = Application::GetInstance();
            app.ToggleChatState();
#endif
        });
        boot_button_.OnPressUp([this]() {
            if(sleep_flag_){
                run_sleep_mode(false);
            }
        });
        boot_button_.OnPressRepeat([this](uint16_t count) {
            if(count >= 3){
                ResetWifiConfiguration();
            }
        });
        rec_button_.OnPressUp([this]() {
            ESP_LOGI(TAG, "Stop listening");
            Application::GetInstance().StopListening();
        });
        rec_button_.OnPressDown([this]() {
            power_save_timer_->WakeUp();
            Application::GetInstance().StartListening();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
    }

public:
    CustomBoard() : boot_button_(BOOT_BUTTON_GPIO), rec_button_(BUILTIN_REC_BUTTON_GPIO), audio_codec(CODEC_TX_GPIO, CODEC_RX_GPIO){      
        InitializePowerSaveTimer();       
        InitializeButtons();
        InitializeIot();

        audio_codec.OnWakeUp([this](const std::string& command) {
            if (command == "你好小智" || command.find("小云") != std::string::npos){
            // if (command.find("小智") != std::string::npos){
                if(Application::GetInstance().GetDeviceState() != kDeviceStateListening){
                    Application::GetInstance().WakeWordInvoke("你好小智");
                }
            }else if (command == "开始配网"){
                ResetWifiConfiguration();
            }
        });
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        return &audio_codec;
    }
};

DECLARE_BOARD(CustomBoard);
