#include <Arduino.h>

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#include "esPod.h"
#include "Esp32A2dpBluetoothSource.h"

#ifdef USE_SD
#include "sdLogUpdate.h"
bool sdLoggerEnabled = false;
#endif

#pragma region Board IO Macros
// LED Logic inversion
#ifndef INVERT_LED_LOGIC
#define INVERT_LED_LOGIC(stateBoolean) stateBoolean
#else
#undef INVERT_LED_LOGIC
#define INVERT_LED_LOGIC(stateBoolean) !stateBoolean
#endif

// DCD Logic inversion
#ifndef INVERT_DCD_LOGIC
#define INVERT_DCD_LOGIC(stateBoolean) stateBoolean
#else
#undef INVERT_DCD_LOGIC
#define INVERT_DCD_LOGIC(stateBoolean) !stateBoolean
#endif

// DCD control pin to pretend there is a physical disconnect
#if defined(ENABLE_ACTIVE_DCD) && !defined(DCD_CTRL_PIN)
#define DCD_CTRL_PIN 5
#endif
#pragma endregion

#pragma region Audio output + Bluetooth backend wiring
// Audio output routing (I2S/codec) is board-specific and stays here, same
// as before - only the A2DP/AVRCP protocol handling moved into
// Esp32A2dpBluetoothSource.

#ifdef AUDIOKIT

#include "AudioBoard.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"
AudioInfo info(44100, 2, 16);
DriverPins minimalPins;
AudioBoard minimalAudioKit(AudioDriverES8388, minimalPins);
I2SCodecStream i2s(minimalAudioKit);
BluetoothA2DPSink a2dp_sink(i2s);

#ifdef USE_ALT_SERIAL
#define USE_SERIAL_1

#ifndef UART1_RX
#define UART1_RX 19
#endif

#ifndef UART1_TX
#define UART1_TX 22
#endif

#else // Use main Serial
#define USE_SERIAL_0
#endif

#else // Case not using the audiokit, like Sandwich Carrier Board
#define USE_SERIAL_1

#ifndef UART1_RX
#define UART1_RX 16
#endif

#ifndef UART1_TX
#define UART1_TX 17
#endif

#ifndef UART1_RST
#define UART1_RST 13
#endif

I2SStream i2s;

BluetoothA2DPSink a2dp_sink;

#endif

// On AUDIOKIT boards a2dp_sink already writes into i2s directly, so the
// backend doesn't need a raw output stream (nullptr). On other boards the
// backend writes into i2s manually from its raw stream-reader callback.
#ifdef AUDIOKIT
Esp32A2dpBluetoothSource btSource(a2dp_sink);
#else
Esp32A2dpBluetoothSource btSource(a2dp_sink, &i2s);
#endif

#pragma endregion

#pragma region Serial Initialization

#if defined(ARDUINO) && !defined(USE_ESP_IDF_SERIAL)

#include "ArduinoUart.h"
#ifdef USE_SERIAL_0
HardwareSerial ipodSerial(0);
#elifdef USE_SERIAL_1
HardwareSerial ipodSerial(1);
#else
#error "Unknown serial"
#endif

ArduinoUart uart(ipodSerial);

#else

#include "EspIdfUart.h"
#ifdef USE_SERIAL_0
EspIdfUart uart(UART_NUM_0);
#elifdef USE_SERIAL_1
EspIdfUart uart(UART_NUM_1);
#else
#error "Unknown serial"
#endif

#endif

esPod espod(uart);

#pragma endregion

#pragma region Helper Functions declaration
void initializeSDCard();
void initializeSerial();
void initializeAudioOutput();
#pragma endregion

void setup()
{
// If available, reset the UART1 transceiver
#ifdef UART1_RST
	platform::gpio_configure(UART1_RST, platform::PinMode::Output);
	platform::gpio_write(UART1_RST, platform::PinLevel::Low);
#endif

#ifdef LED_BUILTIN
	platform::gpio_configure(LED_BUILTIN, platform::PinMode::Output);
	platform::gpio_write(LED_BUILTIN, INVERT_LED_LOGIC(platform::PinLevel::Low));
#endif

#ifdef RESET_STATE_KEY
	platform::gpio_configure(RESET_STATE_KEY, platform::PinMode::Input);
#endif

#ifdef ENABLE_ACTIVE_DCD
	platform::gpio_configure(DCD_CTRL_PIN, platform::PinMode::Output);
	platform::gpio_write(DCD_CTRL_PIN, INVERT_DCD_LOGIC(platform::PinLevel::High)); // Logic is inverted
#endif

	esp_log_level_set("*", ESP_LOG_NONE);
	ESP_LOGI("SETUP", "setup() start");

	initializeSDCard();

	// Inform of possible errors that led to a reset
	ESP_LOGI("RESET", "Reset reason: %d", esp_reset_reason());
	platform::delay_ms(5);

	// Publish build information
	ESP_LOGI("BUILD_INFO", "env:%s\t date: %s\t time: %s", PIOENV, __DATE__, __TIME__);
	platform::delay_ms(5);
	ESP_LOGI("VERSION", "%s", VERSION_STRING);
	platform::delay_ms(5);
	ESP_LOGI("BRANCH", "%s", VERSION_BRANCH);
	platform::delay_ms(5);

	initializeAudioOutput();

	espod.attachPlaybackSource(btSource);
	btSource.begin(A2DP_SINK_NAME);

#ifdef UART1_RST // Re-enable the UART1 transceiver if available
	platform::gpio_write(UART1_RST, platform::PinLevel::High);
#endif
	initializeSerial();

	ESP_LOGI("SETUP", "Setup finished");
}

void loop()
{
#ifdef RESET_STATE_KEY
	
	uint32_t start_key_pressed = 0;
	bool clean_last_connection;
	
	if (digitalRead(RESET_STATE_KEY) == 0)
	{
		if (start_key_pressed != 0) {
			if (!clean_last_connection && platform::time_now_ms() - start_key_pressed > 10000)
			{
				ESP_LOGI("MAIN", "Clean last connection");
				btSource.forgetConnection();
				clean_last_connection = true;
			}
		}
		else
		{
			start_key_pressed = platform::time_now_ms();
		}
	}
	else
	{
		start_key_pressed = 0;
		clean_last_connection = false;
	}
#endif

#ifdef LED_BUILTIN
	platform::gpio_write(LED_BUILTIN, INVERT_LED_LOGIC(espod.disabled ? platform::PinLevel::Low : platform::PinLevel::High));
#endif
#ifdef ENABLE_ACTIVE_DCD
	platform::gpio_write(DCD_CTRL_PIN, INVERT_DCD_LOGIC(espod.disabled ? platform::PinLevel::High : platform::PinLevel::Low));
#endif

	platform::delay_ms(10);
}

#pragma region Helper Function Definitions
/// @brief Attempts to initialize the SD card if present and enabled
void initializeSDCard()
{
#ifdef USE_SD
	if (platform::gpio_read(SD_DETECT) == platform::PinLevel::Low)
	{
		if (initSD())
		{
#ifdef LOG_TO_SD
			sdLoggerEnabled = initSDLogger();
			if (sdLoggerEnabled)
				esp_log_level_set("*", ESP_LOG_INFO);
#endif
			updateFromFS(SD_MMC);
		}
	}
#endif
}

/// @brief Sets up and starts the appropriate Serial interface
void initializeSerial()
{
#if defined(ARDUINO) && !defined(USE_ESP_IDF_SERIAL)
#ifndef IPOD_SERIAL_BAUDRATE
#define IPOD_SERIAL_BAUDRATE 19200
#endif
#if defined(USE_SERIAL_1) || defined(USE_ALT_SERIAL) // If Alt Serial or Serial 1 is used
	ipodSerial.setPins(UART1_RX, UART1_TX);
#endif
	ipodSerial.setRxBufferSize(1024);
	ipodSerial.setTxBufferSize(1024);
	ipodSerial.begin(IPOD_SERIAL_BAUDRATE);
#else
    uart_config_t cfg = {
        .baud_rate = IPOD_SERIAL_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &cfg));

    ESP_ERROR_CHECK(uart_set_pin(
        UART_NUM_1,
        UART1_TX,
        UART1_RX,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_driver_install(
        UART_NUM_1,
        1024,
        1024,
        0,
        nullptr,
        0));

    uart_flush(UART_NUM_1);
#endif
}

/// @brief Configures the CODEC or DAC audio output. Purely board/audio-path
/// wiring - nothing Bluetooth-protocol-specific lives here anymore.
void initializeAudioOutput()
{
#ifdef AUDIOKIT
	minimalPins.addI2C(PinFunction::CODEC, 32, 33);
	minimalPins.addI2S(PinFunction::CODEC, 0, 27, 25, 26, 35);
	auto cfg = i2s.defaultConfig();
	cfg.copyFrom(info);
	i2s.begin(cfg);
#else
	auto cfg = i2s.defaultConfig(TX_MODE);
	cfg.pin_ws = 25;   // Default is 15
	cfg.pin_data = 26; // Default is 22
	cfg.pin_bck = 27;  // Default is 14
	cfg.sample_rate = 44100;
	cfg.i2s_format = I2S_LSB_FORMAT;
	i2s.begin(cfg);
#endif
}
#pragma endregion
