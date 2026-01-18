#pragma once

#include <stdint.h>

namespace gps {

// Bosch sensor IDs (from SensorLib BoschSensorID.hpp)
constexpr uint8_t kMotionDetect = 77;               // BHY2_SENSOR_ID_MOTION_DET
constexpr uint8_t kSignificantMotion = 55;          // BHY2_SENSOR_ID_SIG
constexpr uint8_t kAnyMotionLpWakeUp = 143;         // BHY2_SENSOR_ID_ANY_MOTION_LP_WU

}  // namespace gps
