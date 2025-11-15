#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ina219.h>
#include <string.h>

<<<<<<< HEAD:main/main.c
#include "battery_cc.h"
#include "driver/gpio.h"

=======
>>>>>>> 397dd728cb6399f31313f088c21a00bdfe8c68ca:ina_example/main/main.c

#include <esp_log.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include "onewire_bus.h"
#include "ds18b20.h"

<<<<<<< HEAD:main/main.c
#define RELAY1   27  // xả
#define RELAY2   26  // sạc
=======
#define RELAY1   27
#define RELAY2   26
>>>>>>> 397dd728cb6399f31313f088c21a00bdfe8c68ca:ina_example/main/main.c
#define I2C_PORT 0
#define I2C_ADDR CONFIG_EXAMPLE_I2C_ADDR

#define EXAMPLE_ONEWIRE_BUS_GPIO    18
#define EXAMPLE_ONEWIRE_MAX_DS18B20 1

const static char *TAG = "INA219";
const static char *TAG1 = "DS18B20";

<<<<<<< HEAD:main/main.c

=======
/*
void relay_task(void *pvParameters)
{
    // Ban đầu để cả hai chân relay ở chế độ input (trở kháng cao)
    gpio_set_direction(RELAY1, GPIO_MODE_INPUT);
    gpio_set_direction(RELAY2, GPIO_MODE_INPUT);
    while (1)
    {
        // Bật relay 1 (RELAY1 output low), RELAY2 input (trở kháng cao)
        gpio_set_direction(RELAY1, GPIO_MODE_OUTPUT);
        gpio_set_level(RELAY1, 0);
        gpio_set_direction(RELAY2, GPIO_MODE_INPUT);
        ESP_LOGI(TAG, "Relay 1 ON (active low), Relay 2 High-Z");
        vTaskDelay(pdMS_TO_TICKS(20000));

        // Tắt cả hai relay (đều input - trở kháng cao)
        gpio_set_direction(RELAY1, GPIO_MODE_INPUT);
        gpio_set_direction(RELAY2, GPIO_MODE_INPUT);
        ESP_LOGI(TAG, "Relay 1 OFF, Relay 2 OFF (High-Z)");
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Bật relay 2 (RELAY2 output low), RELAY1 input (trở kháng cao)
        gpio_set_direction(RELAY1, GPIO_MODE_INPUT);
        gpio_set_direction(RELAY2, GPIO_MODE_OUTPUT);
        gpio_set_level(RELAY2, 0);
        ESP_LOGI(TAG, "Relay 1 High-Z, Relay 2 ON (active low)");
        vTaskDelay(pdMS_TO_TICKS(20000));

        // Tắt cả hai relay (đều input - trở kháng cao)
        gpio_set_direction(RELAY1, GPIO_MODE_INPUT);
        gpio_set_direction(RELAY2, GPIO_MODE_INPUT);
        ESP_LOGI(TAG, "Relay 1 OFF, Relay 2 OFF (High-Z)");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
*/
>>>>>>> 397dd728cb6399f31313f088c21a00bdfe8c68ca:ina_example/main/main.c
void ina219_task(void *pvParameters)
{
    ina219_t dev;
    memset(&dev, 0, sizeof(ina219_t));
    assert(CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM > 0);
    ESP_ERROR_CHECK(ina219_init_desc(&dev, I2C_ADDR, I2C_PORT, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_LOGI(TAG, "Initializing INA219");
    ESP_ERROR_CHECK(ina219_init(&dev));
    ESP_LOGI(TAG, "Configuring INA219");
    ESP_ERROR_CHECK(ina219_configure(&dev, INA219_BUS_RANGE_16V, INA219_GAIN_0_125,
                                     INA219_RES_12BIT_1S, INA219_RES_12BIT_1S, INA219_MODE_CONT_SHUNT_BUS));
       ESP_LOGI(TAG, "Calibrating INA219");
    ESP_ERROR_CHECK(ina219_calibrate(&dev, (float)CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM / 1000.0f));
<<<<<<< HEAD:main/main.c
    float bus_voltage, shunt_voltage, current;
    battery_soc_t soc;
    // Khởi tạo SOC với dung lượng danh định 1080Ah
    ESP_ERROR_CHECK(bsoc_init(&soc, 1.080));

    // Cấu hình chân relay1
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY1),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY1, 1);
=======
    float bus_voltage, shunt_voltage, current, power;
>>>>>>> 397dd728cb6399f31313f088c21a00bdfe8c68ca:ina_example/main/main.c
    /*
    //--------------------ds18b20----------------//

    // install 1-wire bus
    onewire_bus_handle_t bus = NULL;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
        .flags = {
            .en_pull_up = true, // enable the internal pull-up resistor in case the external device didn't have one
        }
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
    ESP_LOGI(TAG1, "1-Wire bus installed on GPIO%d", EXAMPLE_ONEWIRE_BUS_GPIO);

    int ds18b20_device_num = 0;
    ds18b20_device_handle_t ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t search_result = ESP_OK;

    // create 1-wire device iterator, which is used for device search
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(TAG1, "Device iterator created, start searching...");
    do {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_OK) { // found a new device, let's check if we can upgrade it to a DS18B20
            ds18b20_config_t ds_cfg = {};
            onewire_device_address_t address;
            // check if the device is a DS18B20, if so, return the ds18b20 handle
            if (ds18b20_new_device_from_enumeration(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]) == ESP_OK) {
                ds18b20_get_device_address(ds18b20s[ds18b20_device_num], &address);
                ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, address);
                ds18b20_device_num++;
                if (ds18b20_device_num >= EXAMPLE_ONEWIRE_MAX_DS18B20) {
                    ESP_LOGI(TAG1, "Max DS18B20 number reached, stop searching...");
                    break;
                }
            } else {
                ESP_LOGI(TAG1, "Found an unknown device, address: %016llX", next_onewire_device.address);
            }
        }
    } while (search_result != ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    ESP_LOGI(TAG1, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);

    float temperature;
        */
    // Đo điện áp ban đầu khi mở log
    ESP_ERROR_CHECK(ina219_get_bus_voltage(&dev, &bus_voltage));
    ESP_LOGI(TAG, "Initial OCV voltage: %.04f V", bus_voltage);

    vTaskDelay(pdMS_TO_TICKS(5000));
    // Đóng relay1 (active low)
    gpio_set_level(RELAY1, 0);
    ESP_LOGI(TAG, "bat dau xa");

    while (1)
    {
        ESP_ERROR_CHECK(ina219_get_bus_voltage(&dev, &bus_voltage));
        ESP_ERROR_CHECK(ina219_get_shunt_voltage(&dev, &shunt_voltage));
        ESP_ERROR_CHECK(ina219_get_current(&dev, &current));
<<<<<<< HEAD:main/main.c

        // Cập nhật SOC bằng coulomb counting
        bsoc_feed_sample(&soc, bus_voltage, current);
        float soc_percent = bsoc_get_soc_percent(&soc);

=======
        ESP_ERROR_CHECK(ina219_get_power(&dev, &power));
>>>>>>> 397dd728cb6399f31313f088c21a00bdfe8c68ca:ina_example/main/main.c
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm tm_info;
        localtime_r(&tv.tv_sec, &tm_info);
<<<<<<< HEAD:main/main.c
        ESP_LOGI(TAG, "%02d:%02d:%02d VBUS: %.04f V, VSHUNT: %.04f mV, IBUS: %.04f mA, SOC: %.2f%%",
            tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
            bus_voltage, shunt_voltage * 1000, current * 1000, soc_percent);

=======
        ESP_LOGI(TAG, "%02d:%02d:%02d VBUS: %.04f V, VSHUNT: %.04f mV, IBUS: %.04f mA, PBUS: %.04f mW",
            tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
            bus_voltage, shunt_voltage * 1000, current * 1000, power * 1000);
        printf("%02d:%02d:%02d,%.04f,%.04f,%.04f,%.04f\n",
            tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
            bus_voltage, shunt_voltage * 1000, current * 1000, power * 1000);
>>>>>>> 397dd728cb6399f31313f088c21a00bdfe8c68ca:ina_example/main/main.c
        fflush(stdout);
/*
        ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion_for_all(bus));
        for (int i = 0; i < ds18b20_device_num; i ++) {
            ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temperature));
            ESP_LOGI(TAG1, "temperature read from DS18B20[%d]: %.2fC", i, temperature);
        }
*/
        if (soc_percent <= 0.9f) {
            ESP_LOGI(TAG, "SOC decrease 10%, stopping log.");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // (Tùy chọn) Ngắt relay1 nếu muốn
    gpio_set_level(RELAY1, 1);

}

void app_main()
{
    ESP_ERROR_CHECK(i2cdev_init());
    // relay_task chạy trên core 0, ina219_task chạy trên core 1
<<<<<<< HEAD:main/main.c
=======
   // xTaskCreate(relay_task, "relay_task", configMINIMAL_STACK_SIZE * 2, NULL, 5, NULL);
>>>>>>> 397dd728cb6399f31313f088c21a00bdfe8c68ca:ina_example/main/main.c
    xTaskCreate(ina219_task, "ina219_task", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}
