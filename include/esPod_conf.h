#pragma once

// vpPod
#ifdef VPPOD

#define TOTAL_NUM_TRACKS 3
#define SINGLE_DB_CAT_TRACKS
#define START_INDEX 1
#define ESPIPOD_NAME "vp-pod"

#endif // vpPod

#ifndef ARDUINO
#define LED_BUILTIN 2
#endif

// #define USE_ESP_IDF_SERIAL
// #define USE_ESP_IDF_TIME
// #define USE_ESP_IDF_GPIO

// Seek mode is now a runtime flag (esPod::_seekMode, SeekMode enum in
// esPod.h), not a build-time #define - see SHUFFLE_TOGGLE_WINDOW_MS below
// for how it's cycled in the field.
#ifndef SEEK_MODE_DEFAULT
#define SEEK_MODE_DEFAULT SeekMode::FastForwardRewindPressHoldRelease
#endif
// Toggling Shuffle twice within this window (off->on->off or on->off->on)
// calls shuffleSwitch().
#ifndef SHUFFLE_TOGGLE_WINDOW_MS
#define SHUFFLE_TOGGLE_WINDOW_MS 3000
#endif
// default value for TRACK_POSITION_FIX
#ifndef TRACK_POSITION_FIX_DEFAULT
#define TRACK_POSITION_FIX_DEFAULT true
#endif
// default value for ZERO_VOLUME_FIX
#ifndef ZERO_VOLUME_FIX_DEFAULT
#define ZERO_VOLUME_FIX_DEFAULT true
#endif
// default value for USE_PEER_NAME
#ifndef USE_PEER_NAME_DEFAULT
#define USE_PEER_NAME_DEFAULT true
#endif
// default valut for TRANSLIT_TRACK_TITLE
#ifndef TRANSLIT_TRACK_TITLE_DEFAULT
#define TRANSLIT_TRACK_TITLE_DEFAULT true
#endif

// How long esPod stays "Suspended" (still enabled, DCD held, UART still
// answered) after an unexpected BT disconnect before finally going
// Disabled. Stored in seconds (not ms) specifically so it fits int16_t in
// NVS with headroom - covers both a brief BT blip and a longer "MMI hasn't
// timed out its own idle-off yet" gap without needing a bigger storage type.
#ifndef SUSPEND_TIMEOUT_S_DEFAULT
#define SUSPEND_TIMEOUT_S_DEFAULT 900
#endif
// Settle pause between forcing Disabled and re-entering Enabled when a
// *different* peer reconnects while Suspended - gives the MMI a guaranteed
// falling+rising edge on DCD so it re-probes the "new" iPod from scratch.
#ifndef REENABLE_SETTLE_MS_DEFAULT
#define REENABLE_SETTLE_MS_DEFAULT 2500
#endif


// A2DP instance name
#ifndef A2DP_SINK_NAME
#define A2DP_SINK_NAME "espiPod"
#endif

// ESPiPod instance name
#ifndef ESPIPOD_NAME
#define ESPIPOD_NAME "ipodESP32"
#endif

// Serial settings
#ifndef MAX_PACKET_SIZE
#define MAX_PACKET_SIZE 1024
#endif
#ifndef SERIAL_TIMEOUT
#define SERIAL_TIMEOUT 2500
#endif
#ifndef INTERBYTE_TIMEOUT
#define INTERBYTE_TIMEOUT 500
#endif
// FreeRTOS Queues
#ifndef CMD_QUEUE_SIZE
#define CMD_QUEUE_SIZE 32
#endif
#ifndef TX_QUEUE_SIZE
#define TX_QUEUE_SIZE 32
#endif
#ifndef TIMER_QUEUE_SIZE
#define TIMER_QUEUE_SIZE 10
#endif
#ifndef STATUS_CHANGE_NOTIFICATION_TIMER_QUEUE_SIZE
#define STATUS_CHANGE_NOTIFICATION_TIMER_QUEUE_SIZE 10
#endif
// RX Task settings
#ifndef RX_TASK_STACK_SIZE
#define RX_TASK_STACK_SIZE 4096
#endif
#ifndef RX_TASK_PRIORITY
#define RX_TASK_PRIORITY 2
#endif
#ifndef RX_TASK_INTERVAL_MS
#define RX_TASK_INTERVAL_MS 10
#endif
// Process Task settings
#ifndef PROCESS_TASK_STACK_SIZE
#define PROCESS_TASK_STACK_SIZE 4096
#endif
#ifndef PROCESS_TASK_PRIORITY
#define PROCESS_TASK_PRIORITY 5
#endif
#ifndef PROCESS_INTERVAL_MS
#define PROCESS_INTERVAL_MS 15
#endif
// TX Task settings
#ifndef TX_TASK_STACK_SIZE
#define TX_TASK_STACK_SIZE 4096
#endif
#ifndef TX_TASK_PRIORITY
#define TX_TASK_PRIORITY 20
#endif
#ifndef TX_INTERVAL_MS
#define TX_INTERVAL_MS 20
#endif
// Timer Task settings
#ifndef TIMER_TASK_STACK_SIZE
#define TIMER_TASK_STACK_SIZE 4096
#endif
#ifndef TIMER_TASK_PRIORITY
#define TIMER_TASK_PRIORITY 1
#endif
#ifndef TIMER_INTERVAL_MS
#define TIMER_INTERVAL_MS 5
#endif

// General iPod settings

#ifndef START_INDEX
#define START_INDEX 0
#endif

#ifndef TOTAL_NUM_TRACKS
#define TOTAL_NUM_TRACKS 3000
#endif

#ifndef TRACK_CHANGE_TIMEOUT
#define TRACK_CHANGE_TIMEOUT 1100
#endif

#ifndef AVRC_RECEIVE_METADATA_TIMEOUT
#define AVRC_RECEIVE_METADATA_TIMEOUT 250
#endif

#ifndef MAX_SEEK_HOLD_TICKS
#define MAX_SEEK_HOLD_TICKS 120 // 120 * 250ms = 30s watchdog
#endif

#if TOTAL_NUM_TRACKS == 3

#ifndef TRACK_CHANGE_NOTIFICATION_TIMEOUT
#define TRACK_CHANGE_NOTIFICATION_TIMEOUT 750
#endif

#ifndef FIRST_TIME_TRACK_CHANGE_NOTIFICATION_TIMEOUT
#define FIRST_TIME_TRACK_CHANGE_NOTIFICATION_TIMEOUT 2000
#endif

#ifndef SKIP_PLAYCURRENT_TIMEOUT
#define SKIP_PLAYCURRENT_TIMEOUT 1500
#endif

#ifndef FORCED_TRACK_CHANGE_TIMEOUT
#define FORCED_TRACK_CHANGE_TIMEOUT 1500
#endif

#endif