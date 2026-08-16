#pragma once

// vpPod
#ifdef VPPOD
#define ZERO_VOLUME_FIX
#define TRACK_POSITION_FIX
#define TOTAL_NUM_TRACKS 3
#define SINGLE_DB_CAT_TRACKS
#define START_INDEX 1
#define ESPIPOD_NAME "vp-pod"
#define TRACK_CHANGE_NOTIFICATION_TIMEOUT 750
#define FIRST_TIME_TRACK_CHANGE_NOTIFICATION_TIMEOUT 2000
#define SKIP_PLAYCURRENT_TIMEOUT 1500
#define FORCED_TRACK_CHANGE_TIMEOUT 1500
// Seek mode (fast_forward()/rewind() vs. volume_up()/volume_down() as an
// iOS workaround - AVRCP FF/REW clicks are simply ignored there) is now a
// runtime flag (esPod::_seekAsVolume), not a build-time #define - see
// SEEK_MODE_TOGGLE_WINDOW_MS below for how it's flipped in the field.
#ifndef SEEK_MODE_DEFAULT_VOLUME
#define SEEK_MODE_DEFAULT_VOLUME false
#endif
// Toggling Shuffle twice within this window (off->on->off or on->off->on)
// flips the seek mode at runtime - a hidden gesture on existing MMI
// controls, so no web UI/settings storage is needed for this one flag.
#ifndef SEEK_MODE_TOGGLE_WINDOW_MS
#define SEEK_MODE_TOGGLE_WINDOW_MS 3000
#endif

#ifndef ARDUINO
#define LED_BUILTIN 2
#endif

// #define USE_ESP_IDF_SERIAL
// #define USE_ESP_IDF_TIME
// #define USE_ESP_IDF_GPIO

//@DEPRECATED
//#define AUTOPLAY_AFTER_CONNECT

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

#if TOTAL_NUM_TRACKS == 3

#ifndef TRACK_CHANGE_NOTIFICATION_TIMEOUT
#define TRACK_CHANGE_NOTIFICATION_TIMEOUT 500
#endif

#ifndef SKIP_PLAYCURRENT_TIMEOUT
#define SKIP_PLAYCURRENT_TIMEOUT 1500
#endif

#ifndef FORCED_TRACK_CHANGE_TIMEOUT
#define FORCED_TRACK_CHANGE_TIMEOUT 1500
#endif

#endif