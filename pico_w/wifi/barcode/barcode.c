#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/time.h"
#include <stdbool.h>

// GPIO Definitions
#define ADC_PIN 26 // GPIO 26 (ADC0)

// Thresholds and Timing
#define THRESHOLD 1800        // ADC midpoint (0-4095 for 12-bit ADC)
#define MAX_ELEMENTS 9        // Each character in the barcode has 9 elements (5 bars and 4 spaces)
#define MAX_BARCODE_LENGTH 90 // Maximum barcode length (10 characters)

char decoded_barcode[MAX_BARCODE_LENGTH];
int decoded_length = 0;
bool scanning_completed = false;

// Element Structure. Each scanned element is classified as either a bar or a space.
typedef struct
{
    float duration_ms; // Duration in milliseconds
    bool is_wide;      // Wide (1) or Narrow (0)
} Element;

// Array to store elements
Element elements[MAX_ELEMENTS];
int element_count = 0;

// Function to initialize ADC
void init_adc_sensor()
{
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(0); // ADC0
}

// Code 39 Patterns and Characters
typedef struct
{
    const int pattern[9]; // Array of 0s and 1s representing narrow and wide elements
    char character;       // Corresponding character
} Code39Mapping;

// Complete Code 39 Mapping with provided patterns
const Code39Mapping code39_mappings[] = {
    {{0, 1, 0, 0, 1, 0, 1, 0, 0}, '*'},
    {{1, 0, 0, 0, 0, 1, 0, 0, 1}, 'A'},
    {{0, 0, 1, 0, 0, 1, 0, 0, 1}, 'B'},
    {{1, 0, 1, 0, 0, 1, 0, 0, 0}, 'C'},
    {{0, 0, 0, 0, 1, 1, 0, 0, 1}, 'D'},
    {{1, 0, 0, 0, 1, 1, 0, 0, 0}, 'E'},
    {{0, 0, 1, 0, 1, 1, 0, 0, 0}, 'F'},
    {{0, 0, 0, 0, 0, 1, 1, 0, 1}, 'G'},
    {{1, 0, 0, 0, 0, 1, 1, 0, 0}, 'H'},
    {{0, 0, 1, 0, 0, 1, 1, 0, 0}, 'I'},
    {{0, 0, 0, 0, 1, 1, 1, 0, 0}, 'J'},
    {{1, 0, 0, 0, 0, 0, 0, 1, 1}, 'K'},
    {{0, 0, 1, 0, 0, 0, 0, 1, 1}, 'L'},
    {{1, 0, 1, 0, 0, 0, 0, 1, 0}, 'M'},
    {{0, 0, 0, 0, 1, 0, 0, 1, 1}, 'N'},
    {{1, 0, 0, 0, 1, 0, 0, 1, 0}, 'O'},
    {{0, 0, 1, 0, 1, 0, 0, 1, 0}, 'P'},
    {{0, 0, 0, 0, 0, 0, 1, 1, 1}, 'Q'},
    {{1, 0, 0, 0, 0, 0, 1, 1, 0}, 'R'},
    {{0, 0, 1, 0, 0, 0, 1, 1, 0}, 'S'},
    {{0, 0, 0, 0, 1, 0, 1, 1, 0}, 'T'},
    {{1, 1, 0, 0, 0, 0, 0, 0, 1}, 'U'},
    {{0, 1, 1, 0, 0, 0, 0, 0, 1}, 'V'},
    {{1, 1, 1, 0, 0, 0, 0, 0, 0}, 'W'},
    {{0, 1, 0, 0, 1, 0, 0, 0, 1}, 'X'},
    {{1, 1, 0, 0, 1, 0, 0, 0, 0}, 'Y'},
    {{0, 1, 1, 0, 1, 0, 0, 0, 0}, 'Z'},
    {{0, 1, 0, 0, 0, 0, 1, 0, 1}, '-'},
    {{1, 1, 0, 0, 0, 0, 1, 0, 0}, '.'},
    {{0, 1, 0, 1, 0, 1, 0, 0, 0}, '$'},
    {{0, 1, 0, 1, 0, 0, 0, 1, 0}, '/'},
    {{0, 1, 0, 0, 0, 1, 0, 1, 0}, '+'},
    {{0, 0, 0, 1, 0, 1, 0, 1, 0}, '%'},
    {{0, 0, 0, 1, 1, 0, 1, 0, 0}, '0'},
    {{1, 0, 0, 1, 0, 0, 0, 0, 1}, '1'},
    {{0, 0, 1, 1, 0, 0, 0, 0, 1}, '2'},
    {{1, 0, 1, 1, 0, 0, 0, 0, 0}, '3'},
    {{0, 0, 0, 1, 1, 0, 0, 0, 1}, '4'},
    {{1, 0, 0, 1, 1, 0, 0, 0, 0}, '5'},
    {{0, 0, 1, 1, 1, 0, 0, 0, 0}, '6'},
    {{0, 0, 0, 1, 0, 0, 1, 0, 1}, '7'},
    {{1, 0, 0, 1, 0, 0, 1, 0, 0}, '8'},
    {{0, 0, 1, 1, 0, 0, 1, 0, 0}, '9'}};

const int num_mappings = sizeof(code39_mappings) / sizeof(code39_mappings[0]);

// Function that compares a group of 9 elements against all mappings to find a matching character.
char decode_character(Element *elements_group)
{
    // Create a temporary pattern array
    int temp_pattern[9];
    for (int i = 0; i < 9; i++)
    {
        temp_pattern[i] = elements_group[i].is_wide ? 1 : 0;
    }

    // Compare with each mapping
    for (int i = 0; i < num_mappings; i++)
    {
        bool match = true;
        for (int j = 0; j < 9; j++)
        {
            if (temp_pattern[j] != code39_mappings[i].pattern[j])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return code39_mappings[i].character;
        }
    }
    return '?'; // Return '?' if no match is found
}

// Function to classify elements as narrow or wide
void classify_elements(Element *elements, int count)
{
    // Find the shortest
    float min_duration = elements[0].duration_ms;

    for (int i = 1; i < count; i++)
    {
        if (elements[i].duration_ms < min_duration)
        {
            min_duration = elements[i].duration_ms; // shortest duration
        }
    }

    // Classify elements based on the shortest duration
    for (int i = 0; i < count; i++)
    {
        if (elements[i].duration_ms >= min_duration * 3) // wide element is 3 to 3.5 times longer than the narrow element
        {
            elements[i].is_wide = true; // Narrow element
        }
        else
        {
            elements[i].is_wide = false; // Wide element
        }
    }
}

// Function to measure pulse widths and storing them in an array to later classify them as narrow or wide. Short duration would classify as narrow and long duration would classify as wide.
void measure_pulse_width(uint gpio_pin)
{
    uint32_t start_time, end_time;
    uint32_t pulse_width = 0;

    // Detect initial state at startup
    bool initial_state = gpio_get(gpio_pin);

    // Start timer when we see a transition
    if (initial_state)
    {
        start_time = time_us_32();
    }

    // Keep track of the number of transitions
    int transitions = 0;

    while (transitions < MAX_ELEMENTS * 2) // Wait for 18 transitions (9 elements * 2 states per element)
    {
        bool current_state = gpio_get(gpio_pin);

        if (current_state != initial_state)
        {
            // Transition detected
            if (start_time != 0)
            {
                end_time = time_us_32();
                pulse_width = end_time - start_time;

                printf("Pulse width: %d us\n", pulse_width);

                // Store the timing in the array
                elements[element_count].duration_ms = pulse_width / 1000.0f; // Convert to milliseconds

                element_count++;

                if (element_count >= MAX_ELEMENTS)
                {
                    element_count = 0; // Reset the counter if we reach the maximum
                }
            }

            // Reset timer and initial state
            start_time = time_us_32();
            transitions++;
            initial_state = !initial_state;
        }
    }
}

int main()
{
    // Initialize standard IO
    stdio_init_all();

    // Initialize ADC
    init_adc_sensor();

    printf("Scanning for barcodes...\n");

    // Initialize timing
    absolute_time_t last_transition_time = get_absolute_time();
    bool previous_state = true;    // Starting outside the barcode
    bool scanning_started = false; // Flag to indicate if scanning has started

    while (1)
    {
        // Read sensor
        uint16_t sensor_value = adc_read();
        printf("Sensor Value: %d\n", sensor_value);

        // Check for barcode start
        if (!scanning_started && sensor_value > THRESHOLD)
        {
            scanning_started = true;
            printf("Barcode scanning started!\n");
            scanning_completed = false;
            element_count = 0;
        }

        if (scanning_started)
        {
            // Measure pulse widths
            measure_pulse_width(ADC_PIN);

            // Wait until scanning is completed
            while (!scanning_completed)
            {
                tight_loop_contents();
            }

            // Classify elements
            classify_elements(elements, element_count);

            // Decode character
            char decoded_char = decode_character(elements);
            printf("Decoded character: %c\n", decoded_char);

            // Reset for next character
            element_count = 0;
            scanning_completed = false;

            // Check if we've reached the end of the barcode
            if (decoded_char == '*')
            { //'*' marks the end of the barcode
                printf("Barcode scan completed.\n");
                scanning_started = false;
            }
        }
    }

    return 0;
}
