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

FindDeviceTask::FindDeviceTask(int priority, uint32_t timeout,
                               std::function<void(const TaskResult &)> callback,
                               const NimBLEUUID &serviceUuid, const NimBLEAddress &deviceAddress)
    : BLETask(TaskType::FIND_DEVICE, priority, timeout, serviceUuid, deviceAddress, callback, false),
      serviceUuid(serviceUuid), scanning(false), deviceFound(false), foundDevice(nullptr) {}

FindDeviceTask::~FindDeviceTask()
{
    if (foundDevice != nullptr)
    {
        delete foundDevice;
        foundDevice = nullptr;
    }
}

void FindDeviceTask::onDiscovered(const NimBLEAdvertisedDevice *advertisedDevice)
{
    if (advertisedDevice && advertisedDevice->isAdvertisingService(serviceUuid))
    {
        Log.debug("FindDeviceTask: Found device with service %s, device: %s, address: %s, add_type: %i", serviceUuid.toString().c_str(), advertisedDevice->getName().c_str(), advertisedDevice->getAddress().toString().c_str(), advertisedDevice->getAddress().getType());
        // Make a copy of the device
        foundDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
        deviceFound = true;
        stop();
    }
    else if (advertisedDevice)
    {
        // Log.debug("FindDeviceTask: Discovered unrelated device: %s, address: %s", advertisedDevice->getName().c_str(), advertisedDevice->getAddress().toString().c_str());
    }
}

void FindDeviceTask::onScanEnd(const NimBLEScanResults &scanResults, int reason)
{
    Log.debug("FindDeviceTask scan ended for service %s, reason %i", serviceUuid.toString().c_str(), reason);
    stop();
}

void FindDeviceTask::execute()
{
    deviceFound = false;

    NimBLEScan *pScan = NimBLEDevice::getScan();

    if (!deviceAddress.isNull())
    {
        Log.debug("FindDeviceTask: Starting scan, Device known already, adding it to whitelist. %s", deviceAddress.toString().c_str());
        if (NimBLEDevice::getWhiteListCount() > 0)
        {
            NimBLEDevice::whiteListRemove(NimBLEDevice::getWhiteListAddress(0));
        }
        NimBLEDevice::whiteListAdd(deviceAddress);
        pScan->setFilterPolicy(BLE_HCI_SCAN_FILT_USE_WL);
    }
    else
    {
        Log.debug("FindDeviceTask: Starting scan, Device unknown, whitelist disabled");
        pScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    }

    pScan->setScanCallbacks(this);
    pScan->setActiveScan(false);
    pScan->setInterval(200);
    pScan->setWindow(200);
    pScan->setMaxResults(0); // callbacks only
    pScan->start(0, false);

    scanning = true;
}

void FindDeviceTask::stop()
{
    if (scanning)
    {
        scanning = false;
        NimBLEDevice::getScan()->stop();
    }
}

bool FindDeviceTask::process()
{
    if (deviceFound && foundDevice != nullptr)
    {
        TaskResult result;
        result.status = TaskStatus::SUCCESS;
        result.deviceName = foundDevice->getName();
        result.deviceAddress = foundDevice->getAddress();
        deviceAddress = foundDevice->getAddress();

        // Get service data if available
        if (foundDevice->haveServiceData())
        {
            std::string serviceData = foundDevice->getServiceData(serviceUuid);
            if (!serviceData.empty())
            {
                result.dataLength = serviceData.length();
                // Use shared_ptr for safe memory management
                result.data = std::shared_ptr<uint8_t[]>(new uint8_t[result.dataLength]);
                memcpy(result.data.get(), serviceData.data(), result.dataLength);
            }
        }

        complete(result);
        return true;
    }
    else if (!scanning && startTime > 0)
    {
        TaskResult result;
        result.status = TaskStatus::ERROR;
        result.errorMessage = "Scan was interrupted";
        complete(result);
        return true;
    }

    return false;
}

// PollCharacteristicTask implementation
PollCharacteristicTask::PollCharacteristicTask(int priority, uint32_t timeout,
                                               std::function<void(const TaskResult &)> callback,
                                               const NimBLEAddress &deviceAddress,
                                               const NimBLEUUID &serviceUuid,
                                               const NimBLEUUID &writeCharUuid,
                                               const NimBLEUUID &readCharUuid,
                                               const uint8_t *data,
                                               size_t dataLength)
    : BLETask(TaskType::POLL_CHARACTERISTIC, priority, timeout, serviceUuid, deviceAddress, callback, false),
      writeCharUuid(writeCharUuid), readCharUuid(readCharUuid),
      dataLength(dataLength), connected(false), written(false)
{

    // Copy the data using shared_ptr
    if (dataLength > 0 && data != nullptr)
    {
        this->data = std::shared_ptr<uint8_t[]>(new uint8_t[dataLength]);
        memcpy(this->data.get(), data, dataLength);
    }
}

PollCharacteristicTask::~PollCharacteristicTask()
{
    // No need to manually free data with shared_ptr
}

void PollCharacteristicTask::execute()
{
    // Reset state
    connected = false;
    written = false;
    connecting = false;
    pBLEClient = nullptr;
    pReadChar = nullptr;
    pWriteChar = nullptr;
    findTask = nullptr;
    pendingResult = std::nullopt;

    // If we don't have a device address, we need to find it first
    if (deviceAddress.isNull())
    {
        Log.info("PollCharacteristicTask: No device address, finding device with service %s first",
                 serviceUuid.toString().c_str());

        // Create a FindDeviceTask to locate the device
        findTask = std::make_shared<FindDeviceTask>(
            priority, 10000, // Use a shorter timeout for the find task
            [this](const TaskResult &result)
            {
                if (result.status == TaskStatus::SUCCESS)
                {
                    deviceAddress = result.deviceAddress;
                    Log.debug("PollCharacteristicTask: Found device %s with address %s",
                              result.deviceName.c_str(), deviceAddress.toString().c_str());

                    connectToDeviceAsync();
                }
                else
                {
                    Log.error("PollCharacteristicTask: Failed to find device with service %s: %s",
                              serviceUuid.toString().c_str(), result.errorMessage.c_str());

                    setErrorResult("Failed to find device: " + result.errorMessage);
                }
                findTask = nullptr;
            },
            serviceUuid,
            NimBLEAddress() // Empty address to trigger a scan
        );

        findTask->setStartTime(millis());
        findTask->execute();
    }
    else
    {
        Log.info("PollCharacteristicTask: Using known device address %s", deviceAddress.toString().c_str());
        connectToDeviceAsync();
    }
}

void PollCharacteristicTask::connectToDeviceAsync()
{
    pBLEClient = NimBLEDevice::createClient();

    // Set connection parameters
    // pBLEClient->setConnectionParams(BLE_GAP_INIT_CONN_ITVL_MIN, BLE_GAP_INIT_CONN_ITVL_MAX, 0, 60);
    pBLEClient->setConnectTimeout(10000);
    pBLEClient->setClientCallbacks(this);

    // Start the connection attempt asynchronously
    connecting = true;
    if (!pBLEClient->connect(deviceAddress, true, true))
    {
        Log.error("PollCharacteristicTask: Failed to start connection to device %s", deviceAddress.toString().c_str());
        connecting = false;
        setErrorResult("Failed to start connection to device");
    }
    else
    {
        Log.debug("PollCharacteristicTask: Connection attempt started for device %s", deviceAddress.toString().c_str());
    }
}

void PollCharacteristicTask::writeCommand()
{
    // Get the service
    NimBLERemoteService *pService = pBLEClient->getService(serviceUuid);
    if (pService == nullptr)
    {
        Log.error("PollCharacteristicTask: Service %s not found", serviceUuid.toString().c_str());
        setErrorResult("Service not found");
        return;
    }
    // Get the write characteristic
    pWriteChar = pService->getCharacteristic(writeCharUuid);
    if (pWriteChar == nullptr)
    {
        Log.error("PollCharacteristicTask: Write characteristic %s not found", writeCharUuid.toString().c_str());
        setErrorResult("Write characteristic not found");
        return;
    }
    // Get the read characteristic
    pReadChar = pService->getCharacteristic(readCharUuid);
    if (pReadChar == nullptr)
    {
        Log.error("PollCharacteristicTask: Read characteristic %s not found", readCharUuid.toString().c_str());
        setErrorResult("Read characteristic not found");
        return;
    }
    // Register for notifications on the read characteristic using lambda
    if (pReadChar->canNotify())
    {
        if (!pReadChar->subscribe(true, [this](NimBLERemoteCharacteristic *pChar, uint8_t *pData, size_t length, bool isNotify)
                                  { this->onNotify(pChar, pData, length, isNotify); }))
        {
            Log.error("PollCharacteristicTask: Failed to subscribe to notifications");
            setErrorResult("Failed to subscribe to notifications");
            return;
        }
    }
    else
    {
        Log.error("PollCharacteristicTask: Read characteristic does not support notifications");
        setErrorResult("Read characteristic does not support notifications");
        return;
    }
    // Write the data to the characteristic
    if (dataLength > 0 && data)
    {
        if (!pWriteChar->writeValue(data.get(), dataLength, false))
        {
            Log.error("PollCharacteristicTask: Failed to write data to characteristic");
            setErrorResult("Failed to write data to characteristic");
            return;
        }

        Log.debug("PollCharacteristicTask: Data written to characteristic");
        written = true;
    }
    else
    {
        Log.error("PollCharacteristicTask: No data to write");
        setErrorResult("No data to write");
        return;
    }
}

void PollCharacteristicTask::onConnect(NimBLEClient *pClient)
{
    Log.info("PollCharacteristicTask: Connected to device %s", deviceAddress.toString().c_str());
    connected = true;
    connecting = false;
}

void PollCharacteristicTask::onDisconnect(NimBLEClient *pClient, int reason)
{
    Log.info("PollCharacteristicTask: Disconnected from device, reason: %d", reason);

    // If we were connecting or connected but hadn't completed the operation
    if ((connecting || (connected && !written)) && !pendingResult.has_value())
    {
        setErrorResult("Disconnected before operation completed");
    }
    connected = false;
}

bool PollCharacteristicTask::process()
{
    if (pendingResult.has_value())
    {
        if (pBLEClient)
        {
            pBLEClient->setClientCallbacks(nullptr);
            NimBLEDevice::deleteClient(pBLEClient); // will call disconnect/cancelConnect
            pBLEClient = nullptr;
        }
        connecting = false;
        connected = false;

        complete(pendingResult.value());
        pendingResult = std::nullopt;
        return true;
    }

    if (connected && !written)
    {
        writeCommand();
    }

    // If we're running a find device task, process it
    if (findTask)
    {
        // Process the find task
        if (findTask->process())
        {
            // FindDeviceTask has completed, its callback will handle the next steps
            return false;
        }

        // Check for timeout on the find task
        if (millis() - findTask->getStartTime() > findTask->getTimeout())
        {
            findTask->stop();

            TaskResult result;
            result.status = TaskStatus::TIMEOUT;
            result.errorMessage = "PollCharacteristicTask: Find device task timed out";
            complete(result);

            findTask = nullptr;
            return true;
        }
    }

    // If we're connected and have written data, we're waiting for a notification
    // The onNotify callback will set pendingResult which will be handled on the next process() call
    return false;
}

void PollCharacteristicTask::onConnectFail(NimBLEClient *pClient, int reason)
{
    Log.warn("PollCharacteristicTask: Connection to device %s failed, reason: %d",
             deviceAddress.toString().c_str(), reason);

    connecting = false;
    std::string errorMsg = "Connection failed, reason: " + std::to_string(reason);
    setErrorResult(errorMsg);
}

void PollCharacteristicTask::stop()
{
    if (findTask)
    {
        findTask->stop();
        findTask = nullptr;
    }
    if (pBLEClient)
    {
        pBLEClient->setClientCallbacks(nullptr);
        NimBLEDevice::deleteClient(pBLEClient); // will call disconnect/cancelConnect
        pBLEClient = nullptr;
    }
    connecting = false;
    connected = false;
}

void PollCharacteristicTask::onNotify(NimBLERemoteCharacteristic *pRemoteCharacteristic,
                                      uint8_t *pData, size_t length, bool isNotify)
{
    if (pRemoteCharacteristic->getUUID().equals(readCharUuid))
    {
        Log.info("PollCharacteristicTask: Received notification from characteristic %s",
                 readCharUuid.toString().c_str());

        // Create the result
        TaskResult result;
        result.status = TaskStatus::SUCCESS;
        result.deviceAddress = deviceAddress;
        result.dataLength = length;

        // Copy the received data
        if (length > 0)
        {
            result.data = std::shared_ptr<uint8_t[]>(new uint8_t[length]);
            memcpy(result.data.get(), pData, length);
        }

        // Store the result for process() to handle
        pendingResult = result;
    }
}

void PollCharacteristicTask::setErrorResult(const std::string &errorMessage)
{
    TaskResult result;
    result.status = TaskStatus::ERROR;
    result.errorMessage = errorMessage;
    pendingResult = result;
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

void BLEManager::queueFindDeviceTask(int priority, uint32_t timeout,
                                     std::function<void(const TaskResult &)> callback,
                                     const NimBLEUUID &serviceUuid)
{
    auto task = std::make_shared<FindDeviceTask>(priority, timeout, callback, serviceUuid, getKnownDevice(serviceUuid));
    queueTask(task);
}

void BLEManager::queuePollCharacteristicTask(int priority, uint32_t timeout,
                                             std::function<void(const TaskResult &)> callback,
                                             const NimBLEUUID &serviceUuid,
                                             const NimBLEUUID &writeCharUuid,
                                             const NimBLEUUID &readCharUuid,
                                             const uint8_t *data,
                                             size_t dataLength)
{
    auto task = std::make_shared<PollCharacteristicTask>(
        priority, timeout, callback, getKnownDevice(serviceUuid), serviceUuid,
        writeCharUuid, readCharUuid, data, dataLength);
    queueTask(task);
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