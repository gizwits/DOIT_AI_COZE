#include "auth.h"

std::string Auth::getAuthKey() {
    return "0e13741451bf42dca84fa22a47f355ef";
}

std::string Auth::getDeviceId() {
    return "n64901bb";
}

std::string Auth::getProductKey() {
    return CONFIG_PRODUCT_KEY;
}

std::string Auth::getProductSecret() {
    return CONFIG_PRODUCT_SECRET;
}
