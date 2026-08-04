#include <Arduino.h>

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"

#ifdef ZERO_VOLUME_FIX
#include "ArduinoNvs.h"
#endif

#ifndef NO_ESPOD
#define ESPOD_ENABLED
#endif

#ifdef ESPOD_ENABLED
#include "esPod.h"
#else
// A2DP instance name
#ifndef A2DP_SINK_NAME
#define A2DP_SINK_NAME "espiPod"
#endif
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
HardwareSerial ipodSerial(1);

#ifndef UART1_RX
#define UART1_RX 19
#endif

#ifndef UART1_TX
#define UART1_TX 22
#endif

#else // Use main Serial
HardwareSerial ipodSerial(0);
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
HardwareSerial ipodSerial(1);
BluetoothA2DPSink a2dp_sink;

/// @brief Data stream reader callback
/// @param data Data buffer to pass to the I2S
/// @param length Length of the data buffer
void read_data_stream(const uint8_t *data, uint32_t length)
{
	i2s.write(data, length);
}

#endif

#ifdef ESPOD_ENABLED
esPod espod(ipodSerial);
#endif

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
	delay(5);

	// Publish build information
	ESP_LOGI("BUILD_INFO", "env:%s\t date: %s\t time: %s", PIOENV, __DATE__, __TIME__);
	delay(5);
	ESP_LOGI("VERSION", "%s", VERSION_STRING);
	delay(5);
	ESP_LOGI("BRANCH", "%s", VERSION_BRANCH);
	delay(5);

	if (initializeAVRCTask() != ESP_OK)
		esp_restart();
	initializeA2DPSink();
#ifdef UART1_RST // Re-enable the UART1 transceiver if available
	digitalWrite(UART1_RST, HIGH);
#endif
	initializeSerial();
#ifdef ESPOD_ENABLED
	espod.attachPlayControlHandler(playStatusHandler);
#endif
#ifdef ZERO_VOLUME_FIX
	NVS.begin();
#endif
	peer_state = PEER_DISCONNECTED;
	ESP_LOGI("SETUP", "Setup finished");
}

uint32_t start_key_pressed = 0;
bool clean_last_connection;

void loop()
{
#ifdef RESET_STATE_KEY
	if (digitalRead(RESET_STATE_KEY) == 0)
	{
		if (start_key_pressed != 0) {
			if (!clean_last_connection && millis() - start_key_pressed > 10000)
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
			start_key_pressed = millis();
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
		delay(50);
		peer_state = PEER_CONNECTED;
		ESP_LOGI("MAIN", "Peer connected: %s", a2dp_sink.get_peer_name());
	}
	else if (peer_state == PEER_CONNECTED && a2dp_sink.get_connection_state() == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
	{
		peer_state = PEER_DISCONNECTED;
	}
	delay(10);
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

/// @brief Low priority task to process a queue of received metadata
/// @param pvParameters
static void processAVRCTask(void *pvParameters)
{
	avrcMetadata incMetadata; // Incoming metadata (pointer to payload)
	// Metadata buffers
	char incAlbumName[255] = "incAlbum";
	char incArtistName[255] = "incArtist";
	char incTrackTitle[255] = "incTitle";
	uint32_t incTrackDuration = 0;
	bool albumNameUpdated = false;
	bool artistNameUpdated = false;
	bool trackTitleUpdated = false;
	bool trackDurationUpdated = false;

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
			// Start processing
			switch (incMetadata.id)
			{
			case ESP_AVRC_MD_ATTR_ALBUM:
				strcpy(incAlbumName,
					   (char *)incMetadata.payload); // Buffer the incoming album string
#ifdef ESPOD_ENABLED
				if (espod.trackChangeAckPending > 0x00)
				{ // There is a pending metadata update
					if (!albumNameUpdated)
					{ // The album Name has not been updated yet
						strcpy(espod.albumName, incAlbumName);
						albumNameUpdated = true;
						ESP_LOGD("AVRC_CB", "Album rxed, ACK pending, albumNameUpdated to %s", espod.albumName);
					}
					else
					{
						ESP_LOGD("AVRC_CB", "Album rxed, ACK pending, already updated to %s", espod.albumName);
					}
				}
				else
				{ // There is no pending track change from iPod : active or
				  // passive track change from avrc target
					if (strcmp(incAlbumName, espod.albumName) != 0)
					{ // Different incoming metadata
						strcpy(espod.prevAlbumName, espod.albumName);
						strcpy(espod.albumName, incAlbumName);
						albumNameUpdated = true;
						ESP_LOGD("AVRC_CB", "Album rxed, NO ACK pending, albumNameUpdated to %s", espod.albumName);
					}
					else
					{ // Despammer for double sends
						ESP_LOGD("AVRC_CB", "Album rxed, NO ACK pending, already updated to %s", espod.albumName);
					}
				}
#endif
				break;

			case ESP_AVRC_MD_ATTR_ARTIST:
				strcpy(incArtistName,
					   (char *)incMetadata.payload); // Buffer the incoming artist string
#ifdef ESPOD_ENABLED
				if (espod.trackChangeAckPending > 0x00)
				{ // There is a pending metadata update
					if (!artistNameUpdated)
					{ // The artist name has not been updated
					  // yet
						strcpy(espod.artistName, incArtistName);
						artistNameUpdated = true;
						ESP_LOGD("AVRC_CB", "Artist rxed, ACK pending, artistNameUpdated to %s", espod.artistName);
					}
					else
					{
						ESP_LOGD("AVRC_CB", "Artist rxed, ACK pending, already updated to %s", espod.artistName);
					}
				}
				else
				{ // There is no pending track change from iPod : active or
				  // passive track change from avrc target
					if (strcmp(incArtistName, espod.artistName) != 0)
					{ // Different incoming metadata
						strcpy(espod.prevArtistName, espod.artistName);
						strcpy(espod.artistName, incArtistName);
						artistNameUpdated = true;
						ESP_LOGD("AVRC_CB", "Artist rxed, NO ACK pending, artistNameUpdated to %s", espod.artistName);
					}
					else
					{ // Despammer for double sends
						ESP_LOGD("AVRC_CB", "Artist rxed, NO ACK pending, already updated to %s", espod.artistName);
					}
				}
#endif
				break;

			case ESP_AVRC_MD_ATTR_TITLE: // Title change triggers the NEXT track
										 // assumption if unexpected. It is too
										 // intensive to try to do NEXT/PREV
										 // guesswork
				strcpy(incTrackTitle,
					   (char *)incMetadata.payload); // Buffer the incoming track title
#ifdef ESPOD_ENABLED
				if (espod.trackChangeAckPending > 0x00)
				{ // There is a pending metadata update
					if (!trackTitleUpdated)
					{ // The track title has not been updated
					  // yet
						strcpy(espod.trackTitle, incTrackTitle);
						trackTitleUpdated = true;
						ESP_LOGD("AVRC_CB", "Title rxed, ACK pending, trackTitleUpdated to %s", espod.trackTitle);
					}
					else
					{
						ESP_LOGD("AVRC_CB", "Title rxed, ACK pending, already updated to %s", espod.trackTitle);
					}
				}
				else
				{ // There is no pending track change from iPod : active or
				  // passive track change from avrc target
					if (strcmp(incTrackTitle, espod.trackTitle) != 0)
					{ // Different from current track Title -> Systematic NEXT
						// Assume it is Next, perform cursor operations
						espod.trackListPosition = (espod.trackListPosition + 1) % TOTAL_NUM_TRACKS;
						espod.prevTrackIndex = espod.currentTrackIndex;
						espod.currentTrackIndex = (espod.currentTrackIndex + 1) % TOTAL_NUM_TRACKS;
						espod.trackList[espod.trackListPosition] = (espod.currentTrackIndex);
						// Copy new title and flag that data has been updated
						strcpy(espod.prevTrackTitle, espod.trackTitle);
						strcpy(espod.trackTitle, incTrackTitle);
						trackTitleUpdated = true;
						ESP_LOGD("AVRC_CB",
								 "Title rxed, NO ACK pending, AUTONEXT, trackTitleUpdated "
								 "to %s\n\ttrackPos %d trackIndex %d",
								 espod.trackTitle, espod.trackListPosition, espod.currentTrackIndex);
					}
					else
					{ // Despammer for double sends
						ESP_LOGD("AVRC_CB", "Title rxed, NO ACK pending, same name : %s", espod.trackTitle);
					}
				}
#endif
				break;

			case ESP_AVRC_MD_ATTR_PLAYING_TIME:
				incTrackDuration = String((char *)incMetadata.payload).toInt();
#ifdef ESPOD_ENABLED
				if (espod.trackChangeAckPending > 0x00)
				{ // There is a pending metadata update
					if (!trackDurationUpdated)
					{ // The duration has not been updated
					  // yet
						espod.trackDuration = incTrackDuration;
						trackDurationUpdated = true;
						ESP_LOGD("AVRC_CB", "Duration rxed, ACK pending, trackDurationUpdated to %d",
								 espod.trackDuration);
					}
					else
					{
						ESP_LOGD("AVRC_CB", "Duration rxed, ACK pending, already updated to %d", espod.trackDuration);
					}
				}
				else
				{ // There is no pending track change from iPod : active or
				  // passive track change from avrc target
					if (incTrackDuration != espod.trackDuration)
					{ // Different incoming metadata
						espod.trackDuration = incTrackDuration;
						trackDurationUpdated = true;
						ESP_LOGD("AVRC_CB", "Duration rxed, NO ACK pending, trackDurationUpdated to %d",
								 espod.trackDuration);
					}
					else
					{ // Despammer for double sends
						ESP_LOGD("AVRC_CB", "Duration rxed, NO ACK pending, already updated to %d",
								 espod.trackDuration);
					}
				}
#endif
				break;
			}

			// Check if it is time to send a notification
			if (albumNameUpdated && artistNameUpdated && trackTitleUpdated && trackDurationUpdated)
			{
				// If all fields have received at least one update and the
				// trackChangeAckPending is still hanging. The failsafe for this one is
				// in the espod _processTask
#ifdef ESPOD_ENABLED
				if (espod.trackChangeAckPending > 0x00)
				{
					ESP_LOGD("AVRC_CB",
							 "Artist+Album+Title+Duration +++ ACK Pending "
							 "0x%x\n\tPending duration: %d",
							 espod.trackChangeAckPending, millis() - espod.trackChangeTimestamp);
					// espod.L0x04_0x01_iPodAck(iPodAck_OK, espod.trackChangeAckPending);
					if (espod.trackChangeAckPending == 0x11)
					{
						L0x03::_0x00_iPodAck(&espod, iPodAck_OK, espod.trackChangeAckPending);
					}
					else
					{
						L0x04::_0x01_iPodAck(&espod, iPodAck_OK, espod.trackChangeAckPending);
					}
					espod.trackChangeAckPending = 0x00;
					ESP_LOGD("AVRC_CB", "trackChangeAckPending reset to 0x00");
				}
#endif
				albumNameUpdated = false;
				artistNameUpdated = false;
				trackTitleUpdated = false;
				trackDurationUpdated = false;
				ESP_LOGD("AVRC_CB", "Artist+Album+Title+Duration : True -> False");
#ifdef ESPOD_ENABLED
				// Inform the car
				if (espod.playStatusNotificationState == NOTIF_ON)
				{
					// espod.L0x04_0x27_PlayStatusNotification(0x01, espod.currentTrackIndex);
					L0x04::_0x27_PlayStatusNotification(&espod, 0x01, espod.currentTrackIndex);
				}
#endif
			}

			// End Processing, deallocate memory
			delete[] incMetadata.payload;
			incMetadata.payload = nullptr;
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
#ifndef IPOD_SERIAL_BAUDRATE
#define IPOD_SERIAL_BAUDRATE 19200
#endif
#if defined(USE_SERIAL_1) || defined(USE_ALT_SERIAL) // If Alt Serial or Serial 1 is used
	ipodSerial.setPins(UART1_RX, UART1_TX);
#endif
	ipodSerial.setRxBufferSize(1024);
	ipodSerial.setTxBufferSize(1024);
	ipodSerial.begin(IPOD_SERIAL_BAUDRATE);
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
	a2dp_sink.set_stream_reader(read_data_stream, false);
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
											   ESP_AVRC_MD_ATTR_ALBUM | ESP_AVRC_MD_ATTR_PLAYING_TIME);
	a2dp_sink.set_avrc_rn_play_pos_callback(avrc_rn_play_pos_callback, 1);
#ifdef ZERO_VOLUME_FIX
	a2dp_sink.set_avrc_rn_volumechange(avrc_rn_volumechange_callback);
	a2dp_sink.set_avrc_rn_volumechange_completed(avrc_rn_volumechange_completed_callback);
#endif
	a2dp_sink.start(A2DP_SINK_NAME);

	ESP_LOGI("SETUP", "a2dp_sink started: %s", A2DP_SINK_NAME);
	delay(5);
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
#ifdef ESPOD_ENABLED
		espod.disabled = false;
#endif
#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, INVERT_LED_LOGIC(HIGH));
#endif
#ifdef ZERO_VOLUME_FIX
		volume_state = VOLUME_AFTER_CONNECTION_NOT_DEFINED;
#endif
		break;
	case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
		ESP_LOGI("A2DP_CB", "ESP_A2D_CONNECTION_STATE_DISCONNECTED, espod disabled");
#ifdef ESPOD_ENABLED
		espod.resetState();
		espod.disabled = true;
#endif
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
#ifdef ESPOD_ENABLED
		espod.playStatus = PB_STATE_PLAYING;
#endif
		ESP_LOGI("A2DP_CB", "ESP_A2D_AUDIO_STATE_STARTED, espod.playStatus = PB_STATE_PLAYING");
		break;
	case ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND:
#ifdef ESPOD_ENABLED
		espod.playStatus = PB_STATE_PAUSED;
#endif
		ESP_LOGI("A2DP_CB", "ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND, espod.playStatus = PB_STATE_PAUSED");
#ifdef ZERO_VOLUME_FIX
		nvs_set_volume(a2dp_sink.get_volume());
#endif
		break;
	case ESP_A2D_AUDIO_STATE_STOPPED:
#ifdef ESPOD_ENABLED
		espod.playStatus = PB_STATE_STOPPED;
#endif
		ESP_LOGI("A2DP_CB", "ESP_A2D_AUDIO_STATE_STOPPED, espod.playStatus = PB_STATE_STOPPED");
		break;
	}
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
	ESP_LOGV("AVRC_CB", "PlayPosition called");
#ifdef ESPOD_ENABLED
	espod.playPosition = play_pos;
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

#ifdef ESPOD_ENABLED
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
#endif

#pragma endregion