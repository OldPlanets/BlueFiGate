#include "TDTPollCharacteristicTask.h"
#include "Log.h"

// TDTPollCharacteristicTask implementation
TDTPollCharacteristicTask::TDTPollCharacteristicTask(int priority, uint32_t timeout,
                                                     std::function<void(const TaskResult &)> callback,
                                                     const NimBLEAddress &deviceAddress,
                                                     bool bSticky)
    : BLETask(TaskType::TDT_POLL_CHARACTERISTIC, priority, timeout, NimBLEUUID(), deviceAddress, callback, bSticky),
      connected(false), initialized(false), commandsSent(false)
{
    // Reset state
    dataBuffer.clear();
    expectedLength = 0;
    dataFinal.clear();
    currentCmdIndex = 0;
    nextPollTime = 0;
}

TDTPollCharacteristicTask::~TDTPollCharacteristicTask()
{
    // Cleanup handled by smart pointers and containers
}

void TDTPollCharacteristicTask::execute()
{
    // Reset state
    connected = false;
    initialized = false;
    commandsSent = false;
    connecting = false;
    pBLEClient = nullptr;
    pReadChar = nullptr;
    pWriteChar = nullptr;
    pConfigChar = nullptr;
    pendingResult = std::nullopt;
    dataBuffer.clear();
    expectedLength = 0;
    dataFinal.clear();
    currentCmdIndex = 0;
    nextPollTime = 0;

    if (deviceAddress.isNull())
    {
        Log.error("TDTPollCharacteristicTask: No device address provided");
        setErrorResult("No device address provided");
    }
    else
    {
        Log.info("TDTPollCharacteristicTask: Using known device address %s", deviceAddress.toString().c_str());
        connectToDeviceAsync();
    }
}

void TDTPollCharacteristicTask::connectToDeviceAsync()
{
    pBLEClient = NimBLEDevice::createClient();
    pBLEClient->setConnectTimeout(10000);
    pBLEClient->setClientCallbacks(this);
    
    connecting = true;
    if (!pBLEClient->connect(deviceAddress, true, true))
    { 
        Log.error("TDTPollCharacteristicTask: Failed to start connection to device %s", deviceAddress.toString().c_str());
        connecting = false;
        setErrorResult("Failed to start connection to device");
    } else {
        Log.debug("TDTPollCharacteristicTask: Connection attempt started for device %s", deviceAddress.toString().c_str());
    }
}

void TDTPollCharacteristicTask::initializeBMS()
{
    // Get the service
    NimBLERemoteService* pService = pBLEClient->getService("fff0");
    if (pService == nullptr)
    {
        Log.error("TDTPollCharacteristicTask: Service %s not found", serviceUuid.toString().c_str());
        setErrorResult("Service not found");
        return;
    }
    
    // Get all characteristics
    pWriteChar = pService->getCharacteristic("fff2");
    if (pWriteChar == nullptr)
    {
        Log.error("TDTPollCharacteristicTask: Write characteristic %s not found", "fff2");
        setErrorResult("Write characteristic not found");
        return;
    }
    
    pReadChar = pService->getCharacteristic("fff1");
    if (pReadChar == nullptr)
    {
        Log.error("TDTPollCharacteristicTask: Read characteristic %s not found", "fff1");
        setErrorResult("Read characteristic not found");
        return;
    }
    
    pConfigChar = pService->getCharacteristic("fffa");
    if (pConfigChar == nullptr)
    {
        Log.error("TDTPollCharacteristicTask: Config characteristic %s not found", "fffa");
        setErrorResult("Config characteristic not found");
        return;
    }
    
    // Initialize the BMS connection with "HiLink"
    const char* initData = "HiLink";
    if (!pConfigChar->writeValue((uint8_t*)initData, strlen(initData), false))
    {
        Log.error("TDTPollCharacteristicTask: Failed to write HiLink initialization");
        setErrorResult("Failed to initialize BMS connection");
        return;
    }
    
    // Read the config characteristic to verify initialization
    std::string configValue = pConfigChar->readValue();
    if (configValue.length() > 0 && (uint8_t)configValue[0] != 0x01)
    {
        Log.warn("TDTPollCharacteristicTask: BMS initialization returned: 0x%02X", (uint8_t)configValue[0]);
    }
    
    Log.debug("TDTPollCharacteristicTask: BMS initialized successfully");

    // Register for notifications on the read characteristic
    if (pReadChar->canNotify())
    {
        if (!pReadChar->subscribe(true, [this](NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
            this->onNotify(pChar, pData, length, isNotify);
        }))
        {
            Log.error("TDTPollCharacteristicTask: Failed to subscribe to notifications");
            setErrorResult("Failed to subscribe to notifications");
            return;
        }
    }
    else
    {
        Log.error("TDTPollCharacteristicTask: Read characteristic does not support notifications");
        setErrorResult("Read characteristic does not support notifications");
        return;
    }
    

    initialized = true;
}

void TDTPollCharacteristicTask::sendCommands()
{
    std::vector<uint8_t> commands = {0x8C, 0x8D}; // Basic commands for TDT
    
    // Try different command headers if needed
    std::vector<uint8_t> cmdHeads = {TDT_ALT_HEAD /*, TDT_HEAD*/};
    
    currentCmdIndex = 0;
    
    for (uint8_t cmdHead : cmdHeads)
    {
        bool success = true;
        for (uint8_t cmd : commands)
        {
            std::vector<uint8_t> frame = buildTDTCommand(cmd, cmdHead);
            
            if (!pWriteChar->writeValue(frame.data(), frame.size(), false))
            {
                Log.error("TDTPollCharacteristicTask: Failed to write command 0x%02X", cmd);
                success = false;
                break;
            }
            
            //Log.debug("TDTPollCharacteristicTask: Sent command 0x%02X with header 0x%02X", cmd, cmdHead);
            delay(100);
        }
        
        if (success)
        {
            commandsSent = true;
            //Log.debug("TDTPollCharacteristicTask: All commands sent successfully");
            return;
        }
    }
    
    setErrorResult("Failed to send commands to BMS");
}

std::vector<uint8_t> TDTPollCharacteristicTask::buildTDTCommand(uint8_t cmd, uint8_t cmdHead)
{
    std::vector<uint8_t> frame;
    
    // Build TDT command frame: [HEAD][VER][0x1][0x3][0x0][CMD][LEN_H][LEN_L][CRC_H][CRC_L][TAIL]
    frame.push_back(cmdHead);           // Command head
    frame.push_back(TDT_CMD_VER);       // Version (0x00)
    frame.push_back(0x01);              // Fixed
    frame.push_back(0x03);              // Fixed
    frame.push_back(0x00);              // Error code
    frame.push_back(cmd);               // Command
    frame.push_back(0x00);              // Data length high (no additional data)
    frame.push_back(0x00);              // Data length low
    
    // Calculate CRC-16 MODBUS for the frame (excluding CRC and tail)
    uint16_t crc = calculateModbusCRC(frame);
    frame.push_back((crc >> 8) & 0xFF); // CRC high byte
    frame.push_back(crc & 0xFF);        // CRC low byte
    frame.push_back(TDT_TAIL);          // Tail (0x0D)
    
    return frame;
}

uint16_t TDTPollCharacteristicTask::calculateModbusCRC(const std::vector<uint8_t>& data)
{
    uint16_t crc = 0xFFFF;
    
    for (uint8_t byte : data)
    {
        crc ^= byte;
        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

void TDTPollCharacteristicTask::onConnect(NimBLEClient* pClient)
{
    Log.info("TDTPollCharacteristicTask: Connected to device %s", deviceAddress.toString().c_str());
    connected = true;
    connecting = false;
}

void TDTPollCharacteristicTask::onDisconnect(NimBLEClient* pClient, int reason)
{
    Log.info("TDTPollCharacteristicTask: Disconnected from device, reason: %d", reason);
    
    if ((connecting || (connected && !commandsSent)) && !pendingResult.has_value()) {
        setErrorResult("Disconnected before operation completed");
    }
    connected = false;
}

bool TDTPollCharacteristicTask::process()
{
    if (pendingResult.has_value())
    {
        bool bResult;
        if (!isSticky() || pendingResult->status != TaskStatus::SUCCESS)
        {
            if (pBLEClient)
            {
                pBLEClient->setClientCallbacks(nullptr);
                NimBLEDevice::deleteClient(pBLEClient);
                pBLEClient = nullptr;
            }
            connecting = false;
            connected = false;
            bResult = true;
        }
        else
        {
            setStartTime(0); // disable timeout
            nextPollTime = millis() + POLL_INTERVAL;
            bResult = false;
        }
        
        complete(pendingResult.value());
        pendingResult = std::nullopt;
        return bResult;
    }

    if (isSticky() && nextPollTime > 0 && millis() > nextPollTime)
    {
        nextPollTime = 0;
        setStartTime(millis()); // restart the timeout timer
        commandsSent = false;
        Log.debug("TDTPollCharacteristicTask: Repolling");
    }
    
    if (connected && !initialized)
    {
        initializeBMS();
    }
    else if (connected && initialized && !commandsSent)
    {
        sendCommands();
    }


    return false;
}

void TDTPollCharacteristicTask::onConnectFail(NimBLEClient* pClient, int reason)
{
    Log.warn("TDTPollCharacteristicTask: Connection to device %s failed, reason: %d", 
             deviceAddress.toString().c_str(), reason);
    connecting = false;
    std::string errorMsg = "Connection failed, reason: " + std::to_string(reason);
    setErrorResult(errorMsg);
}

void TDTPollCharacteristicTask::stop()
{
    if (pBLEClient)
    {
        pBLEClient->setClientCallbacks(nullptr);
        NimBLEDevice::deleteClient(pBLEClient);
        pBLEClient = nullptr;
    }
    connecting = false;
    connected = false;
}

void TDTPollCharacteristicTask::restart()
{
    stop();
    pendingResult.reset();
    setStartTime(millis());
    execute();
}


void TDTPollCharacteristicTask::onNotify(NimBLERemoteCharacteristic* pRemoteCharacteristic, 
                                        uint8_t* pData, size_t length, bool isNotify)
{
    if (pRemoteCharacteristic->getUUID().equals(NimBLEUUID("fff1")))
    {
        //Log.debug("TDTPollCharacteristicTask: Received notification, length: %d", length);
        processIncomingData(pData, length);
    }
}

void TDTPollCharacteristicTask::processIncomingData(uint8_t* pData, size_t length)
{
    // Check if this is the start of a new frame
    if (length > TDT_INFO_LEN && pData[0] == TDT_HEAD && dataBuffer.size() >= expectedLength)
    {
        // New frame starting, calculate expected length
        expectedLength = TDT_INFO_LEN + (pData[6] << 8) + pData[7];
        dataBuffer.clear();
        //Log.debug("TDTPollCharacteristicTask: New frame detected, expected length: %d", expectedLength);
    }
    
    // Append data to buffer
    dataBuffer.insert(dataBuffer.end(), pData, pData + length);
    
    //Log.debug("TDTPollCharacteristicTask: Buffer size: %d, expected: %d", dataBuffer.size(), expectedLength);
    
    // Check if we have enough data
    if (dataBuffer.size() < std::max(TDT_INFO_LEN, expectedLength))
    {
        return; // Wait for more data
    }
    
    // Validate frame
    if (!validateTDTFrame())
    {
        return; // Invalid frame, wait for more data
    }
    
    // Extract command ID and store the frame
    uint8_t cmdId = dataBuffer[5];
    dataFinal[cmdId] = dataBuffer;
    
    //Log.debug("TDTPollCharacteristicTask: Stored frame for command 0x%02X", cmdId);
    
    // Check if we have received all expected responses
    if (dataFinal.size() >= 2) // We expect responses for 0x8C and 0x8D
    {
        // Create the result with all collected data
        createSuccessResult();
    }
}

bool TDTPollCharacteristicTask::validateTDTFrame()
{
    // Check frame end
    if (dataBuffer.back() != TDT_TAIL)
    {
        Log.debug("TDTPollCharacteristicTask: Invalid frame end: 0x%02X", dataBuffer.back());
        return false;
    }
    
    // Check frame version
    if (dataBuffer[1] != TDT_RSP_VER)
    {
        Log.debug("TDTPollCharacteristicTask: Unknown frame version: 0x%02X", dataBuffer[1]);
        return false;
    }
    
    // Check error code
    if (dataBuffer[4] != 0)
    {
        Log.debug("TDTPollCharacteristicTask: BMS reported error code: 0x%02X", dataBuffer[4]);
        return false;
    }
    
    // Validate CRC
    std::vector<uint8_t> frameData(dataBuffer.begin(), dataBuffer.end() - 3);
    uint16_t calculatedCRC = calculateModbusCRC(frameData);
    uint16_t receivedCRC = (dataBuffer[dataBuffer.size() - 3] << 8) | dataBuffer[dataBuffer.size() - 2];
    
    if (calculatedCRC != receivedCRC)
    {
        Log.debug("TDTPollCharacteristicTask: Invalid checksum 0x%04X != 0x%04X", receivedCRC, calculatedCRC);
        return false;
    }
    
    //Log.debug("TDTPollCharacteristicTask: Frame validation successful");
    return true;
}

void TDTPollCharacteristicTask::createSuccessResult()
{
    TaskResult result;
    result.status = TaskStatus::SUCCESS;
    result.deviceAddress = deviceAddress;
    
    TDTBMSData bmsData = parseTDTData();
    
    result.dataLength = sizeof(TDTBMSData);
    result.data = std::shared_ptr<uint8_t[]>(new uint8_t[result.dataLength]);
    memcpy(result.data.get(), &bmsData, result.dataLength);
    
    result.errorMessage = formatBMSDataAsString(bmsData);
    
    Log.info("TDTPollCharacteristicTask: Successfully parsed TDT BMS data: %s", result.errorMessage.c_str());
    pendingResult = result;
}

TDTBMSData TDTPollCharacteristicTask::getBMSDataFromResultTaskResult(const TaskResult result)
{
    TDTBMSData bmsData;
    if (result.status == TaskStatus::SUCCESS && result.dataLength == sizeof(TDTBMSData))
    {
        memcpy(&bmsData, result.data.get(), result.dataLength);
    }
    return bmsData;
}

TDTBMSData TDTPollCharacteristicTask::parseTDTData()
{
    TDTBMSData bmsData = {};
    
    // Ensure we have the main data packet from the BMS
    if (dataFinal.find(0x8C) == dataFinal.end())
    {
        Log.error("TDTPollCharacteristicTask: Missing 0x8C response data");
        return bmsData;
    }
    
    const std::vector<uint8_t>& mainData = dataFinal.at(0x8C);
    const int TDT_CELL_POS = 8;

    // --- Parse Cell and Temperature Sensor Counts ---
    if (mainData.size() <= TDT_CELL_POS) return bmsData;

    bmsData.cellCount = mainData[TDT_CELL_POS];
    if (mainData.size() > TDT_CELL_POS + bmsData.cellCount * 2 + 1)
    {
        bmsData.tempSensorCount = mainData[TDT_CELL_POS + bmsData.cellCount * 2 + 1];
    }

    // --- Parse Cell Voltages
    int cellVoltageStart = TDT_CELL_POS + 1;
    for (int i = 0; i < bmsData.cellCount && i < MAX_CELLS; i++)
    {
        if (mainData.size() >= cellVoltageStart + (i + 1) * 2)
        {
            uint16_t voltage = (mainData[cellVoltageStart + i * 2] << 8) | mainData[cellVoltageStart + i * 2 + 1];
            bmsData.cellVoltages[i] = voltage; // in mV
        }
    }
    
    // --- Parse Temperature Values
    int tempStart = TDT_CELL_POS + bmsData.cellCount * 2 + 2;
    for (int i = 0; i < bmsData.tempSensorCount && i < MAX_TEMP_SENSORS; i++)
    {
        if (mainData.size() >= tempStart + (i + 1) * 2)
        {
            uint16_t tempRaw = (mainData[tempStart + i * 2] << 8) | mainData[tempStart + i * 2 + 1];
            // Correctly convert from 0.1 Kelvin to Celsius
            bmsData.temperatures[i] = (static_cast<float>(tempRaw) - 2731.0f); // in °C * 10
        }
    }
    
    int dataStart = TDT_CELL_POS + bmsData.cellCount * 2 + bmsData.tempSensorCount * 2 + 2;
    
    // Current (relative offset 0)
    if (mainData.size() >= dataStart + 2)
    {
        uint16_t currentRaw = (mainData[dataStart + 0] << 8) | mainData[dataStart + 1];
        bmsData.current = (currentRaw & 0x3FFF) * ((currentRaw >> 15) ? -1.0f : 1.0f); // in A * 10
    }
    
    // Voltage (relative offset 2)
    if (mainData.size() >= dataStart + 4)
    {
        bmsData.voltage = ((mainData[dataStart + 2] << 8) | mainData[dataStart + 3]); // in V * 100
    }
    
    // Cycle charge (relative offset 4)
    if (mainData.size() >= dataStart + 6)
    {
        bmsData.cycleCharge = ((mainData[dataStart + 4] << 8) | mainData[dataStart + 5]) / 10.0f;
    }
    
    // Cycles (relative offset 8)
    if (mainData.size() >= dataStart + 10)
    {
        bmsData.cycles = (mainData[dataStart + 8] << 8) | mainData[dataStart + 9];
    }
    
    // Battery level (relative offset 13)
    if (mainData.size() >= dataStart + 14)
    {
        bmsData.batteryLevel = mainData[dataStart + 13]; // in %
    }
    
    if (dataFinal.count(0x8D))
    {
        const std::vector<uint8_t>& problemData = dataFinal.at(0x8D);
        int problemCodePos = TDT_CELL_POS + bmsData.cellCount + bmsData.tempSensorCount + 6;
        
        if (problemData.size() >= problemCodePos + 2)
        {
            bmsData.problemCode = (problemData[problemCodePos] << 8) | problemData[problemCodePos + 1];
        }
    }
    
    return bmsData;
}

std::string TDTPollCharacteristicTask::formatBMSDataAsString(const TDTBMSData& data)
{
    std::string result = "TDT BMS Data: ";
    result += "Voltage=" + std::to_string(data.voltage / 100.0f) + "V, ";
    result += "Current=" + std::to_string(data.current / 10.0f) + "A, ";
    result += "SOC=" + std::to_string(data.batteryLevel) + "%, ";
    result += "Cycles=" + std::to_string(data.cycles) + ", ";
    result += "Cells=" + std::to_string(data.cellCount) + ", ";
    result += "TempSensors=" + std::to_string(data.tempSensorCount);
    
    if (data.problemCode != 0)
    {
        result += ", Problem=0x" + std::to_string(data.problemCode);
    }
    
    return result;
}

void TDTPollCharacteristicTask::setErrorResult(const std::string& errorMessage)
{
    TaskResult result;
    result.status = TaskStatus::ERROR;
    result.errorMessage = errorMessage;
    pendingResult = result;
}