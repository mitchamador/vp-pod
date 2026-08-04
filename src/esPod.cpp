#include "esPod.h"

//-----------------------------------------------------------------------
//|                           Local utilities                           |
//-----------------------------------------------------------------------
// #pragma region Local utilities
// // ESP32 is Little-Endian, iPod is Big-Endian
// template <typename T>
// T swap_endian(T u)
// {
//     static_assert(CHAR_BIT == 8, "CHAR_BIT != 8");

//     union
//     {
//         T u;
//         unsigned char u8[sizeof(T)];
//     } source, dest;

//     source.u = u;

//     for (size_t k = 0; k < sizeof(T); k++)
//         dest.u8[k] = source.u8[sizeof(T) - k - 1];

//     return dest.u;
// }

// /// @brief (Re)starts a timer and changes the interval on the fly.
// /// @param timer Timer handle to (re)start.
// /// @param time_ms New interval in milliseconds. No verification is done if this is 0! Defaults to TRACK_CHANGE_TIMEOUT.
// void startTimer(TimerHandle_t timer, unsigned long time_ms = TRACK_CHANGE_TIMEOUT)
// {
//     // If the timer is already active, it needs to be stopped without a callback call first
//     if (xTimerIsTimerActive(timer) == pdTRUE)
//     {
//         xTimerStop(timer, 0);
//     }
//     // Change the period and start the timer
//     xTimerChangePeriod(timer, pdMS_TO_TICKS(time_ms), 0);
//     xTimerStart(timer, 0);
// }

// /// @brief Stops a running timer. No status is returned if it was already stopped.
// /// @param timer Handle to the Timer that needs to be stopped.
// void stopTimer(TimerHandle_t timer)
// {
//     // If the timer is already active, it needs to be stop without a callback call first
//     if (xTimerIsTimerActive(timer) == pdTRUE)
//     {
//         xTimerStop(timer, 0);
//     }
// }
// #pragma endregion

//-----------------------------------------------------------------------
//|                      Cardinal tasks and Timers                      |
//-----------------------------------------------------------------------
#pragma region Tasks
/// @brief RX Task, sifts through the incoming serial data and compiles packets that pass the checksum and passes them to the processing Queue _cmdQueue. Also handles timeouts and can trigger state resets.
/// @param pvParameters Unused
void esPod::_rxTask(void *pvParameters)
{
    esPod *esPodInstance = static_cast<esPod *>(pvParameters);

    byte prevByte = 0x00;
    byte incByte = 0x00;
    byte buf[MAX_PACKET_SIZE] = {0x00};
    uint32_t expLength = 0;
    uint32_t cursor = 0;

    unsigned long lastByteRX = millis();   // Last time a byte was RXed in a packet
    unsigned long lastActivity = millis(); // Last time any RX activity was detected

    aapCommand cmd;

#ifdef STACK_HIGH_WATERMARK_LOG
    UBaseType_t uxHighWaterMark;
    UBaseType_t minHightWaterMark = RX_TASK_STACK_SIZE;
#endif

    while (true)
    {
// Stack high watermark logging
#ifdef STACK_HIGH_WATERMARK_LOG
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        if (uxHighWaterMark < minHightWaterMark)
        {
            minHightWaterMark = uxHighWaterMark;
            ESP_LOGI("HWM", "RX Task High Watermark: %d, used stack: %d", minHightWaterMark, RX_TASK_STACK_SIZE - minHightWaterMark);
        }
#endif

        // If the esPod is disabled, flush the RX buffer and wait for 2*RX_TASK_INTERVAL_MS before checking again
        if (esPodInstance->disabled)
        {
            while (esPodInstance->_targetSerial.available())
            {
                esPodInstance->_targetSerial.read();
            }
            vTaskDelay(pdMS_TO_TICKS(2 * RX_TASK_INTERVAL_MS));
            continue;
        }
        else // esPod is enabled, process away !
        {
            // Use of while instead of if()
            while (esPodInstance->_targetSerial.available())
            {
                // Timestamping the last activity on RX
                lastActivity = millis();
                incByte = esPodInstance->_targetSerial.read();
                // If we are not in the middle of a RX, and we receive a 0xFF 0x55, start sequence, reset expected length and position cursor
                if (prevByte == 0xFF && incByte == 0x55 && !esPodInstance->_rxIncomplete)
                {
                    lastByteRX = millis();
                    esPodInstance->_rxIncomplete = true;
                    expLength = 0;
                    cursor = 0;
                }
                else if (esPodInstance->_rxIncomplete)
                {
                    // Timestamping the last byte received
                    lastByteRX = millis();
                    // Expected length has not been received yet
                    if (expLength == 0 && cursor == 0)
                    {
                        expLength = incByte; // First byte after 0xFF 0x55
                        if (expLength > MAX_PACKET_SIZE)
                        {
                            ESP_LOGW(__func__, "Expected length is too long, discarding packet");
                            esPodInstance->_rxIncomplete = false;
                            // TODO: Send a NACK to the Accessory
                        }
                        else if (expLength == 0)
                        {
                            ESP_LOGW(__func__, "Expected length is 0, discarding packet");
                            esPodInstance->_rxIncomplete = false;
                            // TODO: Send a NACK to the Accessory
                        }
                    }
                    else // Length is already received
                    {
                        buf[cursor++] = incByte;
                        if (cursor == expLength + 1)
                        {
                            // We have received the expected length + checksum
                            esPodInstance->_rxIncomplete = false;
                            // Check the checksum
                            byte calcChecksum = esPod::_checksum(buf, expLength);
                            if (calcChecksum == incByte)
                            {
                                // Checksum is correct, send the packet to the processing queue
                                // Allocate memory for the payload so it doesn't become out of scope
                                cmd.payload = new byte[expLength];
                                cmd.length = expLength;
                                memcpy(cmd.payload, buf, expLength);
                                if (xQueueSend(esPodInstance->_cmdQueue, &cmd, pdMS_TO_TICKS(5)) == pdTRUE)
                                {
                                    ESP_LOGD(__func__, "Packet received and sent to processing queue");
                                }
                                else
                                {
                                    ESP_LOGW(__func__, "Packet received but could not be sent to processing queue. Discarding");
                                    delete[] cmd.payload;
                                    cmd.payload = nullptr;
                                    cmd.length = 0;
                                }
                            }
                            else // Checksum mismatch
                            {
                                ESP_LOGW(__func__, "Checksum mismatch, discarding packet");
                                // TODO: Send a NACK to the Accessory
                            }
                        }
                    }
                }
                else // We are not in the middle of a packet, but we received a byte
                {
                    ESP_LOGD(__func__, "Received byte 0x%02X outside of a packet, discarding", incByte);
                }
                // Always update the previous byte
                prevByte = incByte;
            }
            if (esPodInstance->_rxIncomplete && millis() - lastByteRX > INTERBYTE_TIMEOUT) // If we are in the middle of a packet and we haven't received a byte in 1s, discard the packet
            {
                ESP_LOGW(__func__, "Packet incomplete, discarding");
                esPodInstance->_rxIncomplete = false;
                // cmd.payload = nullptr;
                // cmd.length = 0;
                // TODO: Send a NACK to the Accessory
            }
            if (millis() - lastActivity > SERIAL_TIMEOUT) // If we haven't received any byte in 30s, reset the RX state
            {
                // Reset the timestamp for next Serial timeout
                lastActivity = millis();
#ifndef NO_RESET_ON_SERIAL_TIMEOUT
                ESP_LOGW(__func__, "No activity in %lu ms, resetting RX state", SERIAL_TIMEOUT);
                esPodInstance->resetState();
#endif
            }
            vTaskDelay(pdMS_TO_TICKS(RX_TASK_INTERVAL_MS));
        }
    }
}

/// @brief Processor task retrieving from the cmdQueue and processing the commands
/// @param pvParameters
void esPod::_processTask(void *pvParameters)
{
    esPod *esPodInstance = static_cast<esPod *>(pvParameters);
    aapCommand incCmd;

#ifdef STACK_HIGH_WATERMARK_LOG
    UBaseType_t uxHighWaterMark;
    UBaseType_t minHightWaterMark = PROCESS_TASK_STACK_SIZE;
#endif

    while (true)
    {
// Stack high watermark logging
#ifdef STACK_HIGH_WATERMARK_LOG
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        if (uxHighWaterMark < minHightWaterMark)
        {
            minHightWaterMark = uxHighWaterMark;
            ESP_LOGI("HWM", "Process Task High Watermark: %d, used stack: %d", minHightWaterMark, PROCESS_TASK_STACK_SIZE - minHightWaterMark);
        }
#endif

        // If the esPod is disabled, check the queue and purge it before jumping to the next cycle
        if (esPodInstance->disabled)
        {
            while (xQueueReceive(esPodInstance->_cmdQueue, &incCmd, 0) == pdTRUE) // Non blocking receive
            {
                // Do not process, just free the memory
                delete[] incCmd.payload;
                incCmd.payload = nullptr;
                incCmd.length = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(2 * PROCESS_INTERVAL_MS));
            continue;
        }
        if (xQueueReceive(esPodInstance->_cmdQueue, &incCmd, 0) == pdTRUE) // Non blocking receive
        {
            // Process the command
            esPodInstance->_processPacket(incCmd.payload, incCmd.length);
            // Free the memory allocated for the payload
            delete[] incCmd.payload;
            incCmd.payload = nullptr;
            incCmd.length = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(PROCESS_INTERVAL_MS));
    }
}

/// @brief Transmit task, retrieves from the txQueue and sends the packets over Serial at high priority but wider timing
/// @param pvParameters
void esPod::_txTask(void *pvParameters)
{
    esPod *esPodInstance = static_cast<esPod *>(pvParameters);
    aapCommand txCmd;

#ifdef STACK_HIGH_WATERMARK_LOG
    UBaseType_t uxHighWaterMark;
    UBaseType_t minHightWaterMark = TX_TASK_STACK_SIZE;
#endif

    while (true)
    {
// Stack high watermark logging
#ifdef STACK_HIGH_WATERMARK_LOG
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        if (uxHighWaterMark < minHightWaterMark)
        {
            minHightWaterMark = uxHighWaterMark;
            ESP_LOGI("HWM", "TX Task High Watermark: %d, used stack: %d", minHightWaterMark, TX_TASK_STACK_SIZE - minHightWaterMark);
        }
#endif

        // If the esPod is disabled, check the queue and purge it before jumping to the next cycle
        if (esPodInstance->disabled)
        {
            while (xQueueReceive(esPodInstance->_txQueue, &txCmd, 0) == pdTRUE)
            {
                // Do not process, just free the memory
                delete[] txCmd.payload;
                txCmd.payload = nullptr;
                txCmd.length = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(TX_INTERVAL_MS));
            continue;
        }
        if (!esPodInstance->_rxIncomplete && esPodInstance->_pendingCmdId_0x00 == 0x00 && esPodInstance->_pendingCmdId_0x03 == 0x00 && esPodInstance->_pendingCmdId_0x04 == 0x00) //_rxTask is not in the middle of a packet, there isn't a valid pending for either lingoes
        {
            // Retrieve from the queue and send the packet
            if (xQueueReceive(esPodInstance->_txQueue, &txCmd, 0) == pdTRUE)
            {
                // vTaskDelay(pdMS_TO_TICKS(TX_INTERVAL_MS));
                // Send the packet
                esPodInstance->_sendPacket(txCmd.payload, txCmd.length);
                // Free the memory allocated for the payload
                delete[] txCmd.payload;
                txCmd.payload = nullptr;
                txCmd.length = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(TX_INTERVAL_MS));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(RX_TASK_INTERVAL_MS));
        }
    }
}

/// @brief Low priority task to queue acks *outside* of the timer interrupt context
/// @param pvParameters
void esPod::_timerTask(void *pvParameters)
{
    esPod *esPodInstance = static_cast<esPod *>(pvParameters);
    TimerCallbackMessage msg;

    while (true)
    {
        if (xQueueReceive(esPodInstance->_timerQueue, &msg, 0) == pdTRUE)
        {
            if (msg.targetLingo == 0x00)
            {
                // esPodInstance->L0x00_0x02_iPodAck(iPodAck_OK, msg.cmdID);
                L0x00::_0x02_iPodAck(esPodInstance, iPodAck_OK, msg.cmdID);
            }
            else if (msg.targetLingo == 0x04)
            {
                // esPodInstance->L0x04_0x01_iPodAck(iPodAck_OK, msg.cmdID);
                L0x04::_0x01_iPodAck(esPodInstance, iPodAck_OK, msg.cmdID);
                if (msg.cmdID == esPodInstance->trackChangeAckPending)
                {
                    esPodInstance->trackChangeAckPending = 0x00;
                }
#if TOTAL_NUM_TRACKS == 3
                L0x04::_0x27_PlayStatusNotification(esPodInstance, 0x01, START_INDEX);
#endif
            }
            else if (msg.targetLingo == 0x03)
            {
                // esPodInstance->L0x04_0x01_iPodAck(iPodAck_OK, msg.cmdID);
                L0x03::_0x00_iPodAck(esPodInstance, iPodAck_OK, msg.cmdID);
                if (msg.cmdID == esPodInstance->trackChangeAckPending)
                {
                    esPodInstance->trackChangeAckPending = 0x00;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TIMER_INTERVAL_MS));
    }
}

#ifdef STATUS_NOTIFICATION_QUEUE
/// @brief Low priority task to queue acks *outside* of the timer interrupt context
/// @param pvParameters
void esPod::_statusChangeNotificationTimerTask(void *pvParameters)
{
    esPod *esPodInstance = static_cast<esPod *>(pvParameters);
    StatusChangeNotificationTimerCallbackMessage msg;
    while (true)
    {
        if (xQueueReceive(esPodInstance->_statusChangeNotificationTimerQueue, &msg, 0) == pdTRUE)
        {
            if (esPodInstance->playStatusNotificationState == NOTIF_ON) {
                if (msg.cmdID == 0x04) {
                    // Playback track position changed
                    if (esPodInstance->playStatus == PB_STATE_PLAYING
                        && esPodInstance->playStatusNotificationState == NOTIF_ON
                        && esPodInstance->trackChangeAckPending == 0x00) {
                        L0x04::_0x27_PlayStatusNotification(esPodInstance, msg.cmdID, esPodInstance->getPlayPosition());
                    }
                } else if (msg.cmdID == 0x01) {
                    // Playback track changed
                    L0x04::_0x27_PlayStatusNotification(esPodInstance, msg.cmdID,
                        esPodInstance->currentTrackIndex != 0xFFFFFFFF ? esPodInstance->currentTrackIndex : START_INDEX);
                } else if (msg.cmdID == 0x00) {
                    // Playback stopped 
                    L0x04::_0x27_PlayStatusNotification(esPodInstance, msg.cmdID);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_CHANGE_NOTIFICATION_TIMER_INTERVAL_MS));
    }

}
#endif
#pragma endregion

#pragma region Timer Callbacks

/// @brief Callback for L0x00 pending Ack timer
/// @param xTimer
void esPod::_pendingTimerCallback_0x00(TimerHandle_t xTimer)
{
    esPod *esPodInstance = static_cast<esPod *>(pvTimerGetTimerID(xTimer));
    TimerCallbackMessage msg = {esPodInstance->_pendingCmdId_0x00, 0x00};
    xQueueSendFromISR(esPodInstance->_timerQueue, &msg, NULL);
}

/// @brief Callback for L0x03 pending Ack timer
/// @param xTimer
void esPod::_pendingTimerCallback_0x03(TimerHandle_t xTimer)
{
    esPod *esPodInstance = static_cast<esPod *>(pvTimerGetTimerID(xTimer));
    TimerCallbackMessage msg = {esPodInstance->_pendingCmdId_0x03, 0x03};
    xQueueSendFromISR(esPodInstance->_timerQueue, &msg, NULL);
}

/// @brief Callback for L0x04 pending Ack timer
/// @param xTimer
void esPod::_pendingTimerCallback_0x04(TimerHandle_t xTimer)
{
    esPod *esPodInstance = static_cast<esPod *>(pvTimerGetTimerID(xTimer));
    TimerCallbackMessage msg = {esPodInstance->_pendingCmdId_0x04, 0x04};
    xQueueSendFromISR(esPodInstance->_timerQueue, &msg, NULL);
}

#ifdef STATUS_NOTIFICATION_QUEUE
/// @brief Callback for L0x04 pending Ack timer
/// @param xTimer
void esPod::_statusChangeNotificationTimerCallback(TimerHandle_t xTimer)
{
    esPod *esPodInstance = static_cast<esPod *>(pvTimerGetTimerID(xTimer));
    StatusChangeNotificationTimerCallbackMessage msg = {0x04};
    xQueueSendFromISR(esPodInstance->_statusChangeNotificationTimerQueue, &msg, NULL);
}
#endif
#pragma endregion

//-----------------------------------------------------------------------
//|                          Packet management                          |
//-----------------------------------------------------------------------
#pragma region Packet management
/// @brief //Calculates the checksum of a packet that starts from i=0 ->Lingo to i=len -> Checksum
/// @param byteArray Array from Lingo byte to Checksum byte
/// @param len Length of array (Lingo byte to Checksum byte)
/// @return Calculated checksum for comparison
byte esPod::_checksum(const byte *byteArray, uint32_t len)
{
    uint32_t tempChecksum = len;
    for (int i = 0; i < len; i++)
    {
        tempChecksum += byteArray[i];
    }
    tempChecksum = 0x100 - (tempChecksum & 0xFF);
    return (byte)tempChecksum;
}

/// @brief Composes and sends a packet over the _targetSerial
/// @param byteArray Array to send, starting with the Lingo byte and without the checksum byte
/// @param len Length of the array to send
void esPod::_sendPacket(const byte *byteArray, uint32_t len)
{
    uint32_t finalLength = len + 4;
    byte tempBuf[finalLength] = {0x00};

    tempBuf[0] = 0xFF;
    tempBuf[1] = 0x55;
    tempBuf[2] = (byte)len;
    for (uint32_t i = 0; i < len; i++)
    {
        tempBuf[3 + i] = byteArray[i];
    }
    tempBuf[3 + len] = esPod::_checksum(byteArray, len);

    _targetSerial.write(tempBuf, finalLength);
}

/// @brief Adds a packet to the transmit queue
/// @param byteArray Array of bytes to add to the queue
/// @param len Length of data in the array
void esPod::_queuePacket(const byte *byteArray, uint32_t len)
{
    aapCommand cmdToQueue;
    cmdToQueue.payload = new byte[len];
    cmdToQueue.length = len;
    memcpy(cmdToQueue.payload, byteArray, len);
    if (xQueueSend(_txQueue, &cmdToQueue, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        ESP_LOGW(__func__, "Could not queue packet");
        delete[] cmdToQueue.payload;
        cmdToQueue.payload = nullptr;
        cmdToQueue.length = 0;
    }
}

/// @brief Adds a packet to the transmit queue, but at the front for immediate processing
/// @param byteArray Array of bytes to add to the queue
/// @param len Length of data in the array
void esPod::_queuePacketToFront(const byte *byteArray, uint32_t len)
{
    aapCommand cmdToQueue;
    cmdToQueue.payload = new byte[len];
    cmdToQueue.length = len;
    memcpy(cmdToQueue.payload, byteArray, len);
    if (xQueueSendToFront(_txQueue, &cmdToQueue, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        ESP_LOGW(__func__, "Could not queue packet");
        delete[] cmdToQueue.payload;
        cmdToQueue.payload = nullptr;
        cmdToQueue.length = 0;
    }
}

/// @brief Processes a valid packet and calls the relevant Lingo processor
/// @param byteArray Checksum-validated packet starting at LingoID
/// @param len Length of valid data in the packet
void esPod::_processPacket(const byte *byteArray, uint32_t len)
{
    byte rxLingoID = byteArray[0];
    const byte *subPayload = byteArray + 1; // Squeeze the Lingo out
    uint32_t subPayloadLen = len - 1;
    switch (rxLingoID) // 0x00 is general Lingo and 0x04 is extended Lingo. Nothing else is expected from the Mini
    {
    case 0x00: // General Lingo
        ESP_LOGD(IPOD_TAG, "Lingo 0x00 Packet in processor,payload length: %d", subPayloadLen);
        L0x00::processLingo(this, subPayload, subPayloadLen);
        break;

    case 0x03: // Display Remote Lingo
        ESP_LOGD(IPOD_TAG, "Lingo 0x03 Packet in processor,payload length: %d", subPayloadLen);
        L0x03::processLingo(this, subPayload, subPayloadLen);
        break;

    case 0x04: // Extended Interface Lingo
        ESP_LOGD(IPOD_TAG, "Lingo 0x04 Packet in processor,payload length: %d", subPayloadLen);
        L0x04::processLingo(this, subPayload, subPayloadLen);
        break;

    default:
        ESP_LOGW(IPOD_TAG, "Unknown Lingo packet : L0x%02x 0x%02x", rxLingoID, byteArray[1]);
        break;
    }
}
#pragma endregion

//-----------------------------------------------------------------------
//|         Constructor, reset, attachCallback for PB control           |
//-----------------------------------------------------------------------
#pragma region Constructor, destructor, reset and external PB Contoller attach
/// @brief Constructor for the esPod class
/// @param targetSerial (Serial) stream on which the esPod will be communicating
esPod::esPod(Stream &targetSerial)
    : _targetSerial(targetSerial)
{
    // Create queues with pointer structures to byte arrays
    _cmdQueue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(aapCommand));
    _txQueue = xQueueCreate(TX_QUEUE_SIZE, sizeof(aapCommand));
    _timerQueue = xQueueCreate(TIMER_QUEUE_SIZE, sizeof(TimerCallbackMessage));
#ifdef STATUS_NOTIFICATION_QUEUE
    _statusChangeNotificationTimerQueue = xQueueCreate(STATUS_CHANGE_NOTIFICATION_TIMER_QUEUE_SIZE, sizeof(StatusChangeNotificationTimerCallbackMessage));
    if (_cmdQueue == NULL || _txQueue == NULL || _timerQueue == NULL || _statusChangeNotificationTimerQueue == NULL) // Add _timerQueue check
#else
    if (_cmdQueue == NULL || _txQueue == NULL || _timerQueue == NULL) // Add _timerQueue check
#endif
    {
        ESP_LOGE(IPOD_TAG, "Could not create queues");
    }

    // Create FreeRTOS tasks for compiling incoming commands, processing commands and transmitting commands
#ifdef STATUS_NOTIFICATION_QUEUE
    if (_cmdQueue != NULL && _txQueue != NULL && _timerQueue != NULL && _statusChangeNotificationTimerQueue != NULL) // Add _timerQueue check
#else
    if (_cmdQueue != NULL && _txQueue != NULL && _timerQueue != NULL) // Add _timerQueue check
#endif
    {
        xTaskCreatePinnedToCore(_rxTask, "RX Task", RX_TASK_STACK_SIZE, this, RX_TASK_PRIORITY, &_rxTaskHandle, 1);
        xTaskCreatePinnedToCore(_processTask, "Processor Task", PROCESS_TASK_STACK_SIZE, this, PROCESS_TASK_PRIORITY, &_processTaskHandle, 1);
        xTaskCreatePinnedToCore(_txTask, "Transmit Task", TX_TASK_STACK_SIZE, this, TX_TASK_PRIORITY, &_txTaskHandle, 1);
        xTaskCreatePinnedToCore(_timerTask, "Timer Task", TIMER_TASK_STACK_SIZE, this, TIMER_TASK_PRIORITY, &_timerTaskHandle, 1);
#ifdef STATUS_NOTIFICATION_QUEUE
        xTaskCreatePinnedToCore(_statusChangeNotificationTimerTask, "Status change notification Timer Task", STATUS_CHANGE_NOTIFICATION_TIMER_TASK_STACK_SIZE, this, STATUS_CHANGE_NOTIFICATION_TIMER_TASK_PRIORITY, &_statusChangeNotificationTimerTaskHandle, 1);
        if (_rxTaskHandle == NULL || _processTaskHandle == NULL || _txTaskHandle == NULL || _timerTaskHandle == NULL || _statusChangeNotificationTimerTaskHandle == NULL)
#else
        if (_rxTaskHandle == NULL || _processTaskHandle == NULL || _txTaskHandle == NULL || _timerTaskHandle == NULL)
#endif
        {
            ESP_LOGE(IPOD_TAG, "Could not create tasks");
        }
        else
        {
            _pendingTimer_0x00 = xTimerCreate("Pending Timer 0x00", pdMS_TO_TICKS(1000), pdFALSE, this, esPod::_pendingTimerCallback_0x00);
            _pendingTimer_0x03 = xTimerCreate("Pending Timer 0x03", pdMS_TO_TICKS(1000), pdFALSE, this, esPod::_pendingTimerCallback_0x03);
            _pendingTimer_0x04 = xTimerCreate("Pending Timer 0x04", pdMS_TO_TICKS(1000), pdFALSE, this, esPod::_pendingTimerCallback_0x04);
#ifdef STATUS_NOTIFICATION_QUEUE
            _statusChangeNotificationTimer = xTimerCreate("Status change notification Timer", pdMS_TO_TICKS(500), pdTRUE, this, esPod::_statusChangeNotificationTimerCallback);
            if (_pendingTimer_0x00 == NULL || _pendingTimer_0x03 == NULL || _pendingTimer_0x04 == NULL || _statusChangeNotificationTimer == NULL)
#else
            if (_pendingTimer_0x00 == NULL || _pendingTimer_0x03 == NULL || _pendingTimer_0x04 == NULL)
#endif
            {
                ESP_LOGE(IPOD_TAG, "Could not create timers");
            }
        }
    }
    else
    {
        ESP_LOGE(IPOD_TAG, "Could not create tasks, queues not created");
    }
}

/// @brief Destructor for the esPod class. Normally not used.
esPod::~esPod()
{
    aapCommand tempCmd;
    vTaskDelete(_rxTaskHandle);
    vTaskDelete(_processTaskHandle);
    vTaskDelete(_txTaskHandle);
    vTaskDelete(_timerTaskHandle);
#ifdef STATUS_NOTIFICATION_QUEUE
    vTaskDelete(_statusChangeNotificationTimerTaskHandle);
#endif
    // Stop timers that might be running
    stopTimer(_pendingTimer_0x00);
    stopTimer(_pendingTimer_0x03);
    stopTimer(_pendingTimer_0x04);
#ifdef STATUS_NOTIFICATION_QUEUE
    stopTimer(_statusChangeNotificationTimer);
#endif
    xTimerDelete(_pendingTimer_0x00, 0);
    xTimerDelete(_pendingTimer_0x03, 0);
    xTimerDelete(_pendingTimer_0x04, 0);
#ifdef STATUS_NOTIFICATION_QUEUE
    xTimerDelete(_statusChangeNotificationTimer, 0);
#endif
    // Remember to deallocate memory
    while (xQueueReceive(_cmdQueue, &tempCmd, 0) == pdTRUE)
    {
        delete[] tempCmd.payload;
        tempCmd.payload = nullptr;
        tempCmd.length = 0;
    }
    while (xQueueReceive(_txQueue, &tempCmd, 0) == pdTRUE)
    {
        delete[] tempCmd.payload;
        tempCmd.payload = nullptr;
        tempCmd.length = 0;
    }
    vQueueDelete(_cmdQueue);
    vQueueDelete(_txQueue);
    vQueueDelete(_timerQueue);
#ifdef STATUS_NOTIFICATION_QUEUE
    vQueueDelete(_statusChangeNotificationTimerQueue);
#endif
}

void esPod::resetState()
{

    ESP_LOGW(IPOD_TAG, "esPod resetState called");
    // State variables
    extendedInterfaceModeActive = false;

    // Metadata variables
    trackDuration = 1;
    playPosition = 0;

    // Playback Engine
    playStatus = PB_STATE_PAUSED;
    playStatusNotificationState = NOTIF_OFF;
    trackChangeAckPending = 0x00;
    shuffleStatus = 0x00;
    repeatStatus = 0x02;

    // TrackList variables
    currentTrackIndex = 0xFFFFFFFF;
#if TOTAL_NUM_TRACKS != 3
    for (uint16_t i = 0; i < TOTAL_NUM_TRACKS; i++)
        trackList[i] = 0;
    trackListPosition = 0xFFFFFFFF;
#else
    trackChangeCompletedTimestamp = 0xFFFFFFFF;
#endif

    // Mini metadata
    // _accessoryCapabilitiesReceived = false;
    // _accessoryCapabilitiesRequested = false;
    // _accessoryFirmwareReceived = false;
    // _accessoryFirmwareRequested = false;
    // _accessoryHardwareReceived = false;
    // _accessoryHardwareRequested = false;
    // _accessoryManufReceived = false;
    // _accessoryManufRequested = false;
    // _accessoryModelReceived = false;
    // _accessoryModelRequested = false;
    // _accessoryNameReceived = false;
    // _accessoryNameRequested = false;

    // Reset the queues
    aapCommand tempCmd;

    // Remember to deallocate memory
    while (xQueueReceive(_cmdQueue, &tempCmd, 0) == pdTRUE)
    {
        delete[] tempCmd.payload;
        tempCmd.payload = nullptr;
        tempCmd.length = 0;
    }
    while (xQueueReceive(_txQueue, &tempCmd, 0) == pdTRUE)
    {
        delete[] tempCmd.payload;
        tempCmd.payload = nullptr;
        tempCmd.length = 0;
    }
    xQueueReset(_cmdQueue);
    xQueueReset(_txQueue);

    // Stop timers
    stopTimer(_pendingTimer_0x00);
    stopTimer(_pendingTimer_0x03);
    stopTimer(_pendingTimer_0x04);
#ifdef STATUS_NOTIFICATION_QUEUE
    stopTimer(_statusChangeNotificationTimer);
#endif
    _pendingCmdId_0x00 = 0x00;
    _pendingCmdId_0x03 = 0x00;
    _pendingCmdId_0x04 = 0x00;
}

void esPod::attachPlayControlHandler(playStatusHandler_t playHandler)
{
    _playStatusHandler = playHandler;
    ESP_LOGD(IPOD_TAG, "PlayControlHandler attached.");
}

uint32_t esPod::getPlayPosition() {
#ifdef TRACK_POSITION_FIX    
    if (playPosition != 0) {
        return playPosition;
    }
    uint32_t calculatedPlayPosition = (uint32_t) (rawAudioDataBytesReceived / (BYTES_PER_SECOND / 1000.0));
    if (calculatedPlayPosition > 500) {
        return calculatedPlayPosition;
    }
    return 0;
#else
    return playPosition;
#endif
}

#pragma endregion

