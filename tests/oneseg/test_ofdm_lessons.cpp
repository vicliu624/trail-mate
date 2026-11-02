#include "platform/linux/oneseg_ofdm_lessons.h"
#include <cassert>
using namespace platform::linux_runtime;
int main()
{
    using P = OneSegOfdmExperiment;
    for (const auto& control : oneseg_ofdm_controls())
    {
        P p;
        const auto baseline = oneseg_ofdm_lesson(control, p);
        assert(baseline.equation[0] && baseline.explanation.find("PREVIEW") != std::string::npos);
        p.digital_gain = 2;
        p.iq_phase_degrees = 10;
        p.carrier_bandwidth = .1;
        p.damping = 1;
        p.channel_average = 8;
        p.equalizer_floor = .01;
        p.constellation = 64;
        const auto lesson = oneseg_ofdm_lesson(control, p);
        const auto has = [&](const char* text)
        { return lesson.explanation.find(text) != std::string::npos; };
        if (control.real == &P::digital_gain) assert(has("+6.02 dB"));
        if (control.real == &P::iq_phase_degrees) assert(has("21.16 dB"));
        if (control.real == &P::carrier_bandwidth) assert(has("alpha=0.330579") && has("beta=0.0330579"));
        if (control.integer == &P::mode) assert(has("Tu=1008.0 us") && has("Tcp=63.0 us") && has("Ts=1071.0 us"));
        if (control.integer == &P::channel_average) assert(has("-11.76 dB") && has("18 symbols"));
        if (control.real == &P::equalizer_floor) assert(has("amplitude gain 5"));
        if (control.integer == &P::evm_window) assert(has("137.09 ms"));
        if (control.integer == &P::constellation) assert(has("6 bits/carrier") && has("0.3086"));
        if (control.integer == &P::code_rate_index) assert(has("0.61438"));
        if (control.real == &P::filter_cutoff_hz)
        {
            p.filter_cutoff_hz = 80000;
            p.filter_transition_hz = 20000;
            p.filter_attenuation_db = 60;
            const auto filter = oneseg_ofdm_lesson(control, p, 64000000. / 63);
            assert(filter.explanation.find("1015.9 kHz") != std::string::npos);
            assert(filter.explanation.find("Approximate") != std::string::npos);
        }
    }
}
