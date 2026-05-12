#include "fake_phone_runtime_context.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_session.h"

#include <cassert>
#include <cstdint>

namespace
{

constexpr uint8_t kMeshCoreResponseError = 1;
constexpr uint8_t kMeshCoreErrorIllegalArg = 6;

class FakeMeshtasticTransport final : public phone::meshtastic::MeshtasticPhoneTransport
{
  public:
    bool isBleConnected() const override { return connected; }
    void notifyFromNum(uint32_t from_num) override
    {
        last_from_num = from_num;
        notify_count++;
    }

    bool connected = true;
    uint32_t last_from_num = 0;
    int notify_count = 0;
};

} // namespace

int main()
{
    phone::tests::FakePhoneRuntimeContext runtime;
    FakeMeshtasticTransport transport;

    phone::meshtastic::MeshtasticPhoneSession meshtastic_session(
        runtime, transport, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    const uint8_t invalid_meshtastic[] = {0xFF, 0xFF, 0xFF};
    assert(!meshtastic_session.handleToRadio(invalid_meshtastic, sizeof(invalid_meshtastic)));
    meshtastic_session.close();

    phone::meshcore::MeshCorePhoneCore meshcore_core(runtime, "Trail Mate");
    assert(!meshcore_core.handleRxFrame(nullptr, 0));
    const uint8_t unknown_meshcore_cmd[] = {2, 0, 0, 0};
    assert(meshcore_core.handleRxFrame(unknown_meshcore_cmd, sizeof(unknown_meshcore_cmd)));

    uint8_t out[172] = {};
    size_t out_len = 0;
    assert(meshcore_core.popTxFrame(out, &out_len));
    assert(out_len == 2);
    assert(out[0] == kMeshCoreResponseError);
    assert(out[1] == kMeshCoreErrorIllegalArg);
    return 0;
}
