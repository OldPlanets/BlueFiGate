// WebRequestManager.cpp
#include "WebRequestManager.h"
#include "Log.h"
#include "wifimanager.h"

WebRequestManager::WebRequestManager(WIFIMANAGER &wifiManager)
    : busy(false), initialized(false), m_wifiManager(wifiManager), bWifiRequested(false), bWifiFailed(false), m_rtc(NULL)
{
}

WebRequestManager::~WebRequestManager()
{
}

void WebRequestManager::init()
{
    if (!initialized)
    {
        Log.debug("[WebRequestManager] Init");
        initialized = true;
    }
}

void WebRequestManager::queueTask(std::shared_ptr<WebTask> task)
{
    taskQueue.push(task);
}

void WebRequestManager::queueFetchDataTask(int priority, uint32_t timeout,
                                           std::function<void(const WebTaskResult &)> callback,
                                           const std::string &url, uint32_t serviceId)
{
    auto task = std::make_shared<FetchDataTask>(priority, timeout, callback, url, serviceId,
                                                [this](const time_t currentTime)
                                                { this->onTimeUpdate(currentTime); });
    queueTask(task);
}

void WebRequestManager::queueQueryRouterTask(int priority, uint32_t timeout,
                                             std::function<void(const WebTaskResult &)> callback,
                                             const String &routerAddress, const String &password)
{
    auto task = std::make_shared<QueryRouterTask>(priority, timeout, callback, routerAddress, password);
    queueTask(task);
}

bool WebRequestManager::isBusy() const
{
    return busy || !taskQueue.empty() || currentTask;
}

void WebRequestManager::process()
{
    if (!initialized)
    {
        return;
    }

    if (!busy && !taskQueue.empty() && !currentTask)
    {
        if (!bWifiRequested)
        {
            bWifiFailed = !m_wifiManager.requestWifi(this, true);
            bWifiRequested = true;
        }
        // Start a new task
        currentTask = taskQueue.top();
        taskQueue.pop();

        if (!bWifiFailed)
        {
            currentTask->setStartTime(millis());
            currentTask->execute();
            busy = true;
        }
        else
        {
            WebTaskResult result;
            result.status = WebTaskStatus::ERROR;
            result.errorMessage = "Failed to connect WiFi";
            currentTask->complete(result);
            currentTask = nullptr;
        }
    }

    if (currentTask)
    {
        if (currentTask->process())
        {
            // Task completed
            currentTask = nullptr;
            busy = false;
            if (taskQueue.empty())
            {
                m_wifiManager.finishedWifi(this);
                bWifiRequested = false;
            }
        }
        else if (millis() - currentTask->getStartTime() > currentTask->getTimeout())
        {
            // Task timed out
            currentTask->stop();
        }
    }
}

void WebRequestManager::setAutoUpdateRtc(SensorPCF8563 *rtc)
{
    m_rtc = rtc;
    bRtcUpdated = false;
}

void WebRequestManager::onTimeUpdate(const time_t currentTime)
{
    if (m_rtc && !bRtcUpdated)
    {
        struct timeval val;
        val.tv_sec = currentTime;
        val.tv_usec = 0;
        settimeofday(&val, NULL);

        bRtcUpdated = true;
        Log.debug("[WebRequestManager] Updated the RTC from WebRequest result, new time: %u", currentTime);
    }
}