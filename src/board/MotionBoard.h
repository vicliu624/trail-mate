#pragma once

class SensorBHI260AP;

// ˶ӿڣ¶ BHI260 طʡ
class MotionBoard
{
  public:
    virtual ~MotionBoard() = default;

    virtual SensorBHI260AP& getMotionSensor() = 0;
    virtual bool isSensorReady() const = 0;
};

