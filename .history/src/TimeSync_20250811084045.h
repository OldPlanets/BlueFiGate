#pragma once

class TimeSync {
private:
    unsigned long syncStartTime = 0;
    unsigned long lastSyncTime = 0;
    bool syncInProgress = false;
    bool timeIsSynced = false;
    
    static const unsigned long SYNC_TIMEOUT = 10000;
    static const unsigned long SYNC_INTERVAL = 86400000; // 24 hours
    static const unsigned long RETRY_INTERVAL = 30000;
    static const unsigned long MIN_VALID_TIMESTAMP = 1609459200; // Jan 1, 2021

    void startSync();
    void onSyncSuccess();
    void onSyncFailed();

public:
    void begin();
    void loop();
    bool isSynced() const;
    void forceSync();
    static bool checkIfSynced();
};