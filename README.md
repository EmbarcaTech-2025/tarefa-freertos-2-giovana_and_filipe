
# Tarefa: Roteiro de FreeRTOS #2 - EmbarcaTech 2025

Autor: **Filipe Alves de Sousa e Giovana Ferreira Santos**

Curso: Residência Tecnológica em Sistemas Embarcados

Instituição: EmbarcaTech - HBr

Brasília, Junho de 2025

---

# Sistema de Lembretes de Medicamentos com FreeRTOS

Este projeto implementa um sistema de lembretes de medicamentos usando o Raspberry Pi Pico com o display OLED, botões, joystick e buzzer disponíveis na **placa BitDogLab**, utilizando o **FreeRTOS** para gerenciamento das tarefas.

## 🎯 Objetivo
Criar uma aplicação que utilize:
- Periféricos da placa BitDogLab
- Recursos do FreeRTOS: tarefas, filas, semáforos e delays
- Interface interativa para adicionar e visualizar lembretes

## ⚙️ Funcionalidades
- Tela inicial com introdução e instrução de início
- Menu principal com opções:
  - A - Adicionar novo lembrete
  - B - Ver lembretes salvos
- Joystick para ajustar hora e minuto
- Alarme com buzzer no horário de cada lembrete
- Opção de confirmar (botão A) ou adiar 5 minutos (botão B)

## 🧩 Periféricos utilizados
- **Display OLED** (via I2C)
- **Joystick** (via ADC nos pinos 26 e 27)
- **Botões A e B** (GPIO 5 e 6)
- **Buzzer** (GPIO 21)

## 🧵 Tarefas FreeRTOS
| Tarefa  | Função |
|--------|--------|
| `vUI` | Interface com o usuário, leitura de botões e joystick |
| `vAlert` | Exibe alertas e controla o buzzer |
| `vClock` | Simula o relógio, dispara lembretes na hora marcada |

## 🔄 Recursos do FreeRTOS utilizados
- `xTaskCreate()`
- `vTaskDelay()` e `vTaskDelayUntil()`
- `xQueueCreate()`, `xQueueSend()`, `xQueueReceive()`
- `xSemaphoreCreateMutex()`

## 📸 Demonstração
Inclua aqui:
- **Vídeo curto** mostrando o funcionamento do sistema


## 📁 Estrutura do projeto
```
├── CMakeLists.txt
├── src/
│   └── main.c
├── inc/
│   └── ssd1306.h
├── include/
│   └── FreeRTOSConfig.h
├── build/
└── README.md
```

---

## 📜 Licença
GNU GPL-3.0.
