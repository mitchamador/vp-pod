#include "esPod.h"
#ifdef ARDUINO
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "Esp32A2dpBluetoothSource.h"
#include "Esp32I2sAudioOutput.h"
#else
#include "nvs_flash.h"
#include "NativeA2dpBluetoothSink.h"
#include "NativeA2dpBluetoothSource.h"
#include "NativeI2sAudioOutput.h"
#endif

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

#ifdef ARDUINO
I2SStream i2s;
Esp32I2sAudioOutput audioOutput(i2s, 27, 25, 26); // bck, ws, data - see old initializeAudioOutput() pin values
#else
NativeI2sAudioOutput audioOutput(I2S_NUM_0, 27, 25, 26);
#endif

#endif

#ifdef ARDUINO
BluetoothA2DPSink a2dp_sink;
#else
NativeA2DPSink a2dp_sink;
#endif


// On AUDIOKIT boards a2dp_sink already writes into i2s directly, so the
// backend doesn't need a separate IAudioOutput (nullptr). On other boards
// the backend writes into it manually from its raw stream-reader callback.
#ifdef AUDIOKIT
Esp32A2dpBluetoothSource btSource(a2dp_sink);
#else
#ifdef ARDUINO
Esp32A2dpBluetoothSource btSource(a2dp_sink, &audioOutput);
#else
NativeA2dpBluetoothSource btSource(a2dp_sink, &audioOutput);
#endif
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
// Pins/baud rate are configured right here, synchronously, in the
// constructor - not deferred to initializeSerial() later in setup(). esPod
// (declared right below) spawns an RX task from its own constructor, and
// that task starts calling uart.available() immediately; deferring the
// actual uart_driver_install() to later in setup() raced against it and
// produced a stream of "uart driver error" log spam until setup() caught up.
#ifdef USE_SERIAL_0
EspIdfUart uart(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, IPOD_SERIAL_BAUDRATE);
#elifdef USE_SERIAL_1
EspIdfUart uart(UART_NUM_1, UART1_RX, UART1_TX, IPOD_SERIAL_BAUDRATE);
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
void initializeNvs();
#pragma endregion

#ifndef ARDUINO
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void setup();
void loop();

void arduinoTask(void *pvParameters) {
	setup();
	while (1) {
		loop();
		vTaskDelay(pdMS_TO_TICKS(1)); 
	}
}

extern "C" void app_main() {
    xTaskCreatePinnedToCore(
        arduinoTask,
        "ArduinoTask",
        8192,
        NULL,
        5,
        NULL,
        1
    );
}
#endif

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

#ifdef ARDUINO
	esp_log_level_set("*", ESP_LOG_NONE);
#endif

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

	initializeNvs();

	espod.attachPlaybackSource(btSource);

	btSource.begin(A2DP_SINK_NAME);

#ifdef UART1_RST // Re-enable the UART1 transceiver if available
	platform::gpio_write(UART1_RST, platform::PinLevel::High);
#endif
	initializeSerial();

	ESP_LOGI("SETUP", "setup() finished");
}

void loop()
{

#ifdef RESET_STATE_KEY
	
	static uint32_t start_key_pressed = 0;
	static bool clean_last_connection;
	
	if (platform::gpio_read(RESET_STATE_KEY) == platform::PinLevel::Low)
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

/// @brief Sets up and starts the appropriate Serial interface. Only needed
/// for the Arduino/HardwareSerial path - the ESP-IDF UART path is fully set
/// up already, synchronously, in the EspIdfUart constructor (see the "Serial
/// Initialization" region above and its comment for why).
void initializeSerial()
{
#if defined(ARDUINO) && !defined(USE_ESP_IDF_SERIAL)
#if defined(USE_SERIAL_1) || defined(USE_ALT_SERIAL) // If Alt Serial or Serial 1 is used
	ipodSerial.setPins(UART1_RX, UART1_TX);
#endif
	ipodSerial.setRxBufferSize(1024);
	ipodSerial.setTxBufferSize(1024);
	ipodSerial.begin(IPOD_SERIAL_BAUDRATE);
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
#endif
	// Non-AUDIOKIT boards: nothing to do here anymore - audioOutput.begin()
	// is called from the Bluetooth backend itself (once at startup for
	// Esp32A2dpBluetoothSource, or dynamically per codec-config-change for
	// NativeA2dpBluetoothSource), not eagerly here.
}

void initializeNvs()
{
#ifdef ARDUINO
#else
	// NVS init
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	}
#endif
}

#pragma endregion
