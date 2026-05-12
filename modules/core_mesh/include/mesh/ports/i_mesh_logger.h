#pragma once

namespace mesh
{

class IMeshLogger
{
  public:
    virtual ~IMeshLogger() = default;

    virtual void info(const char* message) = 0;
    virtual void warn(const char* message) = 0;
    virtual void error(const char* message) = 0;
};

} // namespace mesh
