#include "idf_lora_radio_pump.h"
#include "platform/shared/radio/restart_receive.h"
#include <cassert>
#include <cstdio>

using namespace platform::esp::radio;

struct ContinuousRadio
{
    bool receiving = true;
    unsigned standby_calls = 0, receive_calls = 0;
    int standby_error = 0, receive_error = 0;
    int standby()
    {
        ++standby_calls;
        if (standby_error) return standby_error;
        receiving = false;
        return 0;
    }
    int startReceive()
    {
        ++receive_calls;
        if (receiving) return -706;
        if (receive_error) return receive_error;
        receiving = true;
        return 0;
    }
};

int main()
{
    for (unsigned failure = 0; failure < 3; ++failure)
    {
        ContinuousRadio radio;
        if (failure == 1) radio.standby_error = -1;
        if (failure == 2) radio.receive_error = -2;
        const bool result = platform::shared::radio::restartReceive(
            radio, [](int code, const char*)
            { return code == 0; },
            "test receive");
        assert(result == (failure == 0));
        assert(radio.standby_calls == 1 && radio.receive_calls == (failure == 1 ? 0 : 1));
    }
    for (const uint32_t progress : {0x0004u, 0x0008u, 0x0010u, 0x0018u})
    {
        LoraBoard board;
        IdfLoraRadioPump pump(board);
        IdfLoraRadioFrame packet;
        assert(pump.ensureReceiveStarted());
        board.irq = progress;
        assert(pump.poll(packet) == IdfLoraPollResult::None);
        assert(board.starts == 1 && board.clears == 0 && board.irq == progress);
        board.irq |= 0x0012;
        assert(pump.poll(packet) == IdfLoraPollResult::Frame);
        assert(packet.len == 8 && packet.data[0] == 0x42 && packet.rssi == -55);
        assert(board.reads == 1 && board.clears == 1 && board.starts == 2);
    }
    for (const uint32_t error : {0x0020u, 0x0040u, 0x0200u})
        for (const uint32_t done : {0u, 0x0002u})
        {
            LoraBoard board;
            IdfLoraRadioPump pump(board);
            IdfLoraRadioFrame packet;
            assert(pump.ensureReceiveStarted());
            board.irq = error | done;
            const auto result = pump.poll(packet);
            assert(result == (done ? IdfLoraPollResult::ReadFailed : IdfLoraPollResult::ReceiveRestarted));
            assert(!packet.data && !packet.len && board.reads == 0 && board.starts == 2);
        }
    {
        LoraBoard board;
        IdfLoraRadioPump pump(board);
        IdfLoraRadioFrame packet;
        assert(pump.ensureReceiveStarted());
        board.irq = 0x0012;
        board.start_result = -1;
        assert(pump.poll(packet) == IdfLoraPollResult::Frame);
        assert(packet.len == 8 && packet.data[0] == 0x42 && board.reads == 1);
        assert(pump.poll(packet) == IdfLoraPollResult::ReceiveStartFailed);
        assert(!packet.data && !packet.len && board.reads == 1);
    }
    {
        LoraBoard board;
        IdfLoraRadioPump pump(board);
        IdfLoraRadioFrame packet;
        assert(pump.ensureReceiveStarted());
        board.irq = 0x0013; // Receive pump must leave the TX waiter's IRQ intact.
        assert(pump.poll(packet) == IdfLoraPollResult::None && board.clears == 0);
        board.irq &= ~0x0001u;
        assert(pump.poll(packet) == IdfLoraPollResult::Frame);
    }
    std::puts("Production IDF packet receive: progress preserved, IRQ validated before clear, damaged frames rejected, valid payload retained after failed rearm; shared Linux/ESP standby ordering passed");
}
