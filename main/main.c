#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "esp_timer.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const char *TAG = "midi-controller";

// ADC Configuration
#define ADC_UNIT        ADC_UNIT_1
#define ADC_ATTEN       ADC_ATTEN_DB_11
#define ADC_CHANNEL1    ADC_CHANNEL_9  // GPIO10
#define ADC_CHANNEL2    ADC_CHANNEL_8  // GPIO2
#define ADC_CHANNEL3    ADC_CHANNEL_2  // GPIO3

// MIDI Configuration
#define MIDI_CC_CHANNEL 0  // MIDI channel 1
#define MIDI_CC1        1  // Control Change number for first pot
#define MIDI_CC2        2  // Control Change number for second pot
#define MIDI_CC3        3  // Control Change number for third pot
#define CC_MESSAGE      0xB0

// Filter Configuration
#define ALPHA           0.25f  // Smoothing factor (0-1), lower = more smoothing

// Structure to hold analog input state
typedef struct {
    adc_channel_t channel;
    float filtered_value;
    uint8_t last_midi_value;
    uint8_t cc_number;
} analog_input_t;

// Global state for our three inputs
static analog_input_t analog_inputs[3] = {
    {ADC_CHANNEL1, 0.0f, 0, MIDI_CC1},
    {ADC_CHANNEL2, 0.0f, 0, MIDI_CC2},
    {ADC_CHANNEL3, 0.0f, 0, MIDI_CC3}
};

/** TinyUSB descriptors **/

enum interface_count {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_COUNT
};

enum usb_endpoints {
    EP_EMPTY = 0,
    EPNUM_MIDI,
};

#define TUSB_DESCRIPTOR_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_MIDI * TUD_MIDI_DESC_LEN)

static const char* s_str_desc[5] = {
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "ESP32 MIDI Controller", // 2: Product
    "123456789",           // 3: Serials
    "Ligma Controller"      // 4: MIDI
};

static const uint8_t s_midi_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 64),
};

// Initialize ADC
static void init_adc(void) {
    esp_adc_cal_characteristics_t adc_chars;
    
    adc1_config_width(ADC_WIDTH_BIT_12);
    
    // Configure each ADC channel
    for (int i = 0; i < 3; i++) {
        adc1_config_channel_atten(analog_inputs[i].channel, ADC_ATTEN);
    }
    
    // Characterize ADC
    esp_adc_cal_characterize(ADC_UNIT, ADC_ATTEN, ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

// Send MIDI CC message
static void send_midi_cc(uint8_t cc_number, uint8_t value) {
    if (tud_midi_mounted()) {
        uint8_t msg[3] = {CC_MESSAGE | MIDI_CC_CHANNEL, cc_number, value};
        tud_midi_stream_write(0, msg, 3);
        ESP_LOGD(TAG, "Sent CC %d: %d", cc_number, value);
    }
}

// Read and process analog inputs
static void process_analog_inputs(void) {
    for (int i = 0; i < 3; i++) {
        // Read raw ADC value
        int raw_value = adc1_get_raw(analog_inputs[i].channel);
        
        // Apply exponential moving average filter
        analog_inputs[i].filtered_value = (ALPHA * raw_value) + 
                                        ((1.0f - ALPHA) * analog_inputs[i].filtered_value);
        
        // Convert to MIDI range (0-127)
        uint8_t midi_value = (uint8_t)((analog_inputs[i].filtered_value / 4095.0f) * 127.0f);
        
        // Only send if value has changed
        if (midi_value != analog_inputs[i].last_midi_value) {
            send_midi_cc(analog_inputs[i].cc_number, midi_value);
            analog_inputs[i].last_midi_value = midi_value;
        }
    }
}

static void midi_controller_task(void *arg) {
    const TickType_t delay = pdMS_TO_TICKS(10); // 10ms sampling rate
    
    for (;;) {
        process_analog_inputs();
        vTaskDelay(delay);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing MIDI Controller");

    // Initialize ADC
    init_adc();

    // Initialize USB MIDI
    tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = s_str_desc,
        .string_descriptor_count = sizeof(s_str_desc) / sizeof(s_str_desc[0]),
        .external_phy = false,
        .configuration_descriptor = s_midi_cfg_desc,
    };
    
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB MIDI initialized");

    // Create the MIDI controller task
    xTaskCreate(midi_controller_task, "midi_controller", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "MIDI controller task started");
}