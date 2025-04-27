#include "iot/thing.h"
#include "board.h"
#include "display/lcd_display.h"
#include "settings.h"

#include <esp_log.h>
#include <string>

#define TAG "Screen"

namespace iot {

// 这里仅定义 Screen 的属性和方法，不包含具体的实现
class Screen : public Thing {
public:
    Screen() : Thing("Screen", "这是一个屏幕，可设置主题和亮度") {
        // 定义设备的属性
        properties_.AddStringProperty("theme", "主题", [this]() -> std::string {
            #if CONFIG_IDF_TARGET_ESP32C2
            return "light";  // ESP32C2 没有屏幕，返回默认主题
            #else
            auto theme = Board::GetInstance().GetDisplay()->GetTheme();
            return theme;
            #endif
        });

        properties_.AddNumberProperty("brightness", "当前亮度百分比", [this]() -> int {
            #if CONFIG_IDF_TARGET_ESP32C2
            return 100;  // ESP32C2 没有屏幕，返回默认亮度
            #else
            auto backlight = Board::GetInstance().GetBacklight();
            return backlight ? backlight->brightness() : 100;
            #endif
        });

        // 定义设备可以被远程执行的指令
        methods_.AddMethod("SetTheme", "设置屏幕主题", ParameterList({
            Parameter("theme_name", "主题模式, light 或 dark", kValueTypeString, true)
        }), [this](const ParameterList& parameters) {
            #if !CONFIG_IDF_TARGET_ESP32C2
            std::string theme_name = static_cast<std::string>(parameters["theme_name"].string());
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                display->SetTheme(theme_name);
            }
            #endif
        });
        
        methods_.AddMethod("SetBrightness", "设置亮度", ParameterList({
            Parameter("brightness", "0到100之间的整数", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            #if !CONFIG_IDF_TARGET_ESP32C2
            uint8_t brightness = static_cast<uint8_t>(parameters["brightness"].number());
            auto backlight = Board::GetInstance().GetBacklight();
            if (backlight) {
                backlight->SetBrightness(brightness, true);
            }
            #endif
        });
    }
};

} // namespace iot

DECLARE_THING(Screen);