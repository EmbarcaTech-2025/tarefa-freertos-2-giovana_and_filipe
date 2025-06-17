#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"
#include <string.h>
#include <stdio.h>

// ================= DEFINIÇÕES DE HARDWARE =================
#define BUZZER_PIN      21
#define BUTTON_CONFIRM  5   // Botão A
#define BUTTON_SNOOZE   6   // Botão B
#define JOYSTICK_X_ADC  1   // Canal ADC X (VRX)
#define JOYSTICK_Y_ADC  0   // Canal ADC Y (VRY)
#define I2C_SDA         14
#define I2C_SCL         15
#define DEADZONE        500 // Zona morta do joystick
#define MAX_LENGTH      16  // Tamanho máximo do nome do remédio

// ================= ESTRUTURAS DE DADOS =================
typedef struct {
    uint8_t hour;
    uint8_t minute;
    char med_name[MAX_LENGTH];
} MedicationReminder;

typedef enum {
    MENU_MAIN,
    MENU_ADD_REMINDER,
    MENU_VIEW_REMINDERS,
    MENU_ALERT
} MenuState;

// ================= VARIÁVEIS GLOBAIS =================
static uint8_t display_buffer[ssd1306_buffer_length];
static struct render_area display_area = {
    .start_column = 0,
    .end_column = ssd1306_width - 1,
    .start_page = 0,
    .end_page = ssd1306_n_pages - 1
};

QueueHandle_t xReminderQueue;
SemaphoreHandle_t xDisplayMutex;
MenuState currentMenu = MENU_MAIN;
MedicationReminder reminders[5];
uint8_t reminderCount = 0;
uint8_t selected_hour = 12;
uint8_t selected_minute = 0;

// ================= PROTÓTIPOS DE FUNÇÕES =================
void vInterfaceTask(void *pvParameters);
void vAlertTask(void *pvParameters);
void vTimeManagerTask(void *pvParameters);
void hardware_init();
void display_init();
void clear_display();
void display_menu();
void display_alert(const char *med_name);
void display_text(uint8_t x, uint8_t y, const char *text);
void display_centered_text(uint8_t y, const char *text);
void render_display();
void buzzer_beep(uint duration_ms);
bool read_button_debounced(uint gpio);
int8_t read_joystick_direction();
void add_reminder(uint8_t hour, uint8_t minute, const char *name);
void show_startup_message();

// ================= IMPLEMENTAÇÃO DAS FUNÇÕES =================

// Inicialização do hardware
void hardware_init() {
    stdio_init_all();
    
    // Buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    
    // Botões
    gpio_init(BUTTON_CONFIRM);
    gpio_set_dir(BUTTON_CONFIRM, GPIO_IN);
    gpio_pull_up(BUTTON_CONFIRM);
    
    gpio_init(BUTTON_SNOOZE);
    gpio_set_dir(BUTTON_SNOOZE, GPIO_IN);
    gpio_pull_up(BUTTON_SNOOZE);
    
    // Joystick (ADC)
    adc_init();
    adc_gpio_init(26); // VRX_PIN
    adc_gpio_init(27); // VRY_PIN
    
    // Display OLED
    display_init();
}

// Inicialização do display
void display_init() {
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    ssd1306_init();
    calculate_render_area_buffer_length(&display_area);
    clear_display();
}

// Limpa o display
void clear_display() {
    memset(display_buffer, 0, ssd1306_buffer_length);
    render_display();
}

// Renderiza o buffer no display
void render_display() {
    render_on_display(display_buffer, &display_area);
}

// Exibe texto em posição específica
void display_text(uint8_t x, uint8_t y, const char *text) {
    uint8_t max_chars = (ssd1306_width - x) / 6; // 6 pixels por caractere
    char buffer[MAX_LENGTH];
    
    strncpy(buffer, text, max_chars);
    buffer[max_chars] = '\0';
    
    ssd1306_draw_string(display_buffer, x, y, buffer);
}

// Exibe texto centralizado
void display_centered_text(uint8_t y, const char *text) {
    uint8_t x = (ssd1306_width - (strlen(text) * 6)) / 2;
    if (x < 0) x = 0;
    display_text(x, y, text);
}

// Mensagem inicial
void show_startup_message() {
    if(xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
        clear_display();
        display_centered_text(15, "SISTEMA DE");
        display_centered_text(30, "LEMBRETES");
        display_centered_text(45, "MEDICAMENTOS");
        render_display();
        xSemaphoreGive(xDisplayMutex);
    }
}

// Exibe o menu atual
void display_menu() {
    clear_display();
    
    switch(currentMenu) {
        case MENU_MAIN:
            display_centered_text(10, "LEMBRETES MED");
            display_centered_text(25, "1-ADICIONAR");
            display_centered_text(40, "2-VER LEMBRETES");
            break;
            
        case MENU_ADD_REMINDER: {
            char time_str[10];
            snprintf(time_str, sizeof(time_str), "%02d:%02d", selected_hour, selected_minute);
            display_centered_text(10, "NOVO LEMBRETE");
            display_centered_text(30, time_str);
            display_centered_text(50, "CONFIRME: BOTAO A");
            break;
        }
            
        case MENU_VIEW_REMINDERS:
            if(reminderCount == 0) {
                display_centered_text(20, "SEM LEMBRETES");
                display_centered_text(40, "VOLTAR: BOTAO A");
            } else {
                for(int i = 0; i < reminderCount && i < 3; i++) {
                    char line[20];
                    snprintf(line, sizeof(line), "%02d:%02d %s", 
                            reminders[i].hour, reminders[i].minute, reminders[i].med_name);
                    if(strlen(line) > 16) line[16] = '\0';
                    display_text(5, 15 + (i * 15), line);
                }
                display_centered_text(55, "VOLTAR: BOTAO A");
            }
            break;
    }
    
    render_display();
}

// Exibe alerta de medicamento
void display_alert(const char *med_name) {
    clear_display();
    display_centered_text(10, "ALERTA!");
    
    char line1[20];
    snprintf(line1, sizeof(line1), "TOMAR: %s", med_name);
    display_centered_text(25, line1);
    
    display_centered_text(40, "CONFIRMAR: BOTAO A");
    display_centered_text(55, "ADIAR: BOTAO B");
    
    render_display();
}

// Tarefa de Interface do Usuário
void vInterfaceTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    char med_name[MAX_LENGTH] = "MEDICAMENTO";
    
    while(1) {
        int8_t joy_dir = read_joystick_direction();
        
        switch(currentMenu) {
            case MENU_MAIN:
                if(joy_dir == 1) currentMenu = MENU_ADD_REMINDER;
                else if(joy_dir == -1) currentMenu = MENU_VIEW_REMINDERS;
                break;
                
            case MENU_ADD_REMINDER:
                if(joy_dir == 1) selected_hour = (selected_hour + 1) % 24;
                else if(joy_dir == -1) selected_hour = (selected_hour - 1 + 24) % 24;
                else if(joy_dir == 2) selected_minute = (selected_minute + 1) % 60;
                else if(joy_dir == -2) selected_minute = (selected_minute - 1 + 60) % 60;
                
                if(read_button_debounced(BUTTON_CONFIRM)) {
                    add_reminder(selected_hour, selected_minute, med_name);
                    currentMenu = MENU_MAIN;
                }
                break;
                
            case MENU_VIEW_REMINDERS:
                if(read_button_debounced(BUTTON_CONFIRM)) {
                    currentMenu = MENU_MAIN;
                }
                break;
        }
        
        if(xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
            display_menu();
            xSemaphoreGive(xDisplayMutex);
        }
        
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}

// Tarefa de Alerta
void vAlertTask(void *pvParameters) {
    MedicationReminder received_reminder;
    
    while(1) {
        if(xQueueReceive(xReminderQueue, &received_reminder, portMAX_DELAY) == pdPASS) {
            currentMenu = MENU_ALERT;
            
            for(int i = 0; i < 5; i++) {
                buzzer_beep(200);
                vTaskDelay(pdMS_TO_TICKS(200));
                
                if(xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
                    display_alert(received_reminder.med_name);
                    xSemaphoreGive(xDisplayMutex);
                }
                
                if(read_button_debounced(BUTTON_CONFIRM)) break;
                if(read_button_debounced(BUTTON_SNOOZE)) {
                    received_reminder.minute += 5;
                    if(received_reminder.minute >= 60) {
                        received_reminder.minute -= 60;
                        received_reminder.hour = (received_reminder.hour + 1) % 24;
                    }
                    xQueueSend(xReminderQueue, &received_reminder, 0);
                    break;
                }
            }
            
            currentMenu = MENU_MAIN;
        }
    }
}

// Tarefa de Gerenciamento de Tempo
void vTimeManagerTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while(1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(60000));
        
        // Simulação - dispara todos os lembretes a cada minuto
        // (Substituir por verificação de horário real em produção)
        for(int i = 0; i < reminderCount; i++) {
            xQueueSend(xReminderQueue, &reminders[i], 0);
        }
    }
}

// Funções auxiliares
void buzzer_beep(uint duration_ms) {
    gpio_put(BUZZER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_put(BUZZER_PIN, 0);
}

bool read_button_debounced(uint gpio) {
    static uint32_t last_time[16] = {0};
    if(!gpio_get(gpio)) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if((now - last_time[gpio]) > 50) {
            last_time[gpio] = now;
            return true;
        }
    }
    return false;
}

int8_t read_joystick_direction() {
    uint16_t x_raw, y_raw;
    adc_select_input(JOYSTICK_X_ADC);
    x_raw = adc_read();
    adc_select_input(JOYSTICK_Y_ADC);
    y_raw = adc_read();
    
    if(x_raw < DEADZONE) return 2;       // Direita
    if(x_raw > 4095 - DEADZONE) return -2; // Esquerda
    if(y_raw < DEADZONE) return 1;       // Cima
    if(y_raw > 4095 - DEADZONE) return -1; // Baixo
    
    return 0;
}

void add_reminder(uint8_t hour, uint8_t minute, const char *name) {
    if(reminderCount < 5) {
        reminders[reminderCount].hour = hour;
        reminders[reminderCount].minute = minute;
        strncpy(reminders[reminderCount].med_name, name, MAX_LENGTH-1);
        reminders[reminderCount].med_name[MAX_LENGTH-1] = '\0';
        reminderCount++;
    }
}

// Função principal
int main() {
    hardware_init();
    
    // Cria recursos do FreeRTOS
    xReminderQueue = xQueueCreate(5, sizeof(MedicationReminder));
    xDisplayMutex = xSemaphoreCreateMutex();
    
    // Mensagem inicial
    show_startup_message();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Cria tarefas
    xTaskCreate(vInterfaceTask, "Interface", 256, NULL, 2, NULL);
    xTaskCreate(vAlertTask, "Alert", 256, NULL, 3, NULL);
    xTaskCreate(vTimeManagerTask, "TimeManager", 256, NULL, 1, NULL);
    
    // Inicia scheduler
    vTaskStartScheduler();
    
    while(1);
}
