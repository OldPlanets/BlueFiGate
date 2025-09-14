#include "BLEManager.h"
#include "Log.h"
#include <mbedtls/md5.h>
#include "TDTPollCharacteristicTask.h"

const char *BLE_NVS_NAMESPACE = "bleprefs";

BLETask::BLETask(TaskType type, int priority, uint32_t timeout, const NimBLEUUID &serviceUuid, const NimBLEAddress &deviceAddress,
                 std::function<void(const TaskResult &)> callback, bool bSticky)
    : type(type), priority(priority), timeout(timeout), startTime(0), deviceAddress(deviceAddress), serviceUuid(serviceUuid), callback(callback), bSticky(bSticky)
{
}

void BLETask::complete(const TaskResult &result)
{
    if (callback)
    {
        callback(result);
    }
}

const char *BLETask::getResultLabel(const TaskStatus &status)
{
    switch (status)
    {
    case TaskStatus::SUCCESS:
        return "SUCCESS";
    case TaskStatus::ERROR:
        return "ERROR";
    case TaskStatus::TIMEOUT:
        return "TIMEOUT";
    case TaskStatus::CANCELLED:
        return "CANCELLED";
    default:
        return "UNKNOWN";
    }
}

BLEManager::BLEManager() : busy(false), initialized(false) {}

void BLEManager::init(bool bIsReset)
{
    if (!initialized)
    {
        Log.debug("[BLE] Init");
        NimBLEDevice::init("Vancontrol");
        if (!m_prefs.begin(BLE_NVS_NAMESPACE))
        {
            Log.error("[BLE] Opening preferences failed");
        }
        if (bIsReset)
        {
            m_prefs.clear();
            Log.info("[BLE] Deleting known devices list");
        }
        initialized = true;
    }
}

void BLEManager::queueTask(std::shared_ptr<BLETask> task)
{
    taskQueue.push(task);
}

void BLEManager::queueTDTPollCharacteristicTask(int priority, uint32_t timeout,
                                                std::function<void(const TaskResult &)> callback,
                                                const NimBLEAddress &deviceAddress, bool bSticky)
{
    auto task = std::make_shared<TDTPollCharacteristicTask>(priority, timeout, callback, deviceAddress, bSticky);
    queueTask(task);
}

bool BLEManager::isBusy() const
{
    return busy || !taskQueue.empty() || currentTask;
}

void BLEManager::process()
{
    if (!initialized)
    {
        return;
    }

    if (!busy && !taskQueue.empty() && !currentTask)
    {
        // Start a new task
        currentTask = taskQueue.top();
        taskQueue.pop();
        currentTask->setStartTime(millis());
        currentTask->execute();
        busy = true;
    }

    if (currentTask)
    {
        if (currentTask->process())
        {
            if (currentTask->isSticky())
            {
                currentTask->restart();
            }
            else
            {
                // Task completed
                if (!currentTask->getServiceUuid().equals(NimBLEUUID()) && m_knownDeviceMap[currentTask->getServiceUuid()] != currentTask->getDeviceAddress())
                {
                    m_knownDeviceMap[currentTask->getServiceUuid()] = currentTask->getDeviceAddress();
                    m_prefs.putULong64(uuidToShortKey(currentTask->getServiceUuid()).c_str(), (uint64_t)currentTask->getDeviceAddress());
                    Log.debug("BLE Manager. Put device to known list: %s for service %s", currentTask->getDeviceAddress().toString().c_str(), currentTask->getServiceUuid().toString().c_str());
                }

                currentTask = NULL;
                busy = false;
            }
        }
        else if (currentTask->getTimeout() > 0 && currentTask->getStartTime() > 0 && millis() - currentTask->getStartTime() > currentTask->getTimeout()) // Check for timeout
        {
            currentTask->stop();
            TaskResult result;
            result.status = TaskStatus::TIMEOUT;
            result.errorMessage = "Task timed out";
            currentTask->complete(result);
            if (currentTask->isSticky())
            {
                delay(200);
                currentTask->restart();
            }
            else
            {
                currentTask = nullptr;
                busy = false;
            }
        }
    }
}

std::string BLEManager::uuidToShortKey(const NimBLEUUID &uuid)
{
    std::string uuidStr = uuid.toString();
    // For short UUIDs (16-bit or 32-bit), they're already short enough
    if (uuidStr.length() <= 12)
    {
        return uuidStr;
    }

    // For longer UUIDs, use MD5 hash and take first 12 chars of hex representation
    unsigned char hash[16];
    mbedtls_md5((const unsigned char *)uuidStr.c_str(), uuidStr.length(), hash);
    char hexOutput[13];
    for (int i = 0; i < 6; i++)
    {
        sprintf(&hexOutput[i * 2], "%02x", hash[i]);
    }

    return std::string(hexOutput, 12);
}

NimBLEAddress BLEManager::getKnownDevice(const NimBLEUUID &serviceUuid)
{
    NimBLEAddress address = m_knownDeviceMap[serviceUuid];
    if (address.isNull())
    {
        uint64_t stored = m_prefs.getULong64(uuidToShortKey(serviceUuid).c_str(), 0ULL);
        address = stored ? NimBLEAddress(stored, BLE_ADDR_PUBLIC) : NimBLEAddress();
        m_knownDeviceMap[serviceUuid] = address;
    }
    return address;
}

void BLEManager::close()
{
    m_prefs.end();
    NimBLEDevice::deinit(true);
}