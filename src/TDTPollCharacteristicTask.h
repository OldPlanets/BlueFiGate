#pragma once
#include "BLEManager.h"

class TDTPollCharacteristicTask : public BLETask, public NimBLEClientCallbacks
{
public:
    // TDT BMS Protocol Constants
    static constexpr uint8_t TDT_HEAD = 0x7E;
    static constexpr uint8_t TDT_ALT_HEAD = 0x1E;
    static constexpr uint8_t TDT_TAIL = 0x0D;
    static constexpr uint8_t TDT_CMD_VER = 0x00;
    static constexpr uint8_t TDT_RSP_VER = 0x00;
    static constexpr uint8_t TDT_CELL_POS = 0x08;
    static constexpr int TDT_INFO_LEN = 10;
    static constexpr int MAX_CELLS = 32;
    static constexpr int MAX_TEMP_SENSORS = 8;

    static constexpr int POLL_INTERVAL = 10000;

    TDTPollCharacteristicTask(int priority, uint32_t timeout,
                             std::function<void(const TaskResult &)> callback,
                             const NimBLEAddress &deviceAddress,
                             bool bSticky = false);

    ~TDTPollCharacteristicTask();
    
    void execute() override;
    bool process() override;
    void stop() override;
    void restart() override;

    // NimBLEClientCallbacks
    void onConnect(NimBLEClient* pClient) override;
    void onDisconnect(NimBLEClient* pClient, int reason) override;
    void onConnectFail(NimBLEClient* pClient, int reason) override;

    static TDTBMSData getBMSDataFromResultTaskResult(TaskResult result);

private:
    bool connected;
    bool initialized;
    bool commandsSent;
    bool connecting;

    uint32_t nextPollTime;
    
    NimBLEClient* pBLEClient;
    NimBLERemoteCharacteristic* pReadChar;
    NimBLERemoteCharacteristic* pWriteChar;
    NimBLERemoteCharacteristic* pConfigChar;
    std::optional<TaskResult> pendingResult;
    
    // TDT Protocol specific
    std::vector<uint8_t> dataBuffer;
    int expectedLength;
    std::map<uint8_t, std::vector<uint8_t>> dataFinal;
    int currentCmdIndex;
    
    void connectToDeviceAsync();
    void initializeBMS();
    void sendCommands();
    std::vector<uint8_t> buildTDTCommand(uint8_t cmd, uint8_t cmdHead = TDT_HEAD);
    uint16_t calculateModbusCRC(const std::vector<uint8_t>& data);
    
    void onNotify(NimBLERemoteCharacteristic* pRemoteCharacteristic, 
                  uint8_t* pData, size_t length, bool isNotify);
    void processIncomingData(uint8_t* pData, size_t length);
    bool validateTDTFrame();
    void createSuccessResult();
    TDTBMSData parseTDTData();
    std::string formatBMSDataAsString(const TDTBMSData& data);
    void setErrorResult(const std::string& errorMessage);
};