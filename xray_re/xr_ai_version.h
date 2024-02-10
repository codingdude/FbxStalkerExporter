#ifndef __GNUC__
#pragma once
#endif
#ifndef __XR_AI_VERSION_H__
#define __XR_AI_VERSION_H__

#include "xr_types.h"

namespace xray_re {

// builds 1098, 1114, 1154
const uint32_t AI_VERSION_2 = 2;

// 2215, 2945, 2947+
const uint32_t AI_VERSION_8 = 8;

// 2215 actually differs from 2945+ despite the same on-disk version number.
const uint32_t AI_VERSION_2215 = AI_VERSION_8|0x80000000;

// 3120 (Clear Sky)
const uint32_t AI_VERSION_9 = 9;

// 3456+ (Clear Sky)
const uint32_t AI_VERSION_10 = 10;

} // end of namespace xray_re

#endif
