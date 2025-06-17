#include <string>
#include <memory>
#include <cstring>
#include <ctime>
#include "error_monitor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_crc.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"

static const char* TAG = "ErrorMonitor";

#define ERROR_MONITOR_URL "http://appmonitor.gizwits.com/api/%s/store/?sentry_key=%s&sentry_version=7"
#define SENTRY_KEY "196c7169992d49ee951a09576aaa050a"
#define PROJECT_ID "88"
#define UPLOAD_TASK_STACK_SIZE 8192

std::string device_id;  // MAC address as device ID

// Get MAC address as string
static void get_mac_str(char* mac_str, bool use_colon) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    
    if (use_colon) {
        snprintf(mac_str, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(mac_str, 13, "%02x%02x%02x%02x%02x%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

// Task parameters for stack upload
struct UploadTaskParams {
    std::string event_id;
    std::string stack;
};

// Task parameters for error reporting
struct ErrorReportParams {
    error_type_t type;
    error_level_t level;
    std::string message;
    std::string stack;
};

// Helper function to get error type string
static const char* get_error_type_str(error_type_t type) {
    switch(type) {
        case ERROR_TYPE_MEMORY: return "MemoryError";
        case ERROR_TYPE_NETWORK: return "NetworkError";
        case ERROR_TYPE_BATTERY: return "BatteryError";
        case ERROR_TYPE_AUDIO: return "AudioError";
        case ERROR_TYPE_SYSTEM: return "SystemError";
        default: return "UnknownError";
    }
}

// Helper function to get error level string
static const char* get_error_level_str(error_level_t level) {
    switch(level) {
        case ERROR_LEVEL_INFO: return "info";
        case ERROR_LEVEL_WARNING: return "warning";
        case ERROR_LEVEL_ERROR: return "error";
        case ERROR_LEVEL_FATAL: return "fatal";
        default: return "error";
    }
}

// Generate event ID
static std::string generate_event_id() {
    uint64_t timestamp = esp_timer_get_time();
    uint8_t random_data[16];
    esp_fill_random(random_data, sizeof(random_data));
    
    char event_id[33];
    snprintf(event_id, sizeof(event_id), 
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             random_data[0], random_data[1], random_data[2], random_data[3],
             random_data[4], random_data[5], random_data[6], random_data[7],
             random_data[8], random_data[9], random_data[10], random_data[11],
             random_data[12], random_data[13], random_data[14], random_data[15]);
    
    return std::string(event_id);
}

// Stack upload task implementation
static void upload_stack_task(void *pvParameters) {
    auto params = std::unique_ptr<UploadTaskParams>(static_cast<UploadTaskParams*>(pvParameters));
    
    if (params && !params->stack.empty()) {
        ESP_LOGE(TAG, "Stack upload task: stack length = %zu bytes", params->stack.length());
        ESP_LOGE(TAG, "Starting stack upload task for event %s", params->event_id.c_str());
        upload_stack_as_attachment(params->event_id.c_str(), params->stack.c_str());
    } else {
        ESP_LOGE(TAG, "Invalid parameters for stack upload task");
    }
    
    vTaskDelete(NULL);
}

// Actual error reporting implementation
static esp_err_t _report_error(error_type_t type, error_level_t level, 
                             const char* message, const char* stack) {
    ESP_LOGI(TAG, "Starting error report for type: %s, level: %s", 
             get_error_type_str(type), get_error_level_str(level));
    
    // Create JSON object
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), cJSON_Delete);
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }
    
    // Create exception object
    cJSON *exception = cJSON_CreateObject();
    cJSON *values = cJSON_CreateArray();
    cJSON *value = cJSON_CreateObject();
    
    cJSON_AddStringToObject(value, "type", get_error_type_str(type));
    cJSON_AddStringToObject(value, "value", message ? message : "Unknown error");
    
    // Add empty stacktrace object
    cJSON *stacktrace = cJSON_CreateObject();
    cJSON *frames = cJSON_CreateArray();
    cJSON_AddItemToObject(stacktrace, "frames", frames);
    cJSON_AddItemToObject(value, "stacktrace", stacktrace);
    
    // Add mechanism
    cJSON *mechanism = cJSON_CreateObject();
    cJSON_AddStringToObject(mechanism, "type", "generic");
    cJSON_AddBoolToObject(mechanism, "handled", true);
    cJSON_AddItemToObject(value, "mechanism", mechanism);
    
    cJSON_AddItemToArray(values, value);
    cJSON_AddItemToObject(exception, "values", values);
    cJSON_AddItemToObject(root.get(), "exception", exception);
    
    // Add basic information
    std::string event_id = generate_event_id();
    cJSON_AddStringToObject(root.get(), "event_id", event_id.c_str());
    cJSON_AddStringToObject(root.get(), "level", get_error_level_str(level));
    cJSON_AddStringToObject(root.get(), "platform", "c");
    cJSON_AddNumberToObject(root.get(), "timestamp", (double)esp_timer_get_time() / 1000000.0);
    
    // Add SDK information
    cJSON *sdk = cJSON_CreateObject();
    cJSON_AddStringToObject(sdk, "name", "esp32s3");
    cJSON_AddStringToObject(sdk, "version", "4bo");
    cJSON_AddItemToObject(root.get(), "sdk", sdk);
    
    // Add environment
    cJSON_AddStringToObject(root.get(), "environment", "production");
    
    // Add contexts
    cJSON *contexts = cJSON_CreateObject();
    cJSON *device = cJSON_CreateObject();
    cJSON *os = cJSON_CreateObject();
    cJSON *extra = cJSON_CreateObject();
    
    cJSON_AddStringToObject(os, "name", "esp32s3");
    cJSON_AddStringToObject(os, "version", "4bo");
    cJSON_AddStringToObject(extra, "mac", device_id.c_str());
    cJSON_AddStringToObject(device, "mac", device_id.c_str());
    
    cJSON_AddItemToObject(contexts, "device", device);
    cJSON_AddItemToObject(contexts, "os", os);
    cJSON_AddItemToObject(contexts, "extra", extra);
    cJSON_AddItemToObject(root.get(), "contexts", contexts);
    
    // Add extra information
    cJSON *extra_root = cJSON_CreateObject();
    if (message) {
        cJSON_AddStringToObject(extra_root, "message", message);
        if (strstr(message, "0x") != NULL) {
            cJSON_AddStringToObject(extra_root, "error_code", message);
        }
    }
    cJSON_AddItemToObject(root.get(), "extra", extra_root);
    
    // Convert to string
    char *post_data = cJSON_PrintUnformatted(root.get());
    if (!post_data) {
        ESP_LOGE(TAG, "Failed to convert JSON to string");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Generated JSON payload: %s", post_data);
    
    // Build URL
    char url[256];
    snprintf(url, sizeof(url), ERROR_MONITOR_URL, PROJECT_ID, SENTRY_KEY);
    ESP_LOGI(TAG, "Target URL: %s", url);
    
    // Get HTTP client from board
    auto& board = Board::GetInstance();
    auto http = board.CreateHttp();

    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        free(post_data);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "HTTP client created successfully");
    
    // Configure HTTP request
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("User-Agent", "ESP32/1.0");
    ESP_LOGI(TAG, "HTTP headers set");
    
    // Execute request
    ESP_LOGI(TAG, "Attempting to open HTTP connection...");

    std::string content(post_data, strlen(post_data));
    http->SetContent(std::move(content));

    esp_err_t err = http->Open("POST", url);
    
    free(post_data);  // Free the JSON string
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s (error code: %d)", esp_err_to_name(err), err);
        delete http;
        return err;
    }
    
    ESP_LOGI(TAG, "HTTP connection opened successfully");
    
    // Get response
    int status_code = http->GetStatusCode();
    ESP_LOGI(TAG, "Received HTTP response with status code: %d", status_code);
    
    // Read response content
    std::string response = http->ReadAll();
    if (!response.empty()) {
        ESP_LOGI(TAG, "Response body (%zu bytes): %s", response.length(), response.c_str());
    } else {
        ESP_LOGW(TAG, "Empty response body received");
    }
    
    http->Close();
    delete http;
    ESP_LOGI(TAG, "HTTP connection closed");
    
    // Handle stack trace if provided
    if (stack && strlen(stack) > 0) {
        ESP_LOGI(TAG, "Stack trace provided, length: %zu bytes", strlen(stack));
        ESP_LOGI(TAG, "Stack trace content: %s", stack);
        
        // Create task parameters for stack upload
        auto params = std::make_unique<UploadTaskParams>();
        params->event_id = event_id;
        params->stack = stack;
        
        // Create task for stack upload
        BaseType_t result = xTaskCreate(
            upload_stack_task,
            "stack_upload",
            UPLOAD_TASK_STACK_SIZE,
            params.release(),
            tskIDLE_PRIORITY + 1,
            NULL
        );
        
        if (result != pdPASS) {
            ESP_LOGE(TAG, "Failed to create stack upload task");
        } else {
            ESP_LOGI(TAG, "Stack upload task created successfully");
        }
    }
    
    esp_err_t final_result = (status_code >= 200 && status_code < 300) ? ESP_OK : ESP_FAIL;
    ESP_LOGI(TAG, "Error report completed with result: %s", esp_err_to_name(final_result));
    return final_result;
}

// Error report task implementation
static void report_error_task(void *pvParameters) {
    auto params = std::unique_ptr<ErrorReportParams>(static_cast<ErrorReportParams*>(pvParameters));
    
    if (params) {
        _report_error(params->type, params->level, 
                     params->message.c_str(), params->stack.c_str());
    } else {
        ESP_LOGE(TAG, "Invalid parameters for error report task");
    }
    
    vTaskDelete(NULL);
}

esp_err_t error_monitor_init(void) {
    char mac_str[13];
    get_mac_str(mac_str, true);
    device_id = mac_str;
    return ESP_OK;
}

esp_err_t report_error(error_type_t type, error_level_t level, 
                      const char* message, const char* stack) {
    // auto params = std::make_unique<ErrorReportParams>();
    // params->type = type;
    // params->level = level;
    // if (message) params->message = message;
    // if (stack) params->stack = stack;
    
    // BaseType_t result = xTaskCreate(
    //     report_error_task,
    //     "report_error",
    //     8192 / 2,
    //     params.release(),
    //     5,
    //     NULL
    // );
    
    // if (result != pdPASS) {
    //     ESP_LOGE(TAG, "Failed to create error report task");
    //     return ESP_FAIL;
    // }
    
    // ESP_LOGI(TAG, "Error report task created successfully");
    return ESP_OK;
}

esp_err_t upload_stack_as_attachment(const char* event_id, const char* stack) {
    if (!stack || !event_id) {
        ESP_LOGE(TAG, "Stack or event_id is NULL, skipping attachment upload");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting stack trace upload for event %s", event_id);
    
    size_t stack_len = strlen(stack);
    if (stack_len == 0) {
        ESP_LOGE(TAG, "Stack trace is empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (stack_len >= 65536 * 2) {
        ESP_LOGW(TAG, "Stack trace too long (%zu bytes), truncating to 128KB", stack_len);
        stack_len = 65536 * 2 - 1;
    }
    
    // Build upload URL
    char upload_url[256];
    snprintf(upload_url, sizeof(upload_url), 
             "http://appmonitor.gizwits.com/api/%s/events/%s/attachments/?sentry_key=%s&sentry_version=7", 
             PROJECT_ID, event_id, SENTRY_KEY);
    ESP_LOGI(TAG, "Upload URL: %s", upload_url);
    
    const char *boundary = "boundary123456789";
    std::string content_type = "multipart/form-data; boundary=" + std::string(boundary);
    
    // Create form data
    std::string form_data = 
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"name\"\r\n"
        "\r\n"
        "stack_trace.txt\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"attachment_type\"\r\n"
        "\r\n"
        "event.attachment\r\n"
        "--" + std::string(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"attachment\"; filename=\"stack_trace.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n" +
        std::string(stack, stack_len) + "\r\n"
        "--" + std::string(boundary) + "--\r\n";
    
    ESP_LOGI(TAG, "Form data created, total size: %zu bytes", form_data.length());
    
    // Get HTTP client from board
    auto& board = Board::GetInstance();
    auto http = board.CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client for stack upload");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "HTTP client created for stack upload");
    
    // Configure HTTP request
    http->SetHeader("Content-Type", content_type.c_str());
    http->SetHeader("Connection", "keep-alive");
    http->SetHeader("User-Agent", "ESP32/1.0");
    ESP_LOGI(TAG, "HTTP headers set for stack upload");
    
    // Execute request
    ESP_LOGI(TAG, "Attempting to open HTTP connection for stack upload...");
    std::string content(form_data.c_str(), form_data.length());
    http->SetContent(std::move(content));
    esp_err_t err = http->Open("POST", upload_url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed for stack upload: %s (error code: %d)", 
                 esp_err_to_name(err), err);
        delete http;
        return err;
    }
    
    ESP_LOGI(TAG, "HTTP connection opened successfully for stack upload");
    
    // Get response
    int status_code = http->GetStatusCode();
    ESP_LOGI(TAG, "Stack upload response status code: %d", status_code);
    
    // Read response content
    std::string response = http->ReadAll();
    if (!response.empty()) {
        ESP_LOGI(TAG, "Stack upload response (%zu bytes): %s", 
                 response.length(), response.c_str());
    } else {
        ESP_LOGW(TAG, "Empty response body received for stack upload");
    }
    
    http->Close();
    delete http;
    ESP_LOGI(TAG, "HTTP connection closed for stack upload");
    
    esp_err_t final_result = (status_code >= 200 && status_code < 300) ? ESP_OK : ESP_FAIL;
    ESP_LOGI(TAG, "Stack upload completed with result: %s", esp_err_to_name(final_result));
    return final_result;
} 