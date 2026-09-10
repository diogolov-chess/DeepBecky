#ifndef DEEPBECKY_UCIOUTPUT_H
#define DEEPBECKY_UCIOUTPUT_H
#include <iostream>
#include <mutex>

namespace UCI {
inline std::mutex& outputMutex() {
    static std::mutex mutex;
    return mutex;
}
// Hold only during publication, never while waiting for a worker.
using OutputLock = std::lock_guard<std::mutex>;
}
#endif
