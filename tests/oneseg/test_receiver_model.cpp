#include "platform/linux/oneseg_receiver_model.h"
#include <cassert>
#include <iostream>

using namespace platform::linux_runtime;
using View = OneSegReceiverView;

static void finite(const OneSegReceiverModel& model)
{
    assert(!model.result.status.empty());
    for (const auto metric : model.result.metrics) assert(std::isfinite(metric));
    for (const auto& plot : model.result.plots)
    {
        assert(!plot.title.empty() && !plot.x_unit.empty() && !plot.y_unit.empty());
        assert(plot.series.size() <= 8);
        for (const auto& series : plot.series)
        {
            assert(series.points.size() <= 401);
            for (const auto& point : series.points) assert(std::isfinite(point.x) && std::isfinite(point.y));
        }
    }
}

int main()
{
    const auto& views = oneseg_receiver_views();
    assert(views.size() == static_cast<size_t>(View::Count));
    unsigned configurations = 0;
    for (size_t index = 1; index < views.size(); ++index)
    {
        const auto& info = views[index];
        assert(static_cast<size_t>(info.view) == index);
        assert(info.parameters.size() <= 6 && info.parameters.size() >= 2);
        assert(*info.equations && *info.variables && *info.relationship && *info.conditions);
        OneSegReceiverModel model(info.view);
        model.render();
        finite(model);
        ++configurations;
        for (size_t parameter = 0; parameter < info.parameters.size(); ++parameter)
        {
            const auto& control = info.parameters[parameter];
            for (const auto value : {control.low, control.high})
            {
                model.reset(info.view);
                assert(model.set(parameter, value));
                model.render();
                finite(model);
                ++configurations;
            }
            assert(!model.set(parameter, control.high + 1));
            assert(!model.set(parameter, std::numeric_limits<double>::quiet_NaN()));
            if (control.choices) assert(!model.set(parameter, control.low + .5));
        }
        std::cout << "PASS " << info.name << '\n';
    }

    OneSegReceiverModel m(View::Fourier);
    m.set(0, 5);
    m.set(1, 5);
    m.set(2, 64);
    m.render();
    assert(std::abs(m.result.metrics[0] - 1) < 1e-12);
    m.set(1, 6);
    m.render();
    assert(m.result.metrics[0] < 1e-12);

    m.reset(View::Orthogonality);
    m.render();
    assert(m.result.metrics[0] < 1e-12);
    m.set(2, .25);
    m.render();
    assert(m.result.metrics[0] > .1);

    m.reset(View::ComplexIq);
    m.set(2, 3);
    m.render();
    assert(m.result.metrics[0] == 0);

    m.reset(View::Adc);
    m.render();
    assert(std::abs(m.result.metrics[0] - 7) < 1e-12);
    assert(std::abs(m.result.metrics[2] - 1. / 15) < 1e-12);
    assert(m.result.metrics[3] == 1 && m.result.metrics[4] == -1);

    m.reset(View::Filter);
    m.render();
    assert(std::abs(m.result.metrics[2] - 1) < 1e-12);
    assert(m.result.metrics[1] < .01);

    m.reset(View::Prefix);
    m.render();
    assert(m.result.metrics[1] == 1 && m.result.metrics[0] < 1e-11);
    m.set(3, -2);
    m.render();
    assert(m.result.metrics[1] == 1 && m.result.metrics[0] < 1e-11);
    m.set(3, 1);
    m.render();
    assert(m.result.metrics[1] == 0 && m.result.metrics[0] > 1e-4);

    m.reset(View::Synchronization);
    m.render();
    assert(std::abs(m.result.metrics[0] - 1) < 1e-12);
    assert(std::abs(m.result.metrics[1] - .15) < 1e-12);
    assert(std::abs(m.result.metrics[4] - .72) < 1e-12);

    m.reset(View::Channel);
    m.set(1, 0);
    m.set(3, 0);
    m.render();
    assert(m.result.metrics[0] < 1e-12 && m.result.metrics[1] < 1e-12);
    assert(m.result.metrics[2] == 17);

    m.reset(View::Phase);
    m.set(2, 0);
    m.set(3, 0);
    m.render();
    const auto& innovations = m.result.plots[2].series[0].points;
    const auto& residuals = m.result.plots[2].series[1].points;
    assert(innovations.size() == residuals.size());
    for (size_t i = 0; i < innovations.size(); ++i)
        assert(std::abs(residuals[i].y - (1 - m.values[0]) * innovations[i].y) < 1e-12);

    m.reset(View::SoftBits);
    m.set(0, 0);
    m.set(1, 0);
    m.render();
    assert(m.result.metrics[0] == 0 && m.result.metrics[1] == 0);
    m.set(0, .5);
    m.set(1, -.5);
    m.render();
    assert(m.result.metrics[0] > 0 && m.result.metrics[1] < 0);

    m.reset(View::Viterbi);
    m.set(1, 0);
    m.render();
    assert(m.result.metrics[0] == 0);
    m.set(1, 1);
    m.render();
    assert(m.result.metrics[0] == 0);
    m.set(1, 3);
    m.set(2, 0);
    m.set(3, .25);
    m.render();
    assert(m.result.metrics[0] == 0);
    m.set(3, 2);
    m.render();
    assert(m.result.metrics[0] > 0);

    m.reset(View::Decisions);
    for (unsigned order = 0; order < 3; ++order)
    {
        m.set(0, order);
        m.render();
        const unsigned levels = 2u << order;
        assert(m.result.plots[0].vertical_guides.size() == levels - 1);
        assert(m.result.plots[0].horizontal_guides.size() == levels - 1);
        for (const auto coordinate : m.result.plots[0].vertical_guides) assert(std::isfinite(coordinate));
    }

    m.reset(View::ReedSolomon);
    m.set(0, 8);
    m.set(1, 0);
    m.render();
    assert(m.result.metrics[1] == 1);
    m.set(0, 9);
    m.render();
    assert(m.result.metrics[1] == 0);
    m.set(0, 0);
    m.set(1, 16);
    m.render();
    assert(m.result.metrics[1] == 1);

    m.reset(View::FixedPoint);
    m.set(0, 4);
    m.set(1, 12);
    m.render();
    assert(m.values[1] == 3);
    assert(m.result.metrics[0] == -1 && m.result.metrics[1] == .875);

    m.reset(View::Throughput);
    m.render();
    assert(m.result.metrics[0] == 1800000 && m.result.metrics[3] == 0);
    m.set(2, 440);
    m.render();
    assert(std::abs(m.result.metrics[1] - 6.5536) < 1e-9);
    assert(m.result.metrics[3] == 137856);

    using C = OneSegReceiverModel::C;
    const std::vector<C> samples{{1, 2}, {-3, .5}, {.25, -1}, {2, 0}};
    const auto roundtrip = OneSegReceiverModel::dft(OneSegReceiverModel::dft(samples), true);
    for (size_t i = 0; i < samples.size(); ++i) assert(std::abs(roundtrip[i] - samples[i]) < 1e-12);
    std::cout << "PASS " << configurations << " configurations and independent mathematical invariants\n";
}
