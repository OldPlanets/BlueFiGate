#pragma once
#include <Arduino.h>
#include <functional>
#include <queue>
#include <string>
#include <map>
#include <memory>
#include <NimBLEDevice.h>
#include <Preferences.h>

enum class TaskType
{
    FIND_DEVICE,
    POLL_CHARACTERISTIC,
    TDT_POLL_CHARACTERISTIC,
    // Future task types can be added here
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
    const NimBLEUUID& getServiceUuid() const { return serviceUuid; };

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

class FindDeviceTask : public BLETask, public NimBLEScanCallbacks
{
public:
    FindDeviceTask(int priority, uint32_t timeout,
                   std::function<void(const TaskResult &)> callback,
                   const NimBLEUUID &serviceUuid, const NimBLEAddress &deviceAddress);
    ~FindDeviceTask();
    void execute() override;
    bool process() override;
    void stop() override;

    // NimBLEScanCallbacks methods
    void onDiscovered(const NimBLEAdvertisedDevice *advertisedDevice) override;
    void onScanEnd(const NimBLEScanResults &scanResults, int reason) override;

private:
    
    bool deviceFound;
    bool scanning;
    NimBLEUUID serviceUuid;
    NimBLEAdvertisedDevice *foundDevice;
};

class PollCharacteristicTask : public BLETask, NimBLEClientCallbacks
{
public:
    PollCharacteristicTask(int priority, uint32_t timeout,
                           std::function<void(const TaskResult &)> callback,
                           const NimBLEAddress &deviceAddress,
                           const NimBLEUUID &serviceUuid,
                           const NimBLEUUID &writeCharUuid,
                           const NimBLEUUID &readCharUuid,
                           const uint8_t *data,
                           size_t dataLength);
    ~PollCharacteristicTask();
    void execute() override;
    bool process() override;
    void stop() override;

    void onConnect(NimBLEClient* pClient) override;
    void onDisconnect(NimBLEClient* pClient, int reason) override;
    void onConnectFail(NimBLEClient* pClient, int reason) override;

private:
    NimBLEUUID writeCharUuid;
    NimBLEUUID readCharUuid;
    std::shared_ptr<uint8_t[]> data;
    size_t dataLength;
    bool connected;
    bool written;
    bool connecting;
    std::shared_ptr<FindDeviceTask> findTask;
    NimBLEClient* pBLEClient;
    NimBLERemoteCharacteristic* pReadChar;
    NimBLERemoteCharacteristic* pWriteChar;
    std::optional<TaskResult> pendingResult;
    
    void onNotify(NimBLERemoteCharacteristic* pRemoteCharacteristic, 
                 uint8_t* pData, size_t length, bool isNotify);
    void connectToDeviceAsync();
    void setErrorResult(const std::string& errorMessage);
    void writeCommand();
};

struct TaskCompare
{
    bool operator()(const std::shared_ptr<BLETask> &a, const std::shared_ptr<BLETask> &b)
    {
        return a->getPriority() < b->getPriority();
    }
};

namespace std {
    template<>
    struct hash<NimBLEUUID> {
        std::size_t operator()(const NimBLEUUID& uuid) const {
            return hash<std::string>()(uuid.toString());
        }
    };
}

class BLEManager
{
public:
    BLEManager();

    void queueTask(std::shared_ptr<BLETask> task);

    void queueFindDeviceTask(int priority, uint32_t timeout,
                             std::function<void(const TaskResult &)> callback,
                             const NimBLEUUID &serviceUuid);

    void queuePollCharacteristicTask(int priority, uint32_t timeout,
                                     std::function<void(const TaskResult &)> callback,
                                     const NimBLEUUID &serviceUuid,
                                     const NimBLEUUID &writeCharUuid,
                                     const NimBLEUUID &readCharUuid,
                                     const uint8_t *data,
                                     size_t dataLength);
    void queueTDTPollCharacteristicTask(int priority, uint32_t timeout,
                                     std::function<void(const TaskResult &)> callback,
                                     const NimBLEAddress &deviceAddress, bool bSticky);                                     
    void close();
    void process();
    void init(bool bIsReset);
    bool isBusy() const;
    std::string uuidToShortKey(const NimBLEUUID& uuid);
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