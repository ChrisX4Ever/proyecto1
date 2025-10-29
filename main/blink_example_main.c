// app_main.c
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"

// --- tu código BMI / defines (copiados/adaptados) ---
#define I2C_MASTER_SCL_IO            GPIO_NUM_47
#define I2C_MASTER_SDA_IO            GPIO_NUM_48
#define I2C_MASTER_FREQ_HZ           10000
#define ESP_SLAVE_ADDR               0x68

// sampling default (Fodr) (se usa para configurar BMI)
static uint32_t Fodr = 400; // valor inicial

// Prototipos (asumo tus funciones bmi_read/bmi_write/bmi_init tal como definiste)
extern esp_err_t bmi_read(uint8_t *data_address, uint8_t *data_rd, size_t size);
extern esp_err_t bmi_write(uint8_t *data_address, uint8_t *data_wr, size_t size);
extern esp_err_t bmi_init(void);

// --- Configuración WiFi / TCP Server (ajustar) ---
#define WIFI_SSID       "TU_SSID"
#define WIFI_PASS       "TU_PASSWORD"
#define SERVER_IP       "192.168.1.100"   // IP de la Raspberry Pi (ajusta)
#define SERVER_PORT     5000

static const char *TAG = "BMI_TCP";

// estado global protegido
static int sockfd = -1;
static SemaphoreHandle_t sock_mutex;
static volatile bool tcp_connected = false;
static volatile bool sending = false; // true=transmitir muestras, false=conectado pero sin transmitir

// Helpers
static esp_err_t wifi_init_sta(void);
static void tcp_client_task(void *arg);
static void sensor_task(void *arg);
static esp_err_t send_all(const char *buf, size_t len);
static void handle_command(const char *cmd);

// -----------------------------------------------------------------------------
// Implementación WiFi (modo station básico)
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi desconectado, reintentando...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Obtuvo IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password)-1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi inicializado en modo STA");
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Enviar todo (asegurar que se envía completo)
static esp_err_t send_all(const char *buf, size_t len)
{
    if (!tcp_connected) return ESP_FAIL;
    xSemaphoreTake(sock_mutex, portMAX_DELAY);
    if (sockfd < 0) {
        xSemaphoreGive(sock_mutex);
        return ESP_FAIL;
    }
    ssize_t total = 0;
    while (total < (ssize_t)len) {
        ssize_t sent = send(sockfd, buf + total, len - total, 0);
        if (sent < 0) {
            ESP_LOGW(TAG, "send() falló");
            xSemaphoreGive(sock_mutex);
            return ESP_FAIL;
        }
        total += sent;
    }
    xSemaphoreGive(sock_mutex);
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Tarea TCP: conecta y atiende comandos entrantes
static void tcp_client_task(void *arg)
{
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    while (1) {
        ESP_LOGI(TAG, "Intentando conectar a %s:%d ...", SERVER_IP, SERVER_PORT);
        int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (s < 0) {
            ESP_LOGE(TAG, "No se pudo crear socket");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // conectar
        if (connect(s, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
            ESP_LOGW(TAG, "connect falla, reintentando");
            close(s);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // marcar conectado
        xSemaphoreTake(sock_mutex, portMAX_DELAY);
        sockfd = s;
        tcp_connected = true;
        xSemaphoreGive(sock_mutex);
        ESP_LOGI(TAG, "Conectado al servidor TCP.");
        // si estamos conectados pero sending==false -> estado "conexión pero sin transmisión"
        if (!sending) {
            ESP_LOGI(TAG, "Conexión establecida pero sin transmisión (esperando START).");
        } else {
            ESP_LOGI(TAG, "Conexión y transmisión activa.");
        }

        // loop de lectura de comandos: usar recv con timeout
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        char rxbuf[128];
        while (tcp_connected) {
            ssize_t r = recv(s, rxbuf, sizeof(rxbuf)-1, 0);
            if (r > 0) {
                rxbuf[r] = '\0';
                ESP_LOGI(TAG, "Recibido comando: %s", rxbuf);
                // puede venir con saltos de línea, recorre por líneas
                char *saveptr = NULL;
                char *line = strtok_r(rxbuf, "\r\n", &saveptr);
                while (line) {
                    handle_command(line);
                    line = strtok_r(NULL, "\r\n", &saveptr);
                }
            } else if (r == 0) {
                ESP_LOGW(TAG, "Servidor cerró conexión");
                break;
            } else {
                // r < 0, timeout o error
                // si errno == EWOULDBLOCK/EAGAIN -> no hay datos ahora, seguimos
                // comprobamos estado sending/connected - si sending true y no hubo datos, seguimos enviando en sensor_task
                // Simplemente loop para mantener la conexión viva
            }

            // Chequear socket alive: opcionalmente podemos enviar heartbeat PING si queremos
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // limpiar al desconectarse
        xSemaphoreTake(sock_mutex, portMAX_DELAY);
        if (sockfd >= 0) close(sockfd);
        sockfd = -1;
        tcp_connected = false;
        xSemaphoreGive(sock_mutex);
        sending = false; // preferible forzar stop al perder conexión
        ESP_LOGW(TAG, "Desconectado. Reintentando en 2s...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// -----------------------------------------------------------------------------
// Manejo de comandos recibidos desde Raspberry Pi
static void handle_command(const char *cmd)
{
    if (cmd == NULL) return;

    if (strncmp(cmd, "SRATE ", 6) == 0) {
        int val = atoi(cmd + 6);
        if (val == 100 || val == 400 || val == 1600) {
            Fodr = val;
            ESP_LOGI(TAG, "Frequencia muestreo cambiada a %d Hz", Fodr);
            // re-configurar BMI si ya inicializado: llamar función que configura registro acc_conf
            // enviar ack
            send_all("SRATE_OK\n", strlen("SRATE_OK\n"));
        } else {
            send_all("SRATE_ERR\n", strlen("SRATE_ERR\n"));
        }
    } else if (strcmp(cmd, "START") == 0) {
        sending = true;
        ESP_LOGI(TAG, "START recibido: comenzando transmisión de datos");
        send_all("STARTED\n", strlen("STARTED\n"));
    } else if (strcmp(cmd, "STOP") == 0) {
        sending = false;
        ESP_LOGI(TAG, "STOP recibido: deteniendo transmisión de datos (pero manteniendo conexión)");
        send_all("STOPPED\n", strlen("STOPPED\n"));
    } else if (strcmp(cmd, "PING") == 0) {
        send_all("PONG\n", strlen("PONG\n"));
    } else {
        ESP_LOGI(TAG, "Comando desconocido: %s", cmd);
        send_all("UNK_CMD\n", strlen("UNK_CMD\n"));
    }
}

// -----------------------------------------------------------------------------
// Tarea de sensor: lee, arma JSON y envía cuando sending==true y tcp_connected==true
static void sensor_task(void *arg)
{
    // Si necesitas inicializar BMI aquí, hacerlo (bmi_init ya en app_main)
    // se hará muestreo en bucle; intervalo entre muestras = 1/Fodr segundos
    while (1) {
        if (!tcp_connected) {
            // no hay conexión: esperar y luego reintentar sin reiniciar
            ESP_LOGI(TAG, "Sensor: sin conexión TCP, esperando reconexión...");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!sending) {
            // Conectado pero no enviando
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Cuando sending==true y tcp_connected==true -> tomar una muestra
        // leer datos del BMI (usar tu función lectura simplificada)
        // Leemos registros de datos crudos (asumiendo que reg_data = 0x0C y 12 bytes)
        uint8_t reg_data = 0x0C;
        uint8_t data_data8[12];
        esp_err_t r = bmi_read(&reg_data, data_data8, 12);
        if (r != ESP_OK) {
            ESP_LOGW(TAG, "Error leyendo BMI: %s", esp_err_to_name(r));
            // si error en lectura, esperar y seguir
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // convertir a signed 16 bit
        int16_t acc_x = (int16_t)((data_data8[1] << 8) | data_data8[0]);
        int16_t acc_y = (int16_t)((data_data8[3] << 8) | data_data8[2]);
        int16_t acc_z = (int16_t)((data_data8[5] << 8) | data_data8[4]);
        int16_t gyr_x = (int16_t)((data_data8[7] << 8) | data_data8[6]);
        int16_t gyr_y = (int16_t)((data_data8[9] << 8) | data_data8[8]);
        int16_t gyr_z = (int16_t)((data_data8[11] << 8) | data_data8[10]);

        // Escalas (las mismas que usabas)
        float acc_scale_ms2 = 78.4532f / 32768.0f; // m/s2 per bit
        float acc_scale_g = 8.0f / 32768.0f;       // g per bit (si tu cfg era +/-8g)
        float gyr_scale = 34.90659f / 32768.0f;    // rad/s per bit

        // timestamp ms
        int64_t t_us = esp_timer_get_time(); // microsegundos
        int64_t t_ms = t_us / 1000;

        // Generar JSON (una línea por muestra)
        char outbuf[256];
        int n = snprintf(outbuf, sizeof(outbuf),
            "{\"ts_ms\":%" PRId64 ",\"acc_m_s2\":[%.5f,%.5f,%.5f],\"acc_g\":[%.5f,%.5f,%.5f],\"gyr_rad_s\":[%.5f,%.5f,%.5f],\"fs_hz\":%u}\n",
            t_ms,
            acc_x * acc_scale_ms2, acc_y * acc_scale_ms2, acc_z * acc_scale_ms2,
            acc_x * acc_scale_g, acc_y * acc_scale_g, acc_z * acc_scale_g,
            gyr_x * gyr_scale, gyr_y * gyr_scale, gyr_z * gyr_scale,
            (unsigned)Fodr
        );

        // enviar por TCP
        if (send_all(outbuf, n) != ESP_OK) {
            ESP_LOGW(TAG, "Falló envío de muestra, marcando como desconectado");
            // forzar cierre de socket para que task de tcp_client reintente
            xSemaphoreTake(sock_mutex, portMAX_DELAY);
            if (sockfd >= 0) close(sockfd);
            sockfd = -1;
            tcp_connected = false;
            xSemaphoreGive(sock_mutex);
            sending = false; // detener hasta reconexión y nuevo START
            continue;
        } else {
            // informe de envío correcto (puedes moderar la verbosidad)
            ESP_LOGI(TAG, "Muestra enviada ts=%" PRId64, t_ms);
        }

        // esperar intervalo según Fodr
        if (Fodr > 0) {
            uint32_t wait_ms = 1000 / Fodr;
            if (wait_ms == 0) wait_ms = 1; // mínimo 1ms
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// -----------------------------------------------------------------------------
// app_main: inicializa WiFi, BMI, crea tareas
void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando...");

    // mutex para proteger sockfd
    sock_mutex = xSemaphoreCreateMutex();

    // Inicializar WiFi
    ESP_ERROR_CHECK(wifi_init_sta());

    // Inicializar BMI (usa tu bmi_init)
    ESP_ERROR_CHECK(bmi_init());
    // Aquí asumo que haces softreset, chipid, initialization, etc.
    // Llamar a tus funciones si están disponibles externamente:
    // softreset(); chipid(); initialization(); check_initialization(); bmipowermode(); internal_status();
    // Para este ejemplo: asumo que tu código ya las llama o puedes llamarlas aquí.

    // Crear tareas
    xTaskCreate(tcp_client_task, "tcp_client_task", 8 * 1024, NULL, 5, NULL);
    xTaskCreate(sensor_task, "sensor_task", 8 * 1024, NULL, 6, NULL);

    ESP_LOGI(TAG, "Tareas creadas.");
}