/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/ip4_addr.h"

#include "FreeRTOS.h"
#include "task.h"
#include "ping.h"
#include "message_buffer.h"

#include "hardware/gpio.h"
#include "hardware/adc.h"

#define mbaTASK_MESSAGE_BUFFER_SIZE (60)

#ifndef PING_ADDR
#define PING_ADDR "142.251.35.196"
#endif
#ifndef RUN_FREERTOS_ON_CORE
#define RUN_FREERTOS_ON_CORE 0
#endif

#define TEST_TASK_PRIORITY (tskIDLE_PRIORITY + 1UL)

static MessageBufferHandle_t xControlMessageBuffer;
static MessageBufferHandle_t xMovingAvgMessageBuffer;
static MessageBufferHandle_t xPrintSimpleMessageBuffer;
static MessageBufferHandle_t xPrintMovingMessageBuffer;

float read_onboard_temperature()
{

    /* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
    const float conversionFactor = 3.3f / (1 << 12);

    float adc = (float)adc_read() * conversionFactor;
    float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

    return tempC;
}

void main_task(__unused void *params)
{
    if (cyw43_arch_init())
    {
        printf("failed to initialise\n");
        return;
    }
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000))
    {
        printf("failed to connect.\n");
        exit(1);
    }
    else
    {
        printf("Connected.\n");
    }

    ip_addr_t ping_addr;
    ipaddr_aton(PING_ADDR, &ping_addr);
    ping_init(&ping_addr);

    while (true)
    {
        // not much to do as LED is in another task, and we're using RAW (callback) lwIP API
        vTaskDelay(100);
    }

    cyw43_arch_deinit();
}

/* A Task that obtains the data every 1000 ticks from the inbuilt temperature sensor (RP2040), prints it out and sends it to simpleavg_task via message buffer */
void temp_task(__unused void *params)
{
    float temperature = 0.0;

    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    while (true)
    {
        vTaskDelay(1000); // 1000 ticks = 1 second
        temperature = read_onboard_temperature();
        printf("Onboard temperature = %.02f C\n", temperature);
        xMessageBufferSend(
            xControlMessageBuffer, /* The message buffer to write to. */
            (void *)&temperature,  /* The source of the data to send. */
            sizeof(temperature),   /* The length of the data to send. */
            0);                    /* Do not block, should the buffer be full. */
        xMessageBufferSend(
            xMovingAvgMessageBuffer, /* The message buffer to write to. */
            (void *)&temperature,    /* The source of the data to send. */
            sizeof(temperature),     /* The length of the data to send. */
            0);                      /* Do not block, should the buffer be full. */
    }
}

void movingavg_task(__unused void *params)
{
    float fReceivedData;
    float sum = 0;
    size_t xReceivedBytes;

    static float data[10] = {0};
    static int index = 0;
    static int count = 0;

    while (true)
    {
        xReceivedBytes = xMessageBufferReceive(
            xMovingAvgMessageBuffer, /* The message buffer to receive from. */
            (void *)&fReceivedData,  /* Location to store received data. */
            sizeof(fReceivedData),   /* Maximum number of bytes to receive. */
            portMAX_DELAY);          /* Wait indefinitely */

        sum -= data[index];          // Subtract the oldest element from sum
        data[index] = fReceivedData; // Assign the new element to the data
        sum += fReceivedData;        // Add the new element to sum

        index = (index + 1) % 10; // go back to index 0 once it reaches 9, the last element
        if (count < 10)
            count++;

        float movingAverage = sum / count;

        // printf("Moving average = %.02f C\n", movingAverage);

        // Send the moving average to the print task
        xMessageBufferSend(
            xPrintMovingMessageBuffer,
            (void *)&movingAverage,
            sizeof(movingAverage),
            portMAX_DELAY);
    }
}

void simpleavg_task(__unused void *params)
{
    float fReceivedData;
    float sum = 0;
    size_t xReceivedBytes;
    int count = 0;

    while (true)
    {
        xReceivedBytes = xMessageBufferReceive(
            xControlMessageBuffer,  /* The message buffer to receive from. */
            (void *)&fReceivedData, /* Location to store received data. */
            sizeof(fReceivedData),  /* Maximum number of bytes to receive. */
            portMAX_DELAY);         /* Wait indefinitely */

        // Update the sum and count
        sum += fReceivedData;
        count++;

        // Calculate the simple average
        float simpleAverage = sum / count;

        // printf("Simple average = %.02f C\n", simpleAverage);

        // Send the moving average to the print task
        xMessageBufferSend(
            xPrintSimpleMessageBuffer,
            (void *)&simpleAverage,
            sizeof(simpleAverage),
            portMAX_DELAY);
    }
}

void printAvg_task(__unused void *params)
{
    float receivedAverage;
    float receivedMovingAverage;

    while (true)
    {
        xMessageBufferReceive(
            xPrintSimpleMessageBuffer, /* The message buffer to receive from. */
            (void *)&receivedAverage,  /* Location to store received data. */
            sizeof(receivedAverage),   /* Maximum number of bytes to receive. */
            portMAX_DELAY);            /* Wait indefinitely */

        printf("Simple Average = %0.2f\n", receivedAverage);

        xMessageBufferReceive(
            xPrintMovingMessageBuffer,      /* The message buffer to receive from. */
            (void *)&receivedMovingAverage, /* Location to store received data. */
            sizeof(receivedMovingAverage),  /* Maximum number of bytes to receive. */
            portMAX_DELAY);                 /* Wait indefinitely */

        printf("Moving Average = %0.2f\n", receivedMovingAverage);
    }
}

void vLaunch(void)
{
    TaskHandle_t task;
    xTaskCreate(main_task, "TestMainThread", configMINIMAL_STACK_SIZE, NULL, TEST_TASK_PRIORITY, &task);
    TaskHandle_t temptask;
    xTaskCreate(temp_task, "TestTempThread", configMINIMAL_STACK_SIZE, NULL, 8, &temptask);
    TaskHandle_t simpleavgtask;
    xTaskCreate(simpleavg_task, "TestAvgThread", configMINIMAL_STACK_SIZE, NULL, 5, &simpleavgtask);
    TaskHandle_t movingavgtask;
    xTaskCreate(movingavg_task, "TestMovingAvgThread", configMINIMAL_STACK_SIZE, NULL, 5, &movingavgtask);
    TaskHandle_t printtask;
    xTaskCreate(printAvg_task, "TestPrintAvgThread", configMINIMAL_STACK_SIZE, NULL, 5, &printtask);

    xControlMessageBuffer = xMessageBufferCreate(mbaTASK_MESSAGE_BUFFER_SIZE);
    xMovingAvgMessageBuffer = xMessageBufferCreate(mbaTASK_MESSAGE_BUFFER_SIZE);
    xPrintMovingMessageBuffer = xMessageBufferCreate(mbaTASK_MESSAGE_BUFFER_SIZE);
    xPrintSimpleMessageBuffer = xMessageBufferCreate(mbaTASK_MESSAGE_BUFFER_SIZE);

#if NO_SYS && configUSE_CORE_AFFINITY && configNUM_CORES > 1
    // we must bind the main task to one core (well at least while the init is called)
    // (note we only do this in NO_SYS mode, because cyw43_arch_freertos
    // takes care of it otherwise)
    vTaskCoreAffinitySet(task, 1);
#endif

    /* Start the tasks and timer running. */
    vTaskStartScheduler();
}

int main(void)
{
    stdio_init_all();

    /* Configure the hardware ready to run the demo. */
    const char *rtos_name;
#if (portSUPPORT_SMP == 1)
    rtos_name = "FreeRTOS SMP";
#else
    rtos_name = "FreeRTOS";
#endif

#if (portSUPPORT_SMP == 1) && (configNUM_CORES == 2)
    printf("Starting %s on both cores:\n", rtos_name);
    vLaunch();
#elif (RUN_FREERTOS_ON_CORE == 1)
    printf("Starting %s on core 1:\n", rtos_name);
    multicore_launch_core1(vLaunch);
    while (true)
        ;
#else
    printf("Starting %s on core 0:\n", rtos_name);
    vLaunch();
#endif
    return 0;
}