#include "platform/linux/phone_gatt_session.h"
#include <cassert>
#include <stdexcept>

using Session = platform::linux_runtime::PhoneGattSession;
using Result = Session::Result;

int main()
{
    Session session(510, 504);
    Session::Bytes output;
    int pulls = 0;
    auto pull = [&](Session::Bytes& bytes)
    {
        ++pulls;
        bytes.resize(510);
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i);
        return true;
    };
    assert(session.read("a", 23, 1, pull, output) == Result::InvalidOffset);
    assert(session.read("a", 23, 0, pull, output) == Result::Ok);
    assert(output.size() == 22 && pulls == 1);
    assert(session.read("b", 23, 0, pull, output) == Result::Busy);
    assert(pulls == 1 && output.empty());
    assert(session.read("a", 23, 22, pull, output) == Result::Ok);
    assert(output.front() == 22 && pulls == 1);
    assert(session.read("a", 23, 510, pull, output) == Result::Ok && output.empty());
    assert(session.read("a", 23, 511, pull, output) == Result::InvalidOffset);
    int writes = 0;
    auto push = [&](const Session::Bytes&)
    { ++writes; return true; };
    assert(session.write("a", 0, false, Session::Bytes(504), push) == Result::Ok);
    assert(session.write("a", 0, false, Session::Bytes(505), push) == Result::InvalidLength);
    assert(session.write("a", 1, false, {1}, push) == Result::InvalidOffset);
    assert(session.write("a", 0, true, {1}, push) == Result::InvalidOffset);
    assert(session.write("b", 0, false, {1}, push) == Result::Busy && writes == 1);
    assert(session.canNotify(20, 23) && !session.canNotify(21, 23));
    assert(!session.disconnect("b") && session.disconnect("a"));
    assert(!session.canNotify(1, 23));
    assert(session.read("a", 23, 22, pull, output) == Result::InvalidOffset);
    assert(session.write("b", 0, false, {1}, [](const auto&)
                         { return false; }) == Result::Failed);
    assert(session.write("c", 0, false, {1}, push) == Result::Ok);
    assert(session.disconnect("c"));
    assert(session.read(
               "a", 23, 0, [](auto&) -> bool
               { throw std::runtime_error("test"); },
               output) == Result::Failed);
    assert(session.read(
               "b", 23, 0, [](auto& bytes)
               { bytes.resize(511); return true; },
               output) == Result::Failed);
    assert(session.read(
               "c", 23, 0, [](auto&)
               { return true; },
               output) == Result::Ok &&
           output.empty());
    Session meshcore(172, 172);
    assert(meshcore.write("a", 0, false, Session::Bytes(172), push) == Result::Ok);
    assert(meshcore.write("a", 0, false, Session::Bytes(173), push) == Result::InvalidLength);
    assert(meshcore.canNotify(172, 185));
    assert(!meshcore.canNotify(172, 23));
}
