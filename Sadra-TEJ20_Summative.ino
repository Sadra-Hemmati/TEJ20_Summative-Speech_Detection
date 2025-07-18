/* Author: Sadra Hemmati
 * Date: June 19th 2025
 * Description: Uses MFFC and a classifier NN to detect three keywords from audio input on A0: "left", "right", and "stop", 
 * controlling a motor based on the command. When the user presses the button, the next second of audio will be recorded and passed into the model.
 * The motor will be turned off during recording to reduce noise. If the model does not detect a valid command, it will resume the previous one.
 * An LCD is used to display the current command, as well as basic instructions and information.
 * The audio signal from A0 should be centered on 2.5v
 */

#include <LiquidCrystal_I2C.h>
#include <TEJ20_Keyword_Spotting_inferencing.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

//The current command generated by the classifer (left, right, stop, noise or unknown)
//noise is random sounds, while unknown is random words
String command = "stop";

// Motor and button pins
#define BTN_PIN 9
#define DC_ENABLE 11
#define DC_LEFT 8
#define DC_RIGHT 10

// Audio input settings
#define MIC_PIN A0
#define SAMPLE_RATE 6000
#define SAMPLES_PER_FRAME (EI_CLASSIFIER_RAW_SAMPLE_COUNT)

// Microphone readings (1 frame)
int8_t audio_buffer[SAMPLES_PER_FRAME];

// Debug method to estimate available RAM
// Source: https://chatgpt.com/share/6853a2ee-7c00-8009-a891-17cafc48dc48
extern "C" char* sbrk(int incr);
int freeMemory() {
  char top;
  return &top - reinterpret_cast<char*>(sbrk(0));
}

void setup() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);

    pinMode(BTN_PIN, INPUT_PULLUP);
    pinMode(DC_ENABLE, OUTPUT);
    pinMode(DC_LEFT, OUTPUT);
    pinMode(DC_RIGHT, OUTPUT);
    pinMode(MIC_PIN, INPUT);
    Serial.begin(115200);
}

void loop() {
    lcd.setCursor(0, 0);
    lcd.print("Press to record for 1s");
    lcd.setCursor(0, 1);
    lcd.print("Command: " + command);

    //only proceed if the button is pressed
    //button has inverted logic
    if (digitalRead(BTN_PIN)){
        return;
    }
    
    //stop the fans from adding noise
    digitalWrite(DC_ENABLE, HIGH);
    digitalWrite(DC_LEFT, HIGH);
    digitalWrite(DC_RIGHT, HIGH);
    delay(100);

    Serial.println("Start: " + String(freeMemory()));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Recording...");

    recordAudioFrame();

    Serial.println("After recording: " + String(freeMemory()));
    
    // Wrap into signal structure for the classifier
    //Source: https://chatgpt.com/c/684b207e-3ad0-8009-871c-5ca79cb1a9db
    signal_t signal;
    int err = numpy::signal_from_buffer_int8_t(audio_buffer, SAMPLES_PER_FRAME, &signal);
    if (err != 0) {
        Serial.println("Failed to create signal from buffer");
        return;
    }

    Serial.println("After signal: " + String(freeMemory()));

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Processing...");

    // Print buffer for debugging
    for (size_t i = 0; i < SAMPLES_PER_FRAME; i++)
    {
        Serial.println(audio_buffer[i]);
    }

    ei_impulse_result_t result = {0};

    // Run inference
    //Source: https://chatgpt.com/c/684b207e-3ad0-8009-871c-5ca79cb1a9db
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
    if (res != EI_IMPULSE_OK) {
        Serial.println("Classification failed");
        return;
    }
    else {
        Serial.println("Classification complete");
    }

    Serial.println("After Classification: " + String(freeMemory()));

    // Debug output
    //Source: https://chatgpt.com/c/684b207e-3ad0-8009-871c-5ca79cb1a9db
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        Serial.print(result.classification[ix].label);
        Serial.print(": ");
        Serial.println(result.classification[ix].value, 3);
    }

    // Find highest confidence label
    //Source: https://chatgpt.com/c/684b207e-3ad0-8009-871c-5ca79cb1a9db
    float max_val = 0;
    String new_command;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        //Exclude "noise" and "unknown"
        if (result.classification[ix].value > max_val && !(ix == 1 || ix == 4)) {
            max_val = result.classification[ix].value;
            new_command = result.classification[ix].label;
        }
    }

    //if a valid command was received, set it as the command
    float min_certainty = 0.25;
    if (max_val >= min_certainty && (new_command == "left" || new_command == "right" || new_command == "stop")) {
        command = new_command;
    }

    //Execute commands if a keyword is detected, if the command was "stop", then the motor is already off
    if (command == "left"){
        analogWrite(DC_ENABLE, 200);
        digitalWrite(DC_LEFT, HIGH);
        digitalWrite(DC_RIGHT, LOW);
    }
    else if (command == "right") {
        analogWrite(DC_ENABLE, 200);
        digitalWrite(DC_RIGHT, HIGH);
        digitalWrite(DC_LEFT, LOW);
    }
}

// Blocks for one second and records a one second audio frame
// Returns a pointer to the start of the buffer
// **WILL CAUSE MEMORY LEAKS IF THE MEMORY IS NOT FREED AFTER USE**
// int16_t* recordAudioFrame_int16() {
//     int16_t* audio_buffer = new int16_t[SAMPLES_PER_FRAME];
//     uint32_t sample_interval_us = 1000000 / SAMPLE_RATE;

//     float dt = 1/SAMPLE_RATE;
//     float fc = 4000; // cutoff frequency
//     float alpha = (2*PI*fc*dt) / (2*PI*fc*dt + 1); // Low pass filter constant

//     audio_buffer[0] = analogRead(sample_interval_us);
//     for (size_t i = 1; i < SAMPLES_PER_FRAME; i++) {
//         delayMicroseconds(sample_interval_us);
//         int raw = analogRead(MIC_PIN);  // 0 - 1023
//         audio_buffer[i] = int16_t(raw * alpha + audio_buffer[i-1] * (1 - alpha)); // Low pass filter to reduce noise
//         audio_buffer[i] = (audio_buffer[i] - 512);  // Center on 0
//     }
//     Serial.println("Done sampling");
//     return audio_buffer;
// }

// int8_t* recordAudioFrame_int8() {
//     int8_t* audio_buffer = new int8_t[SAMPLES_PER_FRAME];
//     uint32_t sample_interval_us = 1000000 / SAMPLE_RATE;

//     float dt = 1/SAMPLE_RATE;
//     float fc = 4000; // cutoff frequency
//     float alpha = (2*PI*fc*dt) / (2*PI*fc*dt + 1); // Low pass filter constant

//     audio_buffer[0] = analogRead(sample_interval_us);
//     for (size_t i = 1; i < SAMPLES_PER_FRAME; ++i) {
//         delayMicroseconds(sample_interval_us);
//         int raw = analogRead(MIC_PIN);  // 0 - 1023
//         // Low pass filter to reduce noise, then map to an 8 bit int centered on 0
//         audio_buffer[i] = int8_t(map(raw * alpha + audio_buffer[i-1] * (1 - alpha), 0, 1023, -128, 127));
//     }
//     Serial.println("Done sampling");
//     return audio_buffer;
// }

// Populate the audio buffer with 1s of 6khz readings
// Source (heavily altered): https://chatgpt.com/c/684b207e-3ad0-8009-871c-5ca79cb1a9db
void recordAudioFrame() {
    uint32_t sample_interval_us = 1000000 / SAMPLE_RATE;

    float dt = 1.0f/SAMPLE_RATE;
    float fc = 4000; // cutoff frequency
    float alpha = (2.0f*PI*fc*dt) / (2.0f*PI*fc*dt + 1); // Low pass filter constant
    Serial.println(alpha, 10);

    // First sample does not have low pass filter
    audio_buffer[0] = int8_t((analogRead(MIC_PIN) - 512) * 128.0f / 512.0f);
    for (size_t i = 1; i < SAMPLES_PER_FRAME; ++i) {
        delayMicroseconds(sample_interval_us);
        int raw = analogRead(MIC_PIN);  // 0 - 1023
        int8_t raw_int8 = int8_t((raw - 512) * 128.0f / 512.0f); //-128 - 127
        // Low pass filter to reduce noise, then map to an 8 bit int centered on 0
        audio_buffer[i] = int8_t(raw_int8 * alpha + audio_buffer[i-1] * (1 - alpha));
        // audio_buffer[i] = raw_int8;
    }
    Serial.println("Done sampling");
}