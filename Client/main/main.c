#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <string.h>

#define EXAMPLE_ESP_WIFI_SSID "esp32_ap"
#define EXAMPLE_ESP_WIFI_PASS "password"
#define HOST_IP_ADDR "192.168.4.1"
#define PORT 1234

static const char *TAG = "UDP_CLIENT";
static bool got_ip = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP || event_base == IP_EVENT)
    {
                       ESP_LOGE(TAG, "IP OK");
               got_ip = true; // Set IP flag when IP is obtained
    }
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
         //got_ip = true;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
        got_ip = false; // Reset IP flag if disconnected
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        got_ip = true; // Set IP flag when IP is obtained
    }
}

static void wifi_init_sta(void)
{
    // Initialize the ESP32 WiFi stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create a new STA network interface instance
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    // Initialize the WiFi library with the default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Configure the WiFi SSID and password
    wifi_config_t wifi_config = {
        .sta = {
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
}



static void udp_client_task(void *pvParameters)
{
        ESP_LOGE(TAG, "UDP OK");
    char payload[128];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    while (1) {
        if (!got_ip) {
            ESP_LOGI(TAG, "Waiting for IP...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Creating socket...");
        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, PORT);
        strcpy(payload, "Hello from ESP32 client!");
        int err = sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        } else {
            ESP_LOGI(TAG, "Message sent");
        }

        if (sock != -1) {
            ESP_LOGI(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_sta();

    // Add a delay to ensure the client has enough time to connect to the AP
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, NULL);
    
        ESP_LOGE(TAG, "Main OK");
}
