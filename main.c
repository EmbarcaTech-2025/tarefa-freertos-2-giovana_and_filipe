// === Includes e Definições ===
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"
#include <stdio.h>
#include <string.h>

#define SDA_PIN 14
#define SCL_PIN 15
#define BUZZER_PIN 21
#define BUTTON_A 5
#define BUTTON_B 6
#define JOY_X 26
#define JOY_Y 27
#define DEADZONE 1000
#define MAX_REMINDERS 5
#define MAX_NAME_LEN 16

// === Tipos ===
typedef enum {
    MENU_WAIT_START,
    MENU_HOME,
    MENU_ADD,
    MENU_LIST,
    MENU_ALERT
} Menu;

typedef struct {
    uint8_t hour, minute;
    char name[MAX_NAME_LEN];
} Reminder;

// === Globais ===
Menu menu = MENU_WAIT_START;
Reminder reminders[MAX_REMINDERS];
int count = 0;
uint8_t sel_hour = 12, sel_min = 0;
QueueHandle_t qReminders;
SemaphoreHandle_t dispMutex;

// === Função utilitária de mensagem no display ===
void display_message(const char* line1, const char* line2) {
    uint8_t buffer[ssd1306_buffer_length];
    struct render_area area = {
        .start_column = 0,
        .end_column = ssd1306_width - 1,
        .start_page = 0,
        .end_page = ssd1306_n_pages - 1
    };
    calculate_render_area_buffer_length(&area);

    memset(buffer, 0, sizeof buffer);

    int x1 = (ssd1306_width - strlen(line1) * 6) / 2;
    int x2 = (ssd1306_width - strlen(line2) * 6) / 2;

    ssd1306_draw_string(buffer, x1, 20, line1);
    ssd1306_draw_string(buffer, x2, 40, line2);

    render_on_display(buffer, &area);
}

// === Funções Display ===
void disp_menu() {
    uint8_t buffer[ssd1306_buffer_length];
    struct render_area area = {0, ssd1306_width - 1, 0, ssd1306_n_pages - 1};
    calculate_render_area_buffer_length(&area);
    memset(buffer, 0, sizeof buffer);

    switch(menu) {
        case MENU_HOME:
            ssd1306_draw_string(buffer, 5, 5, "LEMBRETES MED");
            ssd1306_draw_string(buffer, 5, 20, "A ADICIONAR");
            ssd1306_draw_string(buffer, 5, 35, "B VER LEMBRETES");
            break;

        case MENU_ADD: {
            char time[10];
            snprintf(time, sizeof time, "%02d:%02d", sel_hour, sel_min);
            ssd1306_draw_string(buffer, 5, 5, "NOVO LEMBRETE");
            ssd1306_draw_string(buffer, 5, 20, time);
            ssd1306_draw_string(buffer, 5, 35, "CONFIRME BOTAO A");
            break;
        }

        case MENU_LIST:
            if (count == 0) {
                ssd1306_draw_string(buffer, 5, 25, "SEM LEMBRETES");
            } else {
                for (int i = 0; i < count && i < 3; i++) {
                    char line[22];
                    snprintf(line, sizeof line, "%02d:%02d %s", reminders[i].hour, reminders[i].minute, reminders[i].name);
                    ssd1306_draw_string(buffer, 0, 10 + i * 15, line);
                }
            }
            ssd1306_draw_string(buffer, 5, 55, "VOLTAR: BOTAO A");
            break;
        default:
            break;
    }
    render_on_display(buffer, &area);
}

void disp_alert(const char* name) {
    uint8_t buffer[ssd1306_buffer_length];
    struct render_area area = {0, ssd1306_width - 1, 0, ssd1306_n_pages - 1};
    calculate_render_area_buffer_length(&area);
    memset(buffer, 0, sizeof buffer);

    char msg[22];
    snprintf(msg, sizeof msg, "TOMAR: %s", name);
    ssd1306_draw_string(buffer, 10, 10, "ALERTA!");
    ssd1306_draw_string(buffer, 10, 30, msg);
    ssd1306_draw_string(buffer, 10, 50, "A: OK | B: Adiar");

    render_on_display(buffer, &area);
}

// === Botões e Joystick ===
bool btn_press(uint pin) {
    static uint32_t last[30] = {0};
    if (!gpio_get(pin)) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last[pin] > 200) {
            last[pin] = now;
            return true;
        }
    }
    return false;
}

int8_t joy_dir() {
    adc_select_input(0);
    uint16_t x = adc_read();
    adc_select_input(1);
    uint16_t y = adc_read();

    if (y < DEADZONE) return 1;     // cima
    if (y > 4000) return -1;        // baixo
    if (x < DEADZONE) return 2;     // direita
    if (x > 4000) return -2;        // esquerda
    return 0;
}

// === Hardware ===
void init_hw() {
    stdio_init_all();
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);

    adc_init();
    adc_gpio_init(JOY_X);
    adc_gpio_init(JOY_Y);

    i2c_init(i2c1, 100000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    ssd1306_init();
}

void beep(uint t) {
    gpio_put(BUZZER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(t));
    gpio_put(BUZZER_PIN, 0);
}

// === Tarefas ===
void vUI(void* p) {
    TickType_t last = xTaskGetTickCount();
    while (1) {
        switch(menu) {
            case MENU_WAIT_START:
                display_message("SISTEMA DE", "LEMBRETES");
                vTaskDelay(pdMS_TO_TICKS(2000));
                display_message("PRESSIONE A", "PARA INICIAR");
                while (!btn_press(BUTTON_A)) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                menu = MENU_HOME;
                break;

            case MENU_HOME:
                disp_menu();
                if (btn_press(BUTTON_A)) {
                    menu = MENU_ADD;
                } else if (btn_press(BUTTON_B)) {
                    menu = MENU_LIST;
                }
                break;

            case MENU_ADD: {
                int8_t dir = joy_dir();
                if (dir == 1) sel_hour = (sel_hour + 1) % 24;
                else if (dir == -1) sel_hour = (sel_hour + 23) % 24;
                else if (dir == 2) sel_min = (sel_min + 1) % 60;
                else if (dir == -2) sel_min = (sel_min + 59) % 60;

                if (btn_press(BUTTON_A) && count < MAX_REMINDERS) {
                    reminders[count++] = (Reminder){sel_hour, sel_min, "MEDICAMENTO"};
                    menu = MENU_HOME;
                }
                disp_menu();
                break;
            }

            case MENU_LIST:
                disp_menu();
                if (btn_press(BUTTON_A)) {
                    menu = MENU_HOME;
                }
                break;

            case MENU_ALERT:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}

void vAlert(void* p) {
    Reminder rcv;
    while (1) {
        if (xQueueReceive(qReminders, &rcv, portMAX_DELAY)) {
            menu = MENU_ALERT;
            for (int i = 0; i < 5; i++) {
                beep(100);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            while (1) {
                disp_alert(rcv.name);
                if (btn_press(BUTTON_A)) break;
                if (btn_press(BUTTON_B)) {
                    rcv.minute += 5;
                    if (rcv.minute >= 60) {
                        rcv.minute -= 60;
                        rcv.hour = (rcv.hour + 1) % 24;
                    }
                    xQueueSend(qReminders, &rcv, 0);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            menu = MENU_HOME;
        }
    }
}

void vClock(void* p) {
    TickType_t last = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(60000));
        for (int i = 0; i < count; i++) {
            xQueueSend(qReminders, &reminders[i], 0);
        }
    }
}

int main() {
    init_hw();
    dispMutex = xSemaphoreCreateMutex();
    qReminders = xQueueCreate(5, sizeof(Reminder));
    xTaskCreate(vUI, "UI", 2048, NULL, 3, NULL);
    xTaskCreate(vAlert, "Alert", 1024, NULL, 2, NULL);
    xTaskCreate(vClock, "Clock", 1024, NULL, 1, NULL);
    vTaskStartScheduler();
    while (1);
}
