#include <Arduino.h>

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#include "esPod.h"

#ifdef ZERO_VOLUME_FIX
#include "ArduinoNvs.h"
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

#pragma region A2DP Sink Configuration and Serial Initialization

#ifdef AUDIOKIT

#ifdef USE_SD
#include "sdLogUpdate.h"
bool sdLoggerEnabled = false;
#endif

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

#pragma region FreeRTOS tasks defines
#ifndef AVRC_QUEUE_SIZE
#define AVRC_QUEUE_SIZE 32
#endif
#ifndef PROCESS_AVRC_TASK_STACK_SIZE
#define PROCESS_AVRC_TASK_STACK_SIZE 4096
#endif
#ifndef PROCESS_AVRC_TASK_PRIORITY
#define PROCESS_AVRC_TASK_PRIORITY 6
#endif
#ifndef AVRC_INTERVAL_MS
#define AVRC_INTERVAL_MS 5
#endif
#pragma endregion

#pragma region Helper Functions declaration
void initializeSDCard();
void initializeSerial();
void initializeA2DPSink();
esp_err_t initializeAVRCTask();
#ifdef ZERO_VOLUME_FIX
uint8_t nvs_get_volume();
void nvs_set_volume(uint8_t volume);
#endif
#pragma endregion

#pragma region A2DP/AVRC callbacks declaration
void connectionStateChanged(esp_a2d_connection_state_t state, void *ptr);
void audioStateChanged(esp_a2d_audio_state_t state, void *ptr);
void avrc_rn_play_pos_callback(uint32_t play_pos);
void avrc_metadata_callback(uint8_t id, const uint8_t *text);
void avrc_connection_state_callback(bool connected);
#ifdef ZERO_VOLUME_FIX
void avrc_rn_volumechange_callback(int volume);
void avrc_rn_volumechange_completed_callback(int volume);
#endif
void playStatusHandler(byte playCommand);
#if defined(TRACK_POSITION_FIX)
void read_data_stream(const uint8_t *data, uint32_t length);
#endif
#ifdef TRACK_CHANGE_CALLBACK
void avrc_rn_track_change_callback(uint8_t *uid);
#endif

#pragma endregion

typedef enum {
   PEER_DISCONNECTED = 0,
	PEER_CONNECTING,
   PEER_CONNECTED
} t_peer_state;

t_peer_state peer_state;

#ifdef ZERO_VOLUME_FIX

typedef enum {
	VOLUME_AFTER_CONNECTION_NOT_DEFINED = 0,
	VOLUME_AFTER_CONNECTION_SET
} t_volume_state;

t_volume_state volume_state;

uint8_t nvs_get_volume()
{
	ESP_LOGI("MAIN", "get volume from NVS");
	// get volume from NVS
	return NVS.getInt("volume", 32);
}

void nvs_set_volume(uint8_t volume)
{
	if (volume != nvs_get_volume())
	{
		ESP_LOGI("MAIN", "set volume to NVS");
		// set volume to NVS
		if (!NVS.setInt("volume", volume, true))
		{
			ESP_LOGE("MAIN", "failed to save volume to NVS");
		}
	}
}

#endif

void setup()
{
// If available, reset the UART1 transceiver
#ifdef UART1_RST
	pinMode(UART1_RST, OUTPUT);
	digitalWrite(UART1_RST, LOW);
#endif

#ifdef LED_BUILTIN
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, INVERT_LED_LOGIC(LOW));
#endif

#ifdef RESET_STATE_KEY
	pinMode(RESET_STATE_KEY, INPUT);
#endif

#ifdef ENABLE_ACTIVE_DCD
	pinMode(DCD_CTRL_PIN, OUTPUT);
	digitalWrite(DCD_CTRL_PIN, INVERT_DCD_LOGIC(HIGH)); // Logic is inverted
#endif

	esp_log_level_set("*", ESP_LOG_NONE);
	ESP_LOGI("SETUP", "setup() start");

	initializeSDCard();

	// Inform of possible errors that led to a reset
	ESP_LOGI("RESET", "Reset reason: %d", esp_reset_reason());
	platform::delay(5);

	// Publish build information
	ESP_LOGI("BUILD_INFO", "env:%s\t date: %s\t time: %s", PIOENV, __DATE__, __TIME__);
	platform::delay(5);
	ESP_LOGI("VERSION", "%s", VERSION_STRING);
	platform::delay(5);
	ESP_LOGI("BRANCH", "%s", VERSION_BRANCH);
	platform::delay(5);

	if (initializeAVRCTask() != ESP_OK)
		esp_restart();
	initializeA2DPSink();
#ifdef UART1_RST // Re-enable the UART1 transceiver if available
	digitalWrite(UART1_RST, HIGH);
#endif
	initializeSerial();
	espod.attachPlayControlHandler(playStatusHandler);
#ifdef ZERO_VOLUME_FIX
	NVS.begin();
#endif
	peer_state = PEER_DISCONNECTED;
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
			if (!clean_last_connection && platform::millis() - start_key_pressed > 10000)
			{
				ESP_LOGI("MAIN", "Clean last connection");
				if (a2dp_sink.get_connection_state() != ESP_A2D_CONNECTION_STATE_DISCONNECTED)
				{
					a2dp_sink.disconnect();
				}
				a2dp_sink.clean_last_connection();
				clean_last_connection = true;
			}
		} else {
			start_key_pressed = platform::millis();
		}
	}
	else
	{
		start_key_pressed = 0;
		clean_last_connection = false;
	}

#endif

	if (peer_state == PEER_DISCONNECTED) {
		peer_state = PEER_CONNECTING;
		ESP_LOGI("MAIN", "Waiting for peer");
	} else if (peer_state == PEER_CONNECTING && a2dp_sink.get_connection_state() == ESP_A2D_CONNECTION_STATE_CONNECTED)
	{
		platform::delay(50);
		peer_state = PEER_CONNECTED;
#ifdef USE_PEER_NAME
		espod._peer_name = a2dp_sink.get_peer_name();
		ESP_LOGI("MAIN", "Peer connected: %s", espod._peer_name);
#else
		ESP_LOGI("MAIN", "Peer connected: %s", a2dp_sink.get_peer_name());
#endif
	}
	else if (peer_state == PEER_CONNECTED && a2dp_sink.get_connection_state() == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
	{
		peer_state = PEER_DISCONNECTED;
	}
	platform::delay(10);
}

#pragma region AVRC Task and Queue declaration/definition
// Metadata universal structure
struct avrcMetadata
{
	uint8_t id = 0;
	uint8_t *payload = nullptr;
};

// AVRC Queue and Task
QueueHandle_t avrcMetadataQueue;
TaskHandle_t processAVRCTaskHandle;

void filterPayload(char *output, const char *input) {
    size_t in_idx = 0, out_idx = 0, max_len = 255;
    while (input[in_idx] != '\0' && out_idx < max_len - 1) {
        unsigned char c = input[in_idx];
        
        // Check for UTF-8 byte length based on leading bits
        int char_len = 1;
        if (c >= 0xF0) {
            char_len = 4; // 4-byte characters (most emojis live here)
        } else if (c >= 0xE0) {
            char_len = 3; // 3-byte characters (some symbols/emojis)
        } else if (c >= 0xC0) {
            char_len = 2; // 2-byte characters
        }

        // If it's a 4-byte or high 3-byte sequence, skip it (treat as emoji/unsupported symbol)
        if (char_len >= 4 || (char_len == 3 && c >= 0xEF)) {
            in_idx += char_len;
        } else {
            // Copy valid standard bytes
            for (int i = 0; i < char_len && input[in_idx + i] != '\0'; i++) {
                output[out_idx++] = input[in_idx++];
            }
        }
    }
    output[out_idx] = '\0';
}

/// @brief Low priority task to process a queue of received metadata
/// @param pvParameters
static void processAVRCTask(void *pvParameters)
{
	avrcMetadata incMetadata; // Incoming metadata (pointer to payload)
	
	TrackMetadata pendingMetadata;

	uint32_t trackNum = INVALID_TRACK_NUM, prevTrackNum = INVALID_TRACK_NUM;
	uint32_t avrcMetadataTimestamp = INVALID_TIMESTAMP;

#ifdef STACK_HIGH_WATERMARK_LOG
	UBaseType_t uxHighWaterMark;
	UBaseType_t minHightWaterMark = PROCESS_AVRC_TASK_STACK_SIZE;
#endif

	// Main loop
	while (true)
	{
// Stack high watermark logging
#ifdef STACK_HIGH_WATERMARK_LOG
		uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
		if (uxHighWaterMark < minHightWaterMark)
		{
			minHightWaterMark = uxHighWaterMark;
			ESP_LOGI("HWM", "Process AVRC Task High Watermark: %d, used stack: %d", minHightWaterMark,
					 PROCESS_AVRC_TASK_STACK_SIZE - minHightWaterMark);
		}
#endif
		// Check incoming metadata in queue
		if (xQueueReceive(avrcMetadataQueue, &incMetadata, 0) == pdTRUE)
		{
			avrcMetadataTimestamp = platform::millis();

			// Start processing
			switch (incMetadata.id)
			{
			case ESP_AVRC_MD_ATTR_TRACK_NUM:
				trackNum = String((char *)incMetadata.payload).toInt();
				if (prevTrackNum == INVALID_TRACK_NUM)
					prevTrackNum = trackNum;
				break;
			case ESP_AVRC_MD_ATTR_TITLE:
				filterPayload(pendingMetadata.title, (char *)incMetadata.payload);
#ifdef TRACK_POSITION_FIX
					// Reset the byte counter immediately for the new track
					espod.rawAudioDataBytesReceived = 0;
#endif
				break;
			case ESP_AVRC_MD_ATTR_ALBUM:
				filterPayload(pendingMetadata.album, (char *)incMetadata.payload);
				break;
			case ESP_AVRC_MD_ATTR_ARTIST:
				filterPayload(pendingMetadata.artist, (char *)incMetadata.payload);
				break;
			case ESP_AVRC_MD_ATTR_PLAYING_TIME:
				pendingMetadata.duration = String((char *)incMetadata.payload).toInt();
				break;
			}
			// End Processing, deallocate memory
			delete[] incMetadata.payload;
			incMetadata.payload = nullptr;
		}
		if (avrcMetadataTimestamp != INVALID_TIMESTAMP && platform::millis() - avrcMetadataTimestamp > AVRC_RECEIVE_METADATA_TIMEOUT)
		{
			avrcMetadataTimestamp = INVALID_TIMESTAMP;

			espod.updateMetadata(&pendingMetadata, trackNum < prevTrackNum ? PB_CMD_PREV : PB_CMD_NEXT);

			prevTrackNum = trackNum;
		}
		vTaskDelay(pdMS_TO_TICKS(AVRC_INTERVAL_MS));
	}
}
#pragma endregion

#pragma region Helper Function Definitions
/// @brief Attempts to initialize the SD card if present and enabled
void initializeSDCard()
{
#ifdef USE_SD
	if (digitalRead(SD_DETECT) == LOW)
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

    //return ESP_OK;
#endif
}

/// @brief Configures the CODEC or DAC and starts the A2DP Sink
void initializeA2DPSink()
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

	a2dp_sink.set_auto_reconnect(true, 10000);
	a2dp_sink.set_on_connection_state_changed(connectionStateChanged);
	a2dp_sink.set_on_audio_state_changed(audioStateChanged);
	a2dp_sink.set_avrc_connection_state_callback(avrc_connection_state_callback);
	a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
	a2dp_sink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST |
											   ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_PLAYING_TIME |
												ESP_AVRC_MD_ATTR_TRACK_NUM | ESP_AVRC_MD_ATTR_NUM_TRACKS);
	a2dp_sink.set_avrc_rn_play_pos_callback(avrc_rn_play_pos_callback, 1);
#ifdef ZERO_VOLUME_FIX
	a2dp_sink.set_avrc_rn_volumechange(avrc_rn_volumechange_callback);
	a2dp_sink.set_avrc_rn_volumechange_completed(avrc_rn_volumechange_completed_callback);
#endif
#if defined(TRACK_POSITION_FIX)
	a2dp_sink.set_stream_reader(read_data_stream, false);
#endif
#ifdef TRACK_CHANGE_CALLBACK
	a2dp_sink.set_avrc_rn_track_change_callback(avrc_rn_track_change_callback);
#endif
	a2dp_sink.start(A2DP_SINK_NAME);

	ESP_LOGI("SETUP", "a2dp_sink started: %s", A2DP_SINK_NAME);
	platform::delay(500);
	a2dp_sink.set_discoverability(ESP_BT_GENERAL_DISCOVERABLE);
}

/// @brief Initializes the AVRC metadata queue, and attempts to start the
/// related task
/// @return ESP_FAIL if the queue or task could not be created, ESP_OK otherwise
esp_err_t initializeAVRCTask()
{
	avrcMetadataQueue = xQueueCreate(AVRC_QUEUE_SIZE, sizeof(avrcMetadata));
	if (avrcMetadataQueue == nullptr)
	{
		ESP_LOGE("SETUP", "Failed to create metadata queue");
		return ESP_FAIL;
	}

	xTaskCreatePinnedToCore(processAVRCTask, "processAVRCTask", PROCESS_AVRC_TASK_STACK_SIZE, NULL,
							PROCESS_AVRC_TASK_PRIORITY, &processAVRCTaskHandle, ARDUINO_RUNNING_CORE);
	if (processAVRCTaskHandle == nullptr)
	{
		ESP_LOGE("SETUP", "Failed to create processAVRCTask");
		return ESP_FAIL;
	}

	return ESP_OK;
}
#pragma endregion

volatile esp_a2d_connection_state_t connection_state;

#pragma region A2DP/AVRC callbacks Definitions
/// @brief Callback on changes of A2DP connection and AVRCP connection. On
/// disconnect the esPod becomes silent.
/// @param state New state passed by the callback.
/// @param ptr Not used.
void connectionStateChanged(esp_a2d_connection_state_t state, void *ptr)
{
	switch (state)
	{
	case ESP_A2D_CONNECTION_STATE_CONNECTING:
	   connection_state = state;
		ESP_LOGI("A2DP_CB", "ESP_A2D_CONNECTION_STATE_CONNECTING");
		break;
	case ESP_A2D_CONNECTION_STATE_CONNECTED:
		connection_state = state;
		ESP_LOGI("A2DP_CB", "ESP_A2D_CONNECTION_STATE_CONNECTED, espod enabled");
		espod.disabled = false;
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, INVERT_LED_LOGIC(HIGH));
#endif
#ifdef ZERO_VOLUME_FIX
		volume_state = VOLUME_AFTER_CONNECTION_NOT_DEFINED;
#endif
		break;
	case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
		ESP_LOGI("A2DP_CB", "ESP_A2D_CONNECTION_STATE_DISCONNECTED, espod disabled");
		espod.resetState();
		espod.disabled = true;
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, INVERT_LED_LOGIC(LOW));
#endif
		if (connection_state == ESP_A2D_CONNECTION_STATE_CONNECTING) // no connected state -> reconnect
		{
			a2dp_sink.reconnect();
		}
		connection_state = state;
		break;
	}
#ifdef ENABLE_ACTIVE_DCD
	digitalWrite(DCD_CTRL_PIN, INVERT_DCD_LOGIC(espod.disabled)); // Logic inversion by MACRO
#endif
}

/// @brief Callback for the change of playstate after connection. Aligns the
/// state of the esPod to the state of the phone. Play should be called by the
/// espod interaction
/// @param state The A2DP Stream to align to.
/// @param ptr Not used.
void audioStateChanged(esp_a2d_audio_state_t state, void *ptr)
{
	switch (state)
	{
	case ESP_A2D_AUDIO_STATE_STARTED:
#ifdef ZERO_VOLUME_FIX
		if (volume_state == VOLUME_AFTER_CONNECTION_NOT_DEFINED) {
			volume_state = VOLUME_AFTER_CONNECTION_SET;
			ESP_LOGI("MAIN", "Volume is not set after connecting. Restore volume to saved value");
			a2dp_sink.set_volume(nvs_get_volume());
		}
#endif
		espod.playStatus = PB_STATE_PLAYING;
		ESP_LOGI("A2DP_CB", "ESP_A2D_AUDIO_STATE_STARTED, espod.playStatus = PB_STATE_PLAYING");
		break;
	case ESP_A2D_AUDIO_STATE_SUSPEND:
		espod.playStatus = PB_STATE_PAUSED;
		ESP_LOGI("A2DP_CB", "ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND, espod.playStatus = PB_STATE_PAUSED");
#ifdef ZERO_VOLUME_FIX
		nvs_set_volume(a2dp_sink.get_volume());
#endif
		break;
	}

#ifdef TRACK_POSITION_FIX
	if (state == ESP_A2D_AUDIO_STATE_STARTED) {
		espod.is_playing = true;
	} 
	else if (state == ESP_A2D_AUDIO_STATE_STOPPED) {
		espod.is_playing = false;
		if (espod.playStatus != PB_STATE_PAUSED) {
			espod.rawAudioDataBytesReceived = 0; // Reset counter if stopped completely
		}
	}
	else { // ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND (Paused)
		espod.is_playing = false;
	}
#endif
}


/// @brief a callback method which provides connection state of AVRC service
/// @param connected 
void avrc_connection_state_callback(bool connected)
{
	if (connected)
	{
		ESP_LOGD("AVRC_CB", "Connection state: connected");
#ifdef AUTOPLAY_AFTER_CONNECT 
		a2dp_sink.play();
#endif
	}
	else
	{
		ESP_LOGD("AVRC_CB", "Connection state: disconnected");
	}
}

#ifdef ZERO_VOLUME_FIX

/// @brief 
/// @param volume 
void avrc_rn_volumechange_callback(int volume)
{
	ESP_LOGD("AVRC_CB", "Volume change: %d%%", (int)volume * 100 / 0x7f);
	if (volume_state == VOLUME_AFTER_CONNECTION_NOT_DEFINED)
	{
		volume_state = VOLUME_AFTER_CONNECTION_SET;
		if (volume == 0)
		{
			uint8_t saved_volume = nvs_get_volume();
			if (volume != saved_volume)
			{
				ESP_LOGI("MAIN", "Volume is set to 0 after connecting. Restore volume to saved value");
				a2dp_sink.set_volume(saved_volume);
			}
		}
	}
}

/// @brief 
/// @param volume 
void avrc_rn_volumechange_completed_callback(int volume)
{
	ESP_LOGD("AVRC_CB", "Volume change completed: %d%%", (int)volume * 100 / 0x7f);
}

#endif

/// @brief Play position callback returning the ms spent since start on every
/// interval - normally 1s
/// @param play_pos Playing Position in ms
void avrc_rn_play_pos_callback(uint32_t play_pos)
{
	ESP_LOGI("AVRC_CB", "PlayPosition called");
	espod.playPosition = play_pos;
#ifndef STATUS_NOTIFICATION_QUEUE
	if (espod.playStatusNotificationState == NOTIF_ON && espod.trackChangeAckPending == 0x00)
	{
		// espod.L0x04_0x27_PlayStatusNotification(0x04, play_pos);
		L0x04::_0x27_PlayStatusNotification(&espod, 0x04, play_pos);
	}
#endif
}

/// @brief Catch callback for the AVRC metadata. There can be duplicates !
/// @param id Metadata attribute ID : ESP_AVRC_MD_ATTR_xxx
/// @param text Text data passed around, sometimes it's a uint32_t disguised as
/// text
void avrc_metadata_callback(uint8_t id, const uint8_t *text)
{
	avrcMetadata incMetadata;
	incMetadata.id = id;
	incMetadata.payload = new uint8_t[255];
	memcpy(incMetadata.payload, text, 255);
	if (xQueueSend(avrcMetadataQueue, &incMetadata, 0) != pdTRUE)
	{
		ESP_LOGW("AVRC_CB", "Metadata queue full, discarding metadata");
		delete[] incMetadata.payload;
		incMetadata.payload = nullptr;
	}
}

#if defined(TRACK_POSITION_FIX)
/// @brief Data stream reader callback
/// @param data Data buffer to pass to the I2S
/// @param length Length of the data buffer
void read_data_stream(const uint8_t *data, uint32_t length)
{
#ifndef AUDIOKIT
	i2s.write(data, length);
#endif
#ifdef TRACK_POSITION_FIX
	// Only count bytes if audio is actively flowing
  if (espod.is_playing) {
    espod.rawAudioDataBytesReceived += length;
  }
#endif
}
#endif

#ifdef TRACK_CHANGE_CALLBACK
void avrc_rn_track_change_callback(uint8_t *uid)
{
    ESP_LOGI("A2DP_CB",
             "Track UID: %02X%02X%02X%02X%02X%02X%02X%02X",
             uid[0], uid[1], uid[2], uid[3],
             uid[4], uid[5], uid[6], uid[7]);
}
#endif

/// @brief Callback function that passes intended playback operations from the
/// esPod to the A2DP player (i.e. the phone)
/// @param playCommand A2DP_xx command instruction. It does not match the
/// PB_CMD_xx codes !!!
void playStatusHandler(byte playCommand)
{
	switch (playCommand)
	{
	case A2DP_STOP:
		a2dp_sink.stop();
		ESP_LOGI("A2DP_CB", "A2DP_STOP");
		break;
	case A2DP_PLAY:
		espod.currentTrackIndex = 1;
		a2dp_sink.play();
		ESP_LOGI("A2DP_CB", "A2DP_PLAY");
		break;
	case A2DP_PAUSE:
		a2dp_sink.pause();
		ESP_LOGI("A2DP_CB", "A2DP_PAUSE");
		break;
	case A2DP_REWIND:
		a2dp_sink.previous();
		ESP_LOGI("A2DP_CB", "A2DP_REWIND");
		break;
	case A2DP_NEXT:
		a2dp_sink.next();
		ESP_LOGI("A2DP_CB", "A2DP_NEXT");
		break;
	case A2DP_PREV:
		a2dp_sink.previous();
		ESP_LOGI("A2DP_CB", "A2DP_PREV");
		break;
	}
}

#pragma endregion