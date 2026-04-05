#ifndef UTILS_LOGS_H
#define UTILS_LOGS_H

#include <cstdint>
#include <cstring>
#include <array>

#include "libbotanix/utils/time.h"
#include "libbotanix/plcio/vars.h"

using std::min;
using std::max;

namespace Botanix { namespace Utils {
// TODO
struct LogBucket_t {
    uint16_t    state                   = 0;
    uint16_t    suppressed              = 0;
    uint32_t    count                   = 0;
};




#endif // UTILS_LOGS_H