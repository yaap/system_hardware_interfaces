/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "WakeLockEntryList.h"

#include <android-base/file.h>
#include <android-base/logging.h>

using android::base::ReadFdToString;

namespace android {
namespace system {
namespace suspend {
namespace V1_0 {

TimestampType getEpochTimeNow() {
    auto timeSinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(timeSinceEpoch).count();
}

WakeLockEntryList::WakeLockEntryList(size_t capacity, unique_fd kernelWakelockStatsFd)
    : mCapacity(capacity), mKernelWakelockStatsFd(std::move(kernelWakelockStatsFd)) {}

/**
 * Evicts LRU from back of list if stats is at capacity.
 */
void WakeLockEntryList::evictIfFull() {
    if (mStats.size() == mCapacity) {
        auto evictIt = mStats.end();
        std::advance(evictIt, -1);
        auto evictKey = std::make_pair(evictIt->name, evictIt->pid);
        mLookupTable.erase(evictKey);
        mStats.erase(evictIt);
        LOG(ERROR) << "WakeLock Stats: Stats capacity met, consider adjusting capacity to "
                      "avoid stats eviction.";
    }
}

/**
 * Inserts entry as MRU.
 */
void WakeLockEntryList::insertEntry(WakeLockInfo entry) {
    auto key = std::make_pair(entry.name, entry.pid);
    mStats.emplace_front(std::move(entry));
    mLookupTable[key] = mStats.begin();
}

/**
 * Removes entry from the stats list.
 */
void WakeLockEntryList::deleteEntry(std::list<WakeLockInfo>::iterator entry) {
    auto key = std::make_pair(entry->name, entry->pid);
    mLookupTable.erase(key);
    mStats.erase(entry);
}

/**
 * Creates and returns a native wakelock entry.
 */
WakeLockInfo WakeLockEntryList::createNativeEntry(const std::string& name, int pid,
                                                  TimestampType epochTimeNow) const {
    WakeLockInfo info;

    info.name = name;
    // It only makes sense to create a new entry on initial activation of the lock.
    info.activeCount = 1;
    info.lastChange = epochTimeNow;
    info.maxTime = 0;
    info.totalTime = 0;
    info.isActive = true;
    info.activeTime = 0;
    info.isKernelWakelock = false;

    info.pid = pid;

    info.eventCount = 0;
    info.expireCount = 0;
    info.preventSuspendTime = 0;
    info.wakeupCount = 0;

    return info;
}
/*
 * Creates and returns a kernel wakelock entry with data read from mKernelWakelockStatsFd
 */
WakeLockInfo WakeLockEntryList::createKernelEntry(const std::string& name) const {
    WakeLockInfo info;

    info.name = name;

    info.activeCount = 0;
    info.lastChange = 0;
    info.maxTime = 0;
    info.totalTime = 0;
    info.isActive = false;
    info.activeTime = 0;
    info.isKernelWakelock = true;

    info.pid = -1;  // N/A

    info.eventCount = 0;
    info.expireCount = 0;
    info.preventSuspendTime = 0;
    info.wakeupCount = 0;

    unique_fd wakelockFd{TEMP_FAILURE_RETRY(
        openat(mKernelWakelockStatsFd, name.c_str(), O_DIRECTORY | O_CLOEXEC | O_RDONLY))};
    if (wakelockFd < 0) {
        PLOG(ERROR) << "Error opening kernel wakelock stats for: " << name;
    }

    std::unique_ptr<DIR, decltype(&closedir)> wakelockDp(fdopendir(dup(wakelockFd.get())),
                                                         &closedir);
    if (wakelockDp) {
        struct dirent* de;
        while ((de = readdir(wakelockDp.get()))) {
            std::string statName(de->d_name);
            if ((statName == ".") || (statName == ".." || statName == "power" ||
                                      statName == "subsystem" || statName == "uevent")) {
                continue;
            }

            unique_fd statFd{
                TEMP_FAILURE_RETRY(openat(wakelockFd, statName.c_str(), O_CLOEXEC | O_RDONLY))};
            if (statFd < 0) {
                PLOG(ERROR) << "Error opening " << statName << " for " << name << " wakelock";
            }

            std::string valStr;
            if (!ReadFdToString(statFd.get(), &valStr)) {
                PLOG(ERROR) << "Error reading " << statName << " for " << name << " wakelock";
                continue;
            }
            int64_t statVal = std::stoll(valStr);

            if (statName == "active_count") {
                info.activeCount = statVal;
            } else if (statName == "active_time_ms") {
                info.activeTime = statVal;
            } else if (statName == "event_count") {
                info.eventCount = statVal;
            } else if (statName == "expire_count") {
                info.expireCount = statVal;
            } else if (statName == "last_change_ms") {
                info.lastChange = statVal;
            } else if (statName == "max_time_ms") {
                info.maxTime = statVal;
            } else if (statName == "prevent_suspend_time_ms") {
                info.preventSuspendTime = statVal;
            } else if (statName == "total_time_ms") {
                info.totalTime = statVal;
            } else if (statName == "wakeup_count") {
                info.wakeupCount = statVal;
            }
        }
    }

    // Derived stats
    info.isActive = info.activeTime > 0;

    return info;
}

void WakeLockEntryList::getKernelWakelockStats(std::vector<WakeLockInfo>* aidl_return) const {
    std::unique_ptr<DIR, decltype(&closedir)> dp(fdopendir(dup(mKernelWakelockStatsFd.get())),
                                                 &closedir);
    if (dp) {
        // rewinddir, else subsequent calls will not get any kernel wakelocks.
        rewinddir(dp.get());

        struct dirent* de;
        while ((de = readdir(dp.get()))) {
            std::string kwlName(de->d_name);
            if ((kwlName == ".") || (kwlName == "..")) {
                continue;
            }
            WakeLockInfo entry = createKernelEntry(kwlName);
            aidl_return->emplace_back(std::move(entry));
        }
    } else {
        PLOG(ERROR)
            << "SystemSuspend: Failed to get directory pointer to kernel wakelock stats dir";
    }
}

void WakeLockEntryList::updateOnAcquire(const std::string& name, int pid,
                                        TimestampType epochTimeNow) {
    std::lock_guard<std::mutex> lock(mStatsLock);

    auto key = std::make_pair(name, pid);
    auto it = mLookupTable.find(key);
    if (it == mLookupTable.end()) {
        evictIfFull();
        WakeLockInfo newEntry = createNativeEntry(name, pid, epochTimeNow);
        insertEntry(newEntry);
    } else {
        auto staleEntry = it->second;
        WakeLockInfo updatedEntry = *staleEntry;

        // Update entry
        updatedEntry.isActive = true;
        updatedEntry.activeTime = 0;
        updatedEntry.activeCount++;
        updatedEntry.lastChange = epochTimeNow;

        deleteEntry(staleEntry);
        insertEntry(std::move(updatedEntry));
    }
}

void WakeLockEntryList::updateOnRelease(const std::string& name, int pid,
                                        TimestampType epochTimeNow) {
    std::lock_guard<std::mutex> lock(mStatsLock);

    auto key = std::make_pair(name, pid);
    auto it = mLookupTable.find(key);
    if (it == mLookupTable.end()) {
        LOG(INFO) << "WakeLock Stats: A stats entry for, \"" << name
                  << "\" was not found. This is most likely due to it being evicted.";
    } else {
        auto staleEntry = it->second;
        WakeLockInfo updatedEntry = *staleEntry;

        // Update entry
        TimestampType timeDelta = epochTimeNow - updatedEntry.lastChange;
        updatedEntry.isActive = false;
        updatedEntry.activeTime += timeDelta;
        updatedEntry.maxTime = std::max(updatedEntry.maxTime, updatedEntry.activeTime);
        updatedEntry.activeTime = 0;  // No longer active
        updatedEntry.totalTime += timeDelta;
        updatedEntry.lastChange = epochTimeNow;

        deleteEntry(staleEntry);
        insertEntry(std::move(updatedEntry));
    }
}
/**
 * Updates the native wakelock stats based on the current time.
 */
void WakeLockEntryList::updateNow() {
    std::lock_guard<std::mutex> lock(mStatsLock);

    TimestampType epochTimeNow = getEpochTimeNow();

    for (std::list<WakeLockInfo>::iterator it = mStats.begin(); it != mStats.end(); ++it) {
        if (it->isActive) {
            TimestampType timeDelta = epochTimeNow - it->lastChange;
            it->activeTime += timeDelta;
            it->maxTime = std::max(it->maxTime, it->activeTime);
            it->totalTime += timeDelta;
            it->lastChange = epochTimeNow;
        }
    }
}

void WakeLockEntryList::getWakeLockStats(std::vector<WakeLockInfo>* aidl_return) const {
    std::lock_guard<std::mutex> lock(mStatsLock);

    for (const WakeLockInfo& entry : mStats) {
        aidl_return->emplace_back(entry);
    }

    getKernelWakelockStats(aidl_return);
}

}  // namespace V1_0
}  // namespace suspend
}  // namespace system
}  // namespace android