"""Exercise production radio-binding methods with a contract-only fake board.

The ESP adapter translation unit depends on Arduino and FreeRTOS. This fixture
compiles its two actual method bodies with the real LoraBoard contract and radio
parameter derivation, replacing the hardware and task scheduler at their ports.
No board identity macro is required by the default test.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'platform/esp/arduino_common/src/chat/infra/meshtastic/mt_adapter.cpp')
parser.add_argument('--define', action='append', default=[])
parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
methods = []
for name in ('configureRadio', 'startRadioReceive'):
    start = source.index(f'void MtAdapter::{name}()')
    end = source.index('{', start)
    depth = 1
    end += 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[start:end])

fixture = r'''
#include "board/LoraBoard.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#define LORA_LOG(...) ((void)0)
constexpr int RADIOLIB_ERR_NONE = 0;
uint32_t millis() { return 1234; }
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
namespace app {
struct AppTasks {
    inline static unsigned restart_requests = 0;
    inline static bool receive_active = false;
    static void requestRadioReceiveRestart() { ++restart_requests; }
    static void setRadioReceiveActive(bool active) { receive_active = active; }
};
}
class Board final : public LoraBoard {
public:
    bool online = true;
    int config_result = 0, receive_result = 0;
    unsigned configurations = 0, receive_starts = 0;
    float frequency = 869.525f, bandwidth = 0;
    uint8_t spreading = 0, coding = 0, sync = 0, crc = 0;
    uint16_t preamble = 0;
    bool isRadioOnline() const override { return online; }
    int transmitRadio(const uint8_t*, size_t) override { throw std::runtime_error("unexpected TX"); }
    int startRadioReceive() override { ++receive_starts; return receive_result; }
    uint32_t getRadioIrqFlags() override { return 0; }
    int getRadioPacketLength(bool) override { return 0; }
    int readRadioData(uint8_t*, size_t) override { return -1; }
    void clearRadioIrqFlags(uint32_t) override {}
    float getRadioRSSI() override { return 0; }
    float getRadioSNR() override { return 0; }
    int configureLoraRadio(float f, float b, uint8_t s, uint8_t c, int8_t,
                           uint16_t p, uint8_t w, uint8_t k) override {
        ++configurations;
        if (!config_result) { frequency=f; bandwidth=b; spreading=s; coding=c; preamble=p; sync=w; crc=k; }
        return config_result;
    }
};
class MtAdapter {
public:
    explicit MtAdapter(LoraBoard& board) : board_(board) {}
    LoraBoard& board_;
    chat::MeshConfig config_;
    bool ready_ = false;
    uint32_t radio_freq_hz_ = 0, radio_bw_hz_ = 0, last_nodeinfo_ms_ = 0;
    uint8_t radio_sf_ = 0, radio_cr_ = 0;
    void configureRadio();
    void startRadioReceive();
};
'''
fixture += '\n'.join(methods)
fixture += r'''
int main() {
    Board board;
    MtAdapter adapter(board);
    adapter.config_.region = meshtastic_Config_LoRaConfig_RegionCode_CN;
    adapter.config_.use_preset = true;
    adapter.config_.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    adapter.configureRadio();
    require(board.configurations == 1, "online board never received its configuration");
    require(std::fabs(board.frequency - 478.875f) < .001f, "hardware retained the boot frequency");
    require(board.bandwidth == 250 && board.spreading == 11 && board.coding == 5,
            "wrong LongFast air parameters");
    require(board.sync == 0x2B && board.preamble == 16 && board.crc == 2,
            "wrong receive framing");
    require(adapter.ready_ && std::llabs(static_cast<long long>(adapter.radio_freq_hz_) - 478875000) <= 32,
            "successful applied state unavailable");
    require(board.receive_starts == 1 && app::AppTasks::receive_active,
            "configured board did not start RX");
    const auto applied_frequency = adapter.radio_freq_hz_;
    board.config_result = -5;
    adapter.config_.override_frequency_mhz = 500.125f;
    adapter.configureRadio();
    require(!adapter.ready_, "failed hardware configuration reported ready");
    require(adapter.radio_freq_hz_ == applied_frequency, "failed request published as applied frequency");
    require(board.receive_starts == 1, "RX started after rejected configuration");
    board.online = false;
    adapter.configureRadio();
    require(board.configurations == 2 && !adapter.ready_, "offline board was configured");
    const auto restarts = app::AppTasks::restart_requests;
    adapter.startRadioReceive();
    require(app::AppTasks::restart_requests == restarts + 1 && board.receive_starts == 1,
            "offline receive did not defer restart");
    board.online = true;
    board.receive_result = -7;
    app::AppTasks::receive_active = false;
    adapter.startRadioReceive();
    require(board.receive_starts == 2 && !app::AppTasks::receive_active,
            "rejected RX was marked active");
    std::puts("PASS: board-polymorphic CN/LongFast apply, RX start, rejection and deferred restart");
}
'''
build = ROOT / '.codex-build/meshtastic-radio-binding-native'
build.mkdir(parents=True, exist_ok=True)
(build / 'NOTES.txt').write_text('Purpose: production MtAdapter configuration/RX binding regression.\nInput: current method bodies, real LoraBoard contract and shared radio derivation.\nCleanup: reusable native test cache; safe to replace after test completion.\n', encoding='utf-8')
unit = build / 'fixture.cpp'
unit.write_text(fixture, encoding='utf-8')
binary = build / ('fixture.exe' if os.name == 'nt' else 'fixture')
compiler = shlex.split(args.compiler)
if not compiler or not shutil.which(compiler[0]):
    raise SystemExit('C++ compiler unavailable; set CXX or --compiler')
includes = ['platform/shared/include', 'modules/core_chat/include', 'modules/core_chat/generated', 'modules/core_chat/third_party/nanopb']
command = compiler + ['-std=c++17', '-O0', '-g', str(unit)]
command += [str(ROOT / 'modules/core_chat/src/infra/meshtastic' / name) for name in ('mt_radio_config.cpp', 'mt_region.cpp')]
command += ['-I' + str(ROOT / path) for path in includes]
command += ['-D' + definition for definition in args.define]
command += ['-o', str(binary)]
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)
