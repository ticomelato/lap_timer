#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include <string.h>
#include "wifi.h"

#include "cJSON.h"


#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))

#endif

#define DNS_PORT 53
#define CAPTIVE_PORTAL_IP "192.168.4.1"

static const char *TAG = "PORTAL_CATIVO";

// Variáveis globais acessadas de main.c
extern float velocidade;
extern char tempo_set1[20],  tempo_set2[20],  tempo_set3[20], volta_atual[20], volta_anterior[20];

extern float lat_start, lon_start;
extern float lat_sec1, lon_sec1;
extern float lat_sec2, lon_sec2;


// Tarefa do servidor DNS para redirecionar consultas
void dns_server_task(void *pvParameters) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_PORT);

    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    char buffer[512];
    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (len > 0) {
            memset(buffer + 2, 0x84, 1); // Configura a flag de resposta DNS
            sendto(sock, buffer, len, 0, (struct sockaddr *)&client_addr, client_len);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    close(sock);
}

esp_err_t get_handler(httpd_req_t *req) {
    const char *response =
        "<!DOCTYPE html>"
        "<html lang=\"pt-BR\">"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<title>Lap Timer</title>"
        "<style>"
        "body { background-color: black; color: white; font-family: Arial, sans-serif; }"
        "h1 { text-align: center; }"
        ".container { display: flex; flex-direction: column; align-items: center; margin-top: 20px; }"
        ".box { border: 1px solid white; padding: 20px; border-radius: 10px; width: 90%; max-width: 300px; margin-bottom: 20px; }"
        ".box h2 { text-align: center; }"
        ".box input { width: 80%; margin-bottom: 10px; padding: 5px; }"
        ".box button { width: 80%; padding: 5px; background-color: white; color: black; border: none; border-radius: 5px; cursor: pointer; }"
        ".box button:hover { background-color: grey; }"
        "</style>"
        "<script>"
        "function fetchData() {"
        "  fetch('/data')"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      document.getElementById('velocidade').innerText = data.velocidade ? data.velocidade + ' km/h' : '0 km/h';"
        "      document.getElementById('volta_atual').innerText = data.volta_atual || 'Sem Dados';"
        "      document.getElementById('volta_anterior').innerText = data.volta_anterior || 'Sem Dados';"
        "      document.getElementById('tempo_set1').innerText = data.tempo_set1 || 'Sem Dados';"
        "      document.getElementById('tempo_set2').innerText = data.tempo_set2 || 'Sem Dados';"
        "      document.getElementById('tempo_set3').innerText = data.tempo_set3 || 'Sem Dados';"
        "    });"
        "}"
        "function sendData() {"
        "  const lat_start = document.getElementById('lat_start').value || '0';"
        "  const lon_start = document.getElementById('lon_start').value || '0';"
        "  const pos1_lat = document.getElementById('pos1_lat').value || '0';"
        "  const pos1_long = document.getElementById('pos1_long').value || '0';"
        "  const pos2_lat = document.getElementById('pos2_lat').value || '0';"
        "  const pos2_long = document.getElementById('pos2_long').value || '0';"
        "  fetch('/submit', {"
        "    method: 'POST',"
        "    headers: { 'Content-Type': 'application/json' },"
        "    body: JSON.stringify({"
        "      lat_start, lon_start,"
        "      pos1_lat, pos1_long,"
        "      pos2_lat, pos2_long,"
        "    })"
        "  }).then(response => response.text())"
        "    .then(data => alert('Posições salvas com sucesso!'))"
        "    .catch(error => alert('Erro ao enviar os dados!'))"
        "}"
        "setInterval(fetchData, 500);"
        "</script>"
        "</head>"
        "<body onload=\"fetchData()\">"
        "<h1>Bem-vindo ao Lap Timer</h1>"
        "<div class=\"container\">"
        "<div class=\"box\">"
        "<h2>Informações</h2>"
        //Aqui
        "<p>Velocidade: <span id=\"velocidade\"></span></p>"
        "<p>Volta Atual: <span id=\"volta_atual\"></span></p>"
        "<h2>Volta Anterior</h2>"
        "<p>Volta Anterior: <span id=\"volta_anterior\"></span></p>"
        "<p>Setor 1: <span id=\"tempo_set1\"></span></p>"
        "<p>Setor 2: <span id=\"tempo_set2\"></span></p>"
        "<p>Setor 3: <span id=\"tempo_set3\"></span></p>"        
        "</div>"
        "<div class=\"box\">"
        "<h2>Atualizar Posições</h2>"
        "<h3>Linha de Chegada/Saída</h3>"
        "<input id=\"lat_start\" type=\"text\" placeholder=\"Latitude Linha\" />"
        "<input id=\"lon_start\" type=\"text\" placeholder=\"Longitude Linha\" />"
        "<h3>Setor 1</h3>"
        "<input id=\"pos1_lat\" type=\"text\" placeholder=\"Latitude Setor 1\" />"
        "<input id=\"pos1_long\" type=\"text\" placeholder=\"Longitude Setor 1\" />"
        "<h3>Setor 2</h3>"
        "<input id=\"pos2_lat\" type=\"text\" placeholder=\"Latitude Setor 2\" />"
        "<input id=\"pos2_long\" type=\"text\" placeholder=\"Longitude Setor 2\" />"
        "<br><br>"
        "<button onclick=\"sendData()\">Atualizar</button>"
        "</div>"
        "</div>"
        "</body>"
        "</html>";

    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}




// Manipulador para retornar dados JSON
esp_err_t json_handler(httpd_req_t *req) {
    char response[1024];

    //ESP_LOGI(TAG, "Enviando dados JSON: velocidade=%.0f, volta_atual=%s, volta_anterior=%s", velocidade, volta_atual, volta_anterior);

    
    snprintf(response, sizeof(response),
                "{\"velocidade\": %.1f, \"volta_atual\": \"%s\", \"volta_anterior\": \"%s\","
                "\"tempo_set1\": \"%s\", \"tempo_set2\": \"%s\", \"tempo_set3\": \"%s\","
                "\"lat_start\": %.8f, \"lon_start\": %.8f,"
                "\"pos1_lat\": %.8f, \"pos1_long\": %.8f,"
                "\"pos2_lat\": %.8f, \"pos2_long\": %.8f}",
                velocidade, volta_atual, volta_anterior,
                tempo_set1, tempo_set2, tempo_set3,
                lat_start, lon_start,
                lat_sec1, lon_sec1,
                lat_sec2, lon_sec2);

/*
    snprintf(response, sizeof(response),
            "{\"velocidade\": %.1f, \"volta_atual\": %s, \"volta_anterior\": %s,"
            "\"tempo_set1\": %s, \"tempo_set2\": %s, \"tempo_set3\": %s,"
            "\"lat_start\": %.8f, \"lon_start\": %.8f,"
            "\"pos1_lat\": %.8f, \"pos1_long\": %.8f,"
            "\"pos2_lat\": %.8f, \"pos2_long\": %.8f}",
            velocidade, volta_atual, volta_anterior,
            tempo_set1, tempo_set2, tempo_set3,
            lat_start, lon_start,
            lat_sec1, lon_sec1,
            lat_sec2, lon_sec2);
*/

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}



// Manipulador para receber dados POST
esp_err_t post_handler(httpd_req_t *req) {
    char buf[200];
    int ret = 0;
    int remaining = req->content_len;

    // Recebendo os dados do corpo da requisição
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0) {
            ESP_LOGE(TAG, "Erro ao receber dados do cliente");
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[ret] = '\0'; // Certifique-se de que a string está terminada

    ESP_LOGI(TAG, "Dados recebidos: %s", buf);

    // Parse dos dados JSON
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *lin_lat = cJSON_GetObjectItem(root, "lin_lat");
        cJSON *lin_long = cJSON_GetObjectItem(root, "lin_long");
        cJSON *pos1_lat = cJSON_GetObjectItem(root, "pos1_lat");
        cJSON *pos1_long = cJSON_GetObjectItem(root, "pos1_long");
        cJSON *pos2_lat = cJSON_GetObjectItem(root, "pos2_lat");
        cJSON *pos2_long = cJSON_GetObjectItem(root, "pos2_long");

        // Converta e atualize apenas se os valores forem strings válidas ou números
        if (lin_lat && cJSON_IsString(lin_lat) && strlen(lin_lat->valuestring) > 0) {
            lat_start = atof(lin_lat->valuestring);
            ESP_LOGI(TAG, "Linha Latitude atualizado: %.6f", lat_start);
        }
        if (lin_long && cJSON_IsString(lin_long) && strlen(lin_long->valuestring) > 0) {
            lon_start = atof(lin_long->valuestring);
            ESP_LOGI(TAG, "Linha Longitude atualizado: %.6f", lon_start);
        }
        if (pos1_lat && cJSON_IsString(pos1_lat) && strlen(pos1_lat->valuestring) > 0) {
            lat_sec1 = atof(pos1_lat->valuestring);
            ESP_LOGI(TAG, "Setor 1 Latitude atualizado: %.6f", lat_sec1);
        }
        if (pos1_long && cJSON_IsString(pos1_long) && strlen(pos1_long->valuestring) > 0) {
            lon_sec1 = atof(pos1_long->valuestring);
            ESP_LOGI(TAG, "Setor 1 Longitude atualizado: %.6f", lon_sec1);
        }
        if (pos2_lat && cJSON_IsString(pos2_lat) && strlen(pos2_lat->valuestring) > 0) {
            lat_sec2 = atof(pos2_lat->valuestring);
            ESP_LOGI(TAG, "Setor 2 Latitude atualizado: %.6f", lat_sec2);
        }
        if (pos2_long && cJSON_IsString(pos2_long) && strlen(pos2_long->valuestring) > 0) {
            lon_sec2 = atof(pos2_long->valuestring);
            ESP_LOGI(TAG, "Setor 2 Longitude atualizado: %.6f", lon_sec2);
        }

        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG, "Erro ao fazer parse do JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dados inválidos");
        return ESP_FAIL;
    }

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}



// Função para iniciar o servidor HTTP
static void start_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri = "/",
            .method = HTTP_GET,
            .handler = get_handler});
        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri = "/data",
            .method = HTTP_GET,
            .handler = json_handler});
        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri = "/submit",
            .method = HTTP_POST,
            .handler = post_handler});

        ESP_LOGI(TAG, "Servidor HTTP iniciado com sucesso.");
    } else {
        ESP_LOGE(TAG, "Erro ao iniciar o servidor HTTP.");
    }
}

// Função para configurar o WiFi no modo AP
void start_portal_cativo() {
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGE(TAG, "Falha ao criar a interface WiFi AP");
        return;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = "Lap Timer",
            .ssid_len = strlen("Lap Timer"),
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN}};
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP iniciado com SSID: Lap Timer");

    xTaskCreate(&dns_server_task, "dns_server_task", 2048, NULL, 5, NULL);
    start_http_server();
}