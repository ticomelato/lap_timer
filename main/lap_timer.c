#include <stdio.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "string.h"
#include "esp_timer.h"
#include "wifi.h"
#include "nvs_flash.h"

// Variáveis globais que serão manipuladas e exibidas no portal cativo
float velocidade = 0.0;
float volta_rapida = 0.0; // volta mais rapida
float volta_anterior = 0.0; // volta mais rapida
float linha_lat = 9.11111, linha_long = 9.222222222; // Ponto da linha de chegada/largada
float posicao1_lat = 1.111111, posicao1_long = 1.2222222;
float posicao2_lat = 2.111111, posicao2_long = 2.2222222;
float posicao3_lat = 3.111111, posicao3_long = 3.2222222;

#define EARTH_RADIUS 6371000.0          // Utilizado para a formula de Haversine

#define TOLERANCE 10.0                  // Tolerância em metros para computar a passagem por um checkpoint

static const int RX_BUF_SIZE = 1024;    // Utilizado para o buffer do pino RX da porta UART

#define TXD_PIN (GPIO_NUM_17)           // Pino 17 da ESP definido como TX
#define RXD_PIN (GPIO_NUM_16)           // Pino 16 da ESP definido como RX

// Coordenadas da linha de chegada e setor 3
double lat_start = -26.925389;
double lon_start = -48.941590;
// Coordenadas da linha do setor 1
double lat_sec1 = -26.924442;
double lon_sec1 = -48.940674;
// Coordenadas da linha do setor 2
double lat_sec2 = -26.924355;
double lon_sec2 = -48.942377;


typedef struct {
    bool started;             // Se o contador foi iniciado
    bool checkpoint_1;        // Se passou pelo ponto Y
    bool checkpoint_2;        // Se passou pelo ponto Z
    int64_t start_time;       // Tempo de início em microssegundos
    int64_t last_checkpoint_time; // Tempo do último checkpoint
} LapState;

// Inicialização da struct
LapState lap_state = {
    .started = false, 
    .checkpoint_1 = false, 
    .checkpoint_2 = false, 
    .start_time = 0,
    .last_checkpoint_time = 0
};

// Função utilizada para calcular a distancia entre duas coordenadas em metros
double haversine(double lat1, double lon1, double lat2, double lon2){
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;

    // Converte latitude para radianos
    lat1 = lat1 * M_PI / 180.0;
    lat2 = lat2 * M_PI / 180.0;

    // Fórmula de haversine
    double a = sin(dLat / 2) * sin(dLat / 2) +
               sin(dLon / 2) * sin(dLon / 2) * cos(lat1) * cos(lat2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return EARTH_RADIUS * c;
}

// Função para facilitar os prints de tempo
void print_time(const char *label, int64_t start_time, int64_t end_time){
    int64_t elapsed_time_ms = (end_time - start_time) / 1000; // Tempo em milissegundos
    int minutes = elapsed_time_ms / (60 * 1000);             // Minutos
    int seconds = (elapsed_time_ms % (60 * 1000)) / 1000;    // Segundos
    int milliseconds = elapsed_time_ms % 1000;              // Milissegundos

    printf("%s: %02d:%02d:%03d (min:seg:ms)\n", label, minutes, seconds, milliseconds);
}

// Compara a localização atual com os checkpoints para encontrar os tempos
void process_position(double lat, double lon){
    // Verifica se está próximo à linha de chegada
    if (haversine(lat, lon, lat_start, lon_start) < TOLERANCE) {
        if (!lap_state.started) {
            // Inicia o contador
            printf("\nVolta iniciada!\n");
            lap_state.started = true;
            lap_state.start_time = esp_timer_get_time();
            lap_state.last_checkpoint_time = lap_state.start_time;

        } else if (lap_state.checkpoint_1 && lap_state.checkpoint_2) {
            // Finaliza a volta
            int64_t end_time = esp_timer_get_time();

            // Tempo do setor 3 (entre o setor 2 e a finalização)
            print_time("Tempo Setor 3", lap_state.last_checkpoint_time, end_time);

            // Tempo total
            print_time("Volta finalizada! Tempo total", lap_state.start_time, end_time);

            // Reseta o estado
            lap_state.started = false;
            lap_state.checkpoint_1 = false;
            lap_state.checkpoint_2 = false;
            lap_state.start_time = 0;
            lap_state.last_checkpoint_time = 0;
        }
    }

    if (lap_state.started){
        if (!lap_state.checkpoint_1 && haversine(lat, lon, lat_sec1, lon_sec1) < TOLERANCE) {
        int64_t sec1_time = esp_timer_get_time();

        // Tempo entre início e setor 1
        print_time("Tempo Setor 1", lap_state.start_time, sec1_time);

        lap_state.checkpoint_1 = true;
        lap_state.last_checkpoint_time = sec1_time; // Atualiza o último checkpoint
    }

        // Verifica se passou pelo Setor 2
        if (lap_state.checkpoint_1 && !lap_state.checkpoint_2 && haversine(lat, lon, lat_sec2, lon_sec2) < TOLERANCE) {
            int64_t sec2_time = esp_timer_get_time();

            // Tempo entre setor 1 e setor 2
            print_time("Tempo Setor 2", lap_state.last_checkpoint_time, sec2_time);

            lap_state.checkpoint_2 = true;
            lap_state.last_checkpoint_time = sec2_time; // Atualiza o último checkpoint
        }
    }
}

// Inicialização da porta UART
void init_uart(void){
    const uart_config_t uart_config = {
        .baud_rate = 9600,                      // Taxa de transmissão padrão do GPS
        .data_bits = UART_DATA_8_BITS,          // 8 bits de dados
        .parity = UART_PARITY_DISABLE,          // Sem paridade
        .stop_bits = UART_STOP_BITS_1,          // 1 bit de parada
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,  // Sem controle de fluxo
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Instala o driver da UART com buffer de recepção
    esp_err_t ret = uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, RX_BUF_SIZE * 2, 10, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE("UART", "Failed to install UART driver: %s", esp_err_to_name(ret));
        return;
    }

    // Configurando UART
    ret = uart_param_config(UART_NUM_2, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE("UART", "Failed to configure UART parameters: %s", esp_err_to_name(ret));
        return;
    }

    // Configurando os pinos de TX e RX
    ret = uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE("UART", "Failed to set UART pins: %s", esp_err_to_name(ret));
    }
}

// Função corrigida para converter coordenadas NMEA para decimal
double nmea_to_decimal(const char *coord, char direction){
    double degrees = 0.0;
    double minutes = 0.0;

    // Verifica o formato: latitude tem 2 dígitos para graus, longitude tem 3 dígitos
    if (strlen(coord) > 7) {
        if (coord[4] == '.') { // Exemplo: 2655.48542 (latitude)
            sscanf(coord, "%2lf%lf", &degrees, &minutes); // 2 dígitos para latitude
        } else { // Exemplo: 04856.61815 (longitude)
            sscanf(coord, "%3lf%lf", &degrees, &minutes); // 3 dígitos para longitude
        }
    }

    // Converte para decimal
    double decimal = degrees + (minutes / 60.0);

    // Aplica sinal baseado na direção
    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

// Função para processar mensagens NMEA e extrair dados do $GNRMC
void process_nmea_line(const char *line){
    // Verifica se a linha começa com $GNRMC
    if (strncmp(line, "$GNRMC", 6) == 0) {
        // Tokens da linha separados por vírgulas
        char tokens[20][20];
        int token_count = 0;
        const char *ptr = line;

        // Divide a linha em tokens separados por vírgulas
        while (*ptr) {
            char *comma = strchr(ptr, ',');
            if (comma == NULL) {
                strcpy(tokens[token_count++], ptr);
                break;
            } else {
                strncpy(tokens[token_count], ptr, comma - ptr);
                tokens[token_count][comma - ptr] = '\0';
                token_count++;
                ptr = comma + 1;
            }
        }
        
        // Status (V = inválido, A = ativo)
        if (strcmp(tokens[2], "A") == 0) {
            // Latitude
            double latitude = nmea_to_decimal(tokens[3], tokens[4][0]);

            // Longitude
            double longitude = nmea_to_decimal(tokens[5], tokens[6][0]);

            // Velocidade em nós
            double speed_knots = atof(tokens[7]);

            // Converte a velocidade para km/h
            double speed_kmh = speed_knots * 1.852;

            // Extração de horário UTC (hhmmss.ss)
            char utc_time[16];
            strncpy(utc_time, tokens[1], sizeof(utc_time));

            int hours, minutes, seconds;
            sscanf(utc_time, "%2d%2d%2d", &hours, &minutes, &seconds);

            // Extração de data (ddmmyy)
            char date[16];
            strncpy(date, tokens[9], sizeof(date));

            int day, month, year;
            sscanf(date, "%2d%2d%2d", &day, &month, &year);
            year += 2000; // Ajustar para ano completo

            // Imprime resultados
            
            /*
            printf("Data (UTC): %02d/%02d/%04d\n", day, month, year);
            printf("Hora (UTC): %02d:%02d:%02d\n", hours, minutes, seconds);
            printf("Latitude: %.8f\n", latitude);
            printf("Longitude: %.8f\n", longitude);
            printf("Velocidade: %.2f km/h\n\n", speed_kmh);
            */

            // Verificação de passagem em checkpoints
            process_position(latitude, longitude);

        } else {
            printf("Sem sinal de GPS, aguarde.\n");
        }
    }
}

// Função para ler a porta UART, armazenar cada linha em buffer e enviar para process_nmea_line()
static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);

    uint8_t *data = (uint8_t *)malloc(RX_BUF_SIZE + 1); // Buffer de recepção
    if (!data) {
        ESP_LOGE(RX_TASK_TAG, "Failed to allocate memory for RX buffer");
        vTaskDelete(NULL);
        return;
    }

    char *line_buffer = (char *)malloc(RX_BUF_SIZE); // Aloca dinamicamente o buffer de linha
    if (!line_buffer) {
        ESP_LOGE(RX_TASK_TAG, "Failed to allocate memory for line buffer");
        free(data);
        vTaskDelete(NULL);
        return;
    }
    memset(line_buffer, 0, RX_BUF_SIZE); // Preenche com zeros

    int line_pos = 0; // Posição atual no buffer de linha

    while (1) {
        // Lê os dados recebidos na UART
        int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 100 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = '\0'; // Finaliza a string recebida
            //ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data); // Mostra o log do que foi lido na porta UART

            for (int i = 0; i < rxBytes; i++) {
                char c = data[i];

                // Se for um caractere de nova linha, processa a linha
                if (c == '\n') {
                    line_buffer[line_pos] = '\0'; // Finaliza a linha
                    //ESP_LOGI(RX_TASK_TAG, "Processing line: %s", line_buffer); // Mostra o log de qual linha esta sendo enviar para processamento

                    // Processa a linha NMEA
                    process_nmea_line(line_buffer);

                    // Reseta o buffer de linha
                    memset(line_buffer, 0, RX_BUF_SIZE);
                    line_pos = 0;
                } else if (c != '\r' && line_pos < RX_BUF_SIZE - 1) {
                    // Adiciona o caractere ao buffer de linha, ignorando '\r'
                    line_buffer[line_pos++] = c;
                }
            }
        } else if (rxBytes < 0) {
            ESP_LOGE(RX_TASK_TAG, "UART read error");
        }
    }

    free(data);
    free(line_buffer); // Libera a memória alocada para o buffer de linha
    vTaskDelete(NULL);
}

void app_main(void){

    init_uart(); //Chama função para inicializar a porta UART

    printf("Inicializando portal cativo...\n");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Iniciar o modo AP e o servidor para o portal cativo
    start_portal_cativo();

    xTaskCreate(rx_task, "uart_rx_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL); // Cria a task que lê a porta UART

    while (1) {
    // Simula alterações nas variáveis
    velocidade += 0.5;  // Simula alteração de velocidade
    volta_rapida = 60.0 - velocidade; // Simula uma volta rápida
    volta_anterior = 60.0 + velocidade; // Simula uma volta rápida

    // Log dos valores de posição com latitude e longitude
    ESP_LOGI("MAIN", "Velocidade: %.1fkm/h, Volta Rápida: %.2fs, Volta Anterior: %.2fs", 
            velocidade, volta_rapida, volta_anterior);
    ESP_LOGI("MAIN", "Linha de Chegada/Saída: Lat %.6f, Long %.6f", 
            linha_lat, linha_long);
    ESP_LOGI("MAIN", "Setor 1: Lat %.6f, Long %.6f", 
            posicao1_lat, posicao1_long);
    ESP_LOGI("MAIN", "Setor 2: Lat %.6f, Long %.6f", 
            posicao2_lat, posicao2_long);
    ESP_LOGI("MAIN", "Setor 3: Lat %.6f, Long %.6f", 
            posicao3_lat, posicao3_long);
    // Atraso de 2 segundos
    vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
};