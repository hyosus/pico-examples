
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/time.h"
#include <stdbool.h>
// GPIO Definitions
#define ADC_PIN 26 // GPIO 26 (ADC0)
#define DIGI_PIN 0
// Thresholds and Timing
#define THRESHOLD 1000        // ADC midpoint (0-4095 for 12-bit ADC)
#define MAX_ELEMENTS 27       // Each character in the barcode has 9 elements (5 bars and 4 spaces)
#define MAX_BARCODE_LENGTH 90 // Maximum barcode length (10 characters)
#define DEBOUNCE_TIME_US 500  // Debounce time in us
#define SAMPLE_SIZE 200

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

int element_widths[MAX_ELEMENTS];

uint16_t ADC_values[SAMPLE_SIZE];
uint16_t HIGH_THRESHOLD, LOW_THRESHOLD;

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

// Function to initialize ADC
void init_adc_sensor()
{
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(0); // ADC0
}

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

void measure_pulse_width_adc(uint adc_pin)
{
    uint32_t start_time, end_time;
    uint32_t active_pulse_width = 0, inactive_pulse_width = 0;
    // Initialize ADC
    adc_init();
    adc_gpio_init(adc_pin);
    adc_select_input(0);
    // Detect initial state (active or inactive) at startup
    uint16_t initial_value = adc_read();
    bool initial_state = initial_value > THRESHOLD; // 1 for HIGH (dark), 0 for LOW (white)
    // Start timer when we see a transition from inactive to active
    if (!initial_state)
    {
        start_time = time_us_32();
    }
    while (element_count < MAX_ELEMENTS) // Wait for 9 elements
    {
        uint16_t current_value = adc_read();
        bool current_state = current_value > THRESHOLD;
        // Debounce logic: check if there’s a transition
        if (current_state != initial_state)
        {
            sleep_us(DEBOUNCE_TIME_US); // Apply debounce delay
            // Re-check after debounce delay to confirm the state
            current_value = adc_read();
            current_state = current_value > THRESHOLD;
            // Confirm transition if the state is still different
            if (current_state != initial_state)
            {
                if (start_time != 0)
                {
                    end_time = time_us_32();
                    uint32_t pulse_width = end_time - start_time;
                    if (current_state == false)
                    {
                        printf("Active pulse: %f ms\n", pulse_width / 1000.0f);
                        active_pulse_width += pulse_width;
                        elements[element_count].duration_ms = pulse_width / 1000.0f;
                        element_count++;
                    }
                    else // Even number of transitions means it's an inactive pulse
                    {
                        printf("Inactive pulse: %f ms\n", pulse_width / 1000.0f);
                        inactive_pulse_width += pulse_width;
                        elements[element_count].duration_ms = pulse_width / 1000.0f;
                        element_count++;
                    }
                }
                start_time = time_us_32();
                initial_state = current_state;
            }
        }
    }
}

void collect_data()
{
    for (int i = 0; i < SAMPLE_SIZE; i++)
    {
        adc_select_input(0);
        uint16_t sensor_value = adc_read();
        ADC_values[i] = sensor_value;
        printf("Sensor value: %d\n", sensor_value);
        sleep_ms(100);
    }
}

void find_threshold()
{
    uint16_t max = 0;
    uint16_t min = 4095;

    for (int i = 0; i < SAMPLE_SIZE; i++)
    {
        if (ADC_values[i] > max)
        {
            max = ADC_values[i];
        }
        if (ADC_values[i] < min)
        {
            min = ADC_values[i];
        }
    }

    HIGH_THRESHOLD = max * 0.72; // 72% of max value
    LOW_THRESHOLD = min * 1.2;   // 120% of min value
}

void find_width()
{
    int high_count = 0;
    int element_count = 0;

    for (int i = 0; i < SAMPLE_SIZE; i++)
    {
        if (ADC_values[i] >= HIGH_THRESHOLD)
        {
            high_count++;
            printf("Count: %d\n", high_count);
            for (int j = i + 1; j < SAMPLE_SIZE; j++)
            {
                if (ADC_values[j] <= HIGH_THRESHOLD)
                {
                    element_widths[element_count] = high_count; // Store width of HIGH pulse
                    element_count++;
                    break;
                }
                high_count++;
                printf("YOUR MOTHER: %d\n", high_count);
            }
            high_count = 0; // Reset counter
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

    bool scanning_started = false; // Flag to indicate if scanning has started
    bool initial_state = false;    // Assume initial state is on white space

    while (1)
    {
        // Read sensor value
        collect_data();
        find_threshold();
        printf("High threshold: %d, Low threshold: %d\n", HIGH_THRESHOLD, LOW_THRESHOLD);

        find_width();

        for (int i = 0; i < MAX_ELEMENTS; i++)
        {
            printf("Element %d: %d width\n", i, element_widths[i]);
        }

        sleep_ms(5000);

        // if (!scanning_started && sensor_value > THRESHOLD)
        // {
        //     scanning_started = true;
        //     printf("Barcode scanning started!\n");
        //     scanning_completed = false;
        //     element_count = 0;
        // }
        // if (scanning_started)
        // {
        //     // Measure pulse widths
        //     measure_pulse_width_adc(ADC_PIN);
        //     scanning_completed = true;
        //     // Wait until scanning is completed
        //     while (!scanning_completed)
        //     {
        //         tight_loop_contents();
        //     }
        //     printf("Scanning completed.");
        //     // Classify elements
        //     classify_elements(elements, element_count);
        //     // Decode character
        //     char decoded_char = decode_character(elements);
        //     printf("Decoded character: %c\n", decoded_char);
        //     // Reset for next character
        //     element_count = 0;
        //     scanning_completed = false;
        //     // Check if we've reached the end of the barcode
        //     if (decoded_char == '*')
        //     { //'*' marks the end of the barcode
        //         printf("Barcode scan completed.\n");
        //         scanning_started = false;
        //     }
        // }
    }
    return 0;
}
