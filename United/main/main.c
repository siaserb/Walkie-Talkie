#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "math.h"
#include "mbedtls/aes.h"

//#define IS_SERVER
#define IS_CLIENT

#define I2S_NUM_TX 0
#define I2S_NUM_RX 1

#define I2S_WS_TX  12
#define I2S_SCK_TX 13
#define I2S_DATA_OUT_TX  15
#define I2S_WS_RX 25
#define I2S_SCL_RX 26
#define I2S_SD_RX 27
#define BUTTON_GPIO GPIO_NUM_35

#define EXAMPLE_ESP_WIFI_SSID "esp32_ap"
#define EXAMPLE_ESP_WIFI_PASS "password"
#define PORT 1234
#define CLIENT_IP_ADDR "192.168.4.2"
#define SERVER_IP_ADDR "192.168.4.1"
#define EXAMPLE_BUFF_SIZE 1024

#define SAMPLE_RATE 44100 // 44100 also ok

static i2s_chan_handle_t    rx_chan;
static i2s_chan_handle_t    tx_chan;

#define AES_KEY_SIZE 16

static uint8_t aes_key[AES_KEY_SIZE] = {
    0x3d, 0xf2, 0x67, 0xf0, 0x34, 0xa9, 0xbc, 0x0b, 
    0x8e, 0xac, 0xe5, 0x8f, 0x12, 0x3c, 0x56, 0x78
};
#define AES_BLOCK_SIZE 16

static const char *TAG = "UDP_UNITED";
static bool got_ip = false;
volatile bool transmit_data = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{   
#ifdef IS_SERVER
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        ESP_LOGI(TAG, "Station connected");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        ESP_LOGI(TAG, "Station disconnected");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) 
    {
        ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
        ESP_LOGI(TAG, "Assigned IP to station: " IPSTR, IP2STR(&event->ip));

        esp_netif_ip_info_t ip_info;
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif == NULL) 
        {
            ESP_LOGE(TAG, "Failed to get AP interface handle");
        } 
        else 
        {
            if (esp_netif_get_ip_info(ap_netif, &ip_info) != ESP_OK) 
            {
                ESP_LOGE(TAG, "Failed to get IP info for AP interface");
            } 
            else 
            {
                ESP_LOGI(TAG, "Current AP IP address: " IPSTR, IP2STR(&ip_info.ip));
            }
        }
    }
#else
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
        got_ip = false;
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        got_ip = true;
    }
#endif
}

void wifi_init(void)
{
    // Initialize the ESP32 WiFi stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize the WiFi library with the default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
#ifdef IS_SERVER
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = 
    {
        .ap = 
        {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) 
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Set the WiFi mode to AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // Set the WiFi configuration
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // Start the WiFi interface
    ESP_ERROR_CHECK(esp_wifi_start());

    // Configure the IP Address for the AP interface
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
#else
    // Create a new STA network interface instance
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = 
    {
        .sta = 
        {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };

    // Set the WiFi mode to STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Set the WiFi configuration
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    // Start the WiFi interface
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start the DHCP client
    ESP_ERROR_CHECK(esp_netif_dhcpc_start(sta_netif));

    ESP_LOGI(TAG, "wifi_init_sta finished.");
#endif
}

void button_task(void* arg)
{
    esp_rom_gpio_pad_select_gpio(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    int last_state = 1;
    while(1) 
    {
        int state = gpio_get_level(BUTTON_GPIO);
        if(state != last_state) 
        {
            last_state = state;
            if(state == 0) 
            {
                ESP_LOGI(TAG, "Button Pressed");
                 transmit_data = true; 
            } 
            else 
            {
                ESP_LOGI(TAG, "Button Released");
               transmit_data = false; 
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Функція підсилення сигналу
void amplify_signal(int16_t *buffer, size_t length, float gain) {
    for (size_t i = 0; i < length; i++) {
        buffer[i] = (int16_t) fminf(fmaxf(buffer[i] * gain, -32768.0f), 32767.0f);
    }
}

// Функція фільтрації сигналу (наприклад, простий високопропускний фільтр)
void high_pass_filter(int16_t *buffer, size_t length, float alpha) {
    int16_t prev = buffer[0];
    for (size_t i = 1; i < length; i++) {
        int16_t current = buffer[i];
        buffer[i] = (int16_t) (alpha * (buffer[i] - prev) + prev);
        prev = current;
    }
}

void udp_send_task(void *pvParameters)
{
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    uint8_t *r_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
    assert(r_buf); // Check if r_buf allocation success
    size_t r_bytes = 0;

#ifdef IS_SERVER
    dest_addr.sin_addr.s_addr = inet_addr(CLIENT_IP_ADDR);
#else
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDR);
#endif

    while (1) {
        if (transmit_data) {
            if (i2s_channel_read(rx_chan, r_buf, EXAMPLE_BUFF_SIZE, &r_bytes, 1000) == ESP_OK) {
                // Підсилюємо сигнал
                amplify_signal((int16_t *)r_buf, r_bytes / 2, 10.0f); // Підсилюємо в 10 разів

                // Фільтруємо сигнал
                //high_pass_filter((int16_t *)r_buf, r_bytes / 2, 0.9f);

                int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
                int err = sendto(sock, r_buf, r_bytes, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                close(sock);
            }
        } 
        vTaskDelay(10 / portTICK_PERIOD_MS);        // Для запобігання watch dog
    }


    free(r_buf);
}

// void udp_receive_task(void *pvParameters)
// {
//     int addr_family = AF_INET;
//     int ip_protocol = IPPROTO_IP;
//     struct sockaddr_in source_addr;
//     source_addr.sin_family = AF_INET;
//     source_addr.sin_port = htons(PORT);
//     source_addr.sin_addr.s_addr = htonl(INADDR_ANY);

//     int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
//     bind(sock, (struct sockaddr *)&source_addr, sizeof(source_addr));

//     uint8_t *w_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
//     assert(w_buf); // Check if w_buf allocation success
//     size_t w_bytes = 0;

//     while (1) 
//     {
//                     ESP_LOGI(TAG, "Receiving");
//         struct sockaddr_in dest_addr;
//         socklen_t socklen = sizeof(dest_addr);
//         int len = recvfrom(sock, w_buf, EXAMPLE_BUFF_SIZE, 0, (struct sockaddr *)&dest_addr, &socklen);

//         if (len < 0)
//         {
//             ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
//         }
//         else
//         {
//             ESP_LOGI(TAG, "Check silence");
//             // Check if the buffer is silent
//             bool is_silent = true;
//             for (int i = 0; i < len; i++)
//             {
//                 if (w_buf[i] != 0)
//                 {
//                     is_silent = false;
//                     break;
//                 }
//             }

//             if (is_silent)
//             {
//                 memset(w_buf, 0, EXAMPLE_BUFF_SIZE);
//                 if (i2s_channel_write(tx_chan, w_buf, len, &w_bytes, 1000) == ESP_OK)
//                 {
//                     ESP_LOGE(TAG, "i2s buffer cleared");
//                 }
//             }
//             else
//             {
//                 if (i2s_channel_write(tx_chan, w_buf, len, &w_bytes, 1000) != ESP_OK)
//                 {
//                     ESP_LOGE(TAG, "i2s write failed");
//                 }
//             }
//         }
//     }
//     free(w_buf);
// }

void udp_receive_task(void *pvParameters)
{
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in source_addr;
    source_addr.sin_family = AF_INET;
    source_addr.sin_port = htons(PORT);
    source_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    bind(sock, (struct sockaddr *)&source_addr, sizeof(source_addr));

    struct timeval timeout;
    timeout.tv_sec = 0; // 0 секунд
    timeout.tv_usec = 100000; // 100 мілісекунд
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    uint8_t *w_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
    assert(w_buf); // Check if w_buf allocation success
    size_t w_bytes = 0;

    TickType_t last_receive_time = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(100); // 100 мілісекунд таймаут

    while (1) {
        ESP_LOGI(TAG, "Receiving");

        struct sockaddr_in dest_addr;
        socklen_t socklen = sizeof(dest_addr);
        int len = recvfrom(sock, w_buf, EXAMPLE_BUFF_SIZE, 0, (struct sockaddr *)&dest_addr, &socklen);

        if (len < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Таймаут recvfrom, продовжуємо цикл
                if (xTaskGetTickCount() - last_receive_time > timeout_ticks) {
                    memset(w_buf, 0, EXAMPLE_BUFF_SIZE); // Очищення буфера при таймауті
                    for (int i = 0; i < 5; i++) { // Відправляємо кілька нульових буферів поспіль
                        if (i2s_channel_write(tx_chan, w_buf, EXAMPLE_BUFF_SIZE, &w_bytes, 1000) != ESP_OK) {
                            ESP_LOGE(TAG, "i2s write failed");
                            break; // Виходимо з циклу, якщо запис не вдається
                        }
                    }
                    last_receive_time = xTaskGetTickCount(); // Оновлюємо час останнього очищення буфера
                }
            } else {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            }
        } else {
            last_receive_time = xTaskGetTickCount(); // Оновлюємо час останнього отримання даних

            if (i2s_channel_write(tx_chan, w_buf, len, &w_bytes, 1000) != ESP_OK) {
                ESP_LOGE(TAG, "i2s write failed");
            }
        }

       // vTaskDelay(10 / portTICK_PERIOD_MS); // Додаємо невелику затримку
    }

    free(w_buf);
}


// void clear_sound_task(void *pvParameters)
// {
//     uint8_t *w_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
//     assert(w_buf); // Check if w_buf allocation success
//     size_t w_bytes = 0;

//     while (1) {
//         // Перевірка на таймаут
//         if (xTaskGetTickCount() - last_receive_time > timeout_ticks) {
//             memset(w_buf, 0, EXAMPLE_BUFF_SIZE); // Очищення буфера при таймауті
//             if (i2s_channel_write(tx_chan, w_buf, EXAMPLE_BUFF_SIZE, &w_bytes, 1000) != ESP_OK) {
//                 ESP_LOGE(TAG, "i2s write failed");
//             }
//         }

//         vTaskDelay(500 / portTICK_PERIOD_MS); // Перевірка кожні 500 мс
//     }

//     free(w_buf);
// }



void microphone_init(void)
{
    i2s_chan_config_t rx_chan_cfg  = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_RX, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg , NULL, &rx_chan));
    
    i2s_std_config_t rx_std_cfg  = 
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
        {
            .mclk = I2S_GPIO_UNUSED, 
            .bclk = I2S_SCL_RX,
            .ws = I2S_WS_RX,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_SD_RX,
            .invert_flags =
            {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg ));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

void speaker_init(void)
{
    i2s_chan_config_t tx_chan_cfg  = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_TX, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));
    
    i2s_std_config_t tx_std_cfg  = 
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
        {
            .mclk = I2S_GPIO_UNUSED, 
            .bclk = I2S_SCK_TX,
            .ws = I2S_WS_TX,
            .dout = I2S_DATA_OUT_TX,
            .din = -1
        }
    };


    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg ));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init();
    microphone_init();
    speaker_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    xTaskCreate(udp_send_task, "udp_send_task", 4096, NULL, 2, NULL); 
    xTaskCreate(udp_receive_task, "udp_receive_task", 4096, NULL, 2, NULL);

    xTaskCreate(button_task, "button_task", 4096, NULL, 3, NULL);

    //xTaskCreate(clear_sound_task, "clear_sound_task", NULL, 3, NULL)
   // ESP_LOGE(TAG, "Main OK");
}
