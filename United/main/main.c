#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <string.h>
#include "driver/gpio.h"

//#define IS_SERVER
#define IS_CLIENT

#define BUTTON_GPIO GPIO_NUM_35

#define EXAMPLE_ESP_WIFI_SSID "esp32_ap"
#define EXAMPLE_ESP_WIFI_PASS "password"
#define PORT 1234
#define CLIENT_IP_ADDR "192.168.4.2"
#define SERVER_IP_ADDR "192.168.4.1"

static const char *TAG = "UDP_UNITED";
static bool got_ip = false;
char payload[128];

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

void udp_send_task(void *pvParameters)
{
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

#ifdef IS_SERVER
    dest_addr.sin_addr.s_addr = inet_addr(CLIENT_IP_ADDR);
#else
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDR);
#endif

    while (1) 
    {
        if (transmit_data) 
        {
#ifdef IS_SERVER
            ESP_LOGI(TAG, "Message from server");
            strcpy(payload, "Hello from server!");
#else
            ESP_LOGI(TAG, "Message from client");
            strcpy(payload, "Hello from ESP32 client!");
#endif

            int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
            int err = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0) 
            {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            } 
            else 
            {
                ESP_LOGI(TAG, "Message sent");
            }
            close(sock);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

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

    while (1) 
    {
        ESP_LOGI(TAG, "Waiting for data");

        struct sockaddr_in dest_addr;
        socklen_t socklen = sizeof(dest_addr);
        char rx_buffer[128];
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&dest_addr, &socklen);

        if (len < 0) 
        {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
        }
        else 
        {
            rx_buffer[len] = 0;
            ESP_LOGI(TAG, "Received %d bytes:", len);
            ESP_LOGI(TAG, "%s", rx_buffer);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init();

    vTaskDelay(10000 / portTICK_PERIOD_MS);

    xTaskCreate(udp_send_task, "udp_send_task", 4096, NULL, 5, NULL); 
    xTaskCreate(udp_receive_task, "udp_receive_task", 4096, NULL, 5, NULL);
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);

    ESP_LOGE(TAG, "Main OK");
}
