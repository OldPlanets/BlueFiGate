#pragma once
#include <Arduino.h>
#include <functional>
#include <queue>
#include <string>
#include <map>
#include <memory>
#include <NimBLEDevice.h>
#include <Preferences.h>

struct TDTBMSData
{
    uint8_t cellCount = 0;
    uint8_t tempSensorCount = 0;
    uint16_t cellVoltages[4] = {0};       // in mV
    int16_t temperatures[4] = {0}; // in 0.1°C
    uint16_t voltage = 0;                         // in 0.01V
    int16_t current = 0;                          // in 0.1A
    uint16_t cycleCharge = 0;                     // in 0.1Ah
    uint8_t batteryLevel = 0;                     // in %
    uint16_t cycles = 0;
    uint16_t problemCode = 0;
};

enum class TaskType
{
    TDT_POLL_CHARACTERISTIC,
};

enum class TaskStatus
{
    SUCCESS,
    ERROR,
    TIMEOUT,
    CANCELLED
};

struct TaskResult
{
    TaskStatus status;
    std::string errorMessage;
    std::shared_ptr<uint8_t[]> data = nullptr;
    size_t dataLength = 0;
    std::string deviceName;
    NimBLEAddress deviceAddress;
};

class BLETask
{
public:
    BLETask(TaskType type, int priority, uint32_t timeout, const NimBLEUUID &serviceUuid, const NimBLEAddress &deviceAddress,
            std::function<void(const TaskResult &)> callback, bool bSticky = false);
    virtual ~BLETask() = default;

    TaskType getType() const { return type; }
    int getPriority() const { return priority; }
    uint32_t getTimeout() const { return timeout; }
    uint32_t getStartTime() const { return startTime; }
    bool isSticky() const { return bSticky; }
    void setStartTime(uint32_t time) { startTime = time; }
    void complete(const TaskResult &result);
    NimBLEAddress getDeviceAddress() const { return deviceAddress; };
    const NimBLEUUID &getServiceUuid() const { return serviceUuid; };

    static const char *getResultLabel(const TaskStatus &status);

    virtual void execute() = 0;
    virtual bool process() = 0;
    virtual void stop() = 0;
    virtual void restart() {};

protected:
    TaskType type;
    int priority;
    uint32_t timeout;
    uint32_t startTime;
    bool bSticky;

    NimBLEAddress deviceAddress;
    NimBLEUUID serviceUuid;

    std::function<void(const TaskResult &)> callback;
};

struct TaskCompare
{
    bool operator()(const std::shared_ptr<BLETask> &a, const std::shared_ptr<BLETask> &b)
    {
        return a->getPriority() < b->getPriority();
    }
};

namespace std
{
    template <>
    struct hash<NimBLEUUID>
    {
        std::size_t operator()(const NimBLEUUID &uuid) const
        {
            return hash<std::string>()(uuid.toString());
        }
    };
}

class BLEManager
{
public:
    BLEManager();

    void queueTask(std::shared_ptr<BLETask> task);
    void queueTDTPollCharacteristicTask(int priority, uint32_t timeout,
                                        std::function<void(const TaskResult &)> callback,
                                        const NimBLEAddress &deviceAddress, bool bSticky);
    void close();
    void process();
    void init(bool bIsReset);
    bool isBusy() const;
    std::string uuidToShortKey(const NimBLEUUID &uuid);

protected:
    NimBLEAddress getKnownDevice(const NimBLEUUID &serviceUuid);

private:
    std::priority_queue<std::shared_ptr<BLETask>,
                        std::vector<std::shared_ptr<BLETask>>,
                        TaskCompare>
        taskQueue;
    std::shared_ptr<BLETask> currentTask;
    std::unordered_map<NimBLEUUID, NimBLEAddress> m_knownDeviceMap;
    bool busy;
    bool initialized;
    Preferences m_prefs;
};