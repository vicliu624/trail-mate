#pragma once
namespace platform::esp::idf_common
{
class Lr2021Radio
{
  public:
    static Lr2021Radio& instance()
    {
        static Lr2021Radio radio;
        return radio;
    }
    bool online = true;
    bool isOnline() const { return online; }
};
} // namespace platform::esp::idf_common
