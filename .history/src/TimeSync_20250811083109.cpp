#include <WiFi.h>
#include <time.h>
#include "TimeSync.h"
#include "Log.h"

void TimeSync::startSync()
{
    Log.debug("Starting NTP sync...");
    configTime(0, 0, "pool.ntp.org", "de.pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    syncStartTime = millis();
    syncInProgress = true;
}

bool TimeSync::checkIfSynced()
{
    time_t now = time(nullptr);
    return (now > MIN_VALID_TIMESTAMP);
}

void TimeSync::onSyncSuccess()
{
    Log.debug("Time sync successful!");
    timeIsSynced = true;
    syncInProgress = false;
    lastSyncTime = millis();

    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        Log.debug("Current time: %s", asctime(&timeinfo));
    }
}

void TimeSync::onSyncFailed()
{
    Log.debug("Time sync failed, will retry in %d seconds", RETRY_INTERVAL / 1000);
    syncInProgress = false;
}

void TimeSync::begin()
{
    if (WiFi.status() == WL_CONNECTED && !timeIsSynced)
    {
        startSync();
    }
}

void TimeSync::loop()
{
    if (!WiFi.isConnected())
    {
        return;
    }

    if (syncInProgress)
    {
        if (checkIfSynced())
        {
            onSyncSuccess();
        }
        else if (millis() - syncStartTime > SYNC_TIMEOUT)
        {
            onSyncFailed();
        }
    }
    else
    {
        unsigned long timeSinceLastSync = millis() - lastSyncTime;

        if (!timeIsSynced)
        {
            if (timeSinceLastSync > RETRY_INTERVAL)
            {
                startSync();
            }
        }
        else if (timeSinceLastSync > SYNC_INTERVAL)
        {
            Log.debug("24h passed, re-syncing time");
            timeIsSynced = false;
            startSync();
        }
    }
}

bool TimeSync::isSynced() const
{
    return timeIsSynced;
}

void TimeSync::forceSync()
{
    timeIsSynced = false;
    syncInProgress = false;
    lastSyncTime = 0;
}