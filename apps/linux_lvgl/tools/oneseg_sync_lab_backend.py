"""Hash-pinned instrumentation for the real gr-isdbt synchronization loop.

The generated block retains the upstream GPL-3.0-or-later licence. Diagnostics
describe its actual normalized phase-advance state, never a teaching predictor.
"""
import hashlib
from pathlib import Path

BASE_SOURCE_SHA = "854af19ab5004fa0d8ee728fa9caf3429974e4957a18288797bac83e1e902cfe"
BASE_HEADER_SHA = "5f4be23d7d6aadba322641fbb6691d65689d6988c103deab53a4461cc0eb1cec"
MARKER = "TRAILMATE_OFDM_LAB_SCHEMA_1"

HELPER = r'''
// TRAILMATE_OFDM_LAB_SCHEMA_1
static float trailmate_lab_parameter(const char* name, float fallback, float low, float high)
{
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value || *end || !std::isfinite(parsed) || parsed < low || parsed > high)
        throw std::invalid_argument(std::string("Invalid OFDM parameter: ") + name);
    return static_cast<float>(parsed);
}
'''

FIELDS = '''
                // TRAILMATE_OFDM_LAB_SCHEMA_1: worker-owned tracking state.
                bool d_lab_enabled = false;
                unsigned long long d_lab_symbol = 0;
                float d_lab_sample_rate = 0;
                int d_lab_confirmation = 4;
'''

CONFIGURATION = '''
                  message_port_register_out(pmt::mp("ofdm_tracking"));
                  const char* lab = std::getenv("TRAIL_MATE_OFDM_DIAGNOSTICS");
                  d_lab_enabled = lab && std::strcmp(lab, "1") == 0;
                  d_lab_sample_rate = static_cast<float>((64000000.0 / 63.0) * d_fft_length / std::pow(2.0, 7 + mode));
                  d_freq_offset_max = static_cast<int>(trailmate_lab_parameter("TRAIL_MATE_OFDM_INTEGER_SEARCH", 10, 1, 32));
                  d_lab_confirmation = static_cast<int>(trailmate_lab_parameter("TRAIL_MATE_OFDM_OFFSET_CONFIRMATION", 4, 1, 64));
'''

TRACKING = '''
            const float lab_prior = d_est_freq;
            d_freq_aux = d_freq_aux + d_beta_freq*error;
            d_est_freq = d_est_freq + d_freq_aux + d_alpha_freq*error;
            if (d_lab_enabled && d_lab_symbol % 32 == 0)
            {
                const double observed = double(lab_prior) + error;
                pmt::pmt_t sample = pmt::make_dict();
                sample = pmt::dict_add(sample, pmt::mp("schema"), pmt::from_long(1));
                sample = pmt::dict_add(sample, pmt::mp("symbol"), pmt::from_uint64(d_lab_symbol));
                sample = pmt::dict_add(sample, pmt::mp("prior"), pmt::from_double(lab_prior));
                sample = pmt::dict_add(sample, pmt::mp("observed"), pmt::from_double(observed));
                sample = pmt::dict_add(sample, pmt::mp("corrected"), pmt::from_double(d_est_freq));
                sample = pmt::dict_add(sample, pmt::mp("correction"), pmt::from_double(double(d_est_freq) - lab_prior));
                sample = pmt::dict_add(sample, pmt::mp("residual"), pmt::from_double(observed - d_est_freq));
                sample = pmt::dict_add(sample, pmt::mp("nco_phase"), pmt::from_double(d_phase));
                sample = pmt::dict_add(sample, pmt::mp("frequency_hz"), pmt::from_double(double(d_est_freq) * d_lab_sample_rate / d_fft_length / (2 * 3.141592653589793)));
                sample = pmt::dict_add(sample, pmt::mp("sampling_ppm"), pmt::from_double(double(d_est_delta) * 1000000));
                sample = pmt::dict_add(sample, pmt::mp("alpha"), pmt::from_double(d_alpha_freq));
                sample = pmt::dict_add(sample, pmt::mp("beta"), pmt::from_double(d_beta_freq));
                message_port_pub(pmt::mp("ofdm_tracking"), sample);
            }
'''

# Preserve exact recognition of the first generated development revision.
TRACKING_V1 = TRACKING
TRACKING = TRACKING.replace(
    'const float lab_prior = d_est_freq;',
    'const float lab_prior = d_est_freq;\n            const float lab_integrator = d_freq_aux;\n            const double lab_prediction = double(lab_prior) + lab_integrator;').replace(
    'pmt::mp("schema"), pmt::from_long(1)', 'pmt::mp("schema"), pmt::from_long(2)').replace(
    '                message_port_pub(pmt::mp("ofdm_tracking"), sample);',
    '''                sample = pmt::dict_add(sample, pmt::mp("prediction"), pmt::from_double(lab_prediction));
                sample = pmt::dict_add(sample, pmt::mp("integrator"), pmt::from_double(lab_integrator));
                message_port_pub(pmt::mp("ofdm_tracking"), sample);''')
LEGACY_HELPER, LEGACY_FIELDS, LEGACY_CONFIGURATION = HELPER, FIELDS, CONFIGURATION
TRACKING_FORECAST_ONLY = TRACKING
CONFIG_READBACK = '''
                if (d_lab_symbol == 32)
                {
                    pmt::pmt_t configuration = pmt::make_dict();
                    configuration = pmt::dict_add(configuration, pmt::mp("schema"), pmt::from_long(1));
'''
for key, expression in (
        ('mode', '1 + std::lround(std::log2(double(d_active_carriers) / 108))'),
        ('fft_length', 'd_fft_length'), ('cp_length', 'd_cp_length'),
        ('carriers', 'd_active_carriers'), ('sample_rate', 'd_lab_sample_rate'),
        ('alpha', 'd_alpha_freq'), ('beta', 'd_beta_freq'),
        ('timing_alpha', 'd_alpha_timing'), ('timing_beta', 'd_beta_timing'),
        ('integer_search', 'd_freq_offset_max'), ('offset_confirmation', 'd_lab_confirmation'),
        ('channel_average', 'd_lab_average'), ('equalizer_floor', 'd_lab_equalizer'),
        ('sampling_ppm', 'double(d_est_delta) * 1000000')):
    CONFIG_READBACK += '                    configuration = pmt::dict_add(configuration, pmt::mp("'+key+'"), pmt::from_double('+expression+'));\n'
CONFIG_READBACK += '''                    sample = pmt::dict_add(sample, pmt::mp("configuration"), configuration);
                }
'''
TRACKING = TRACKING.replace('                message_port_pub(pmt::mp("ofdm_tracking"), sample);',
                            CONFIG_READBACK+'                message_port_pub(pmt::mp("ofdm_tracking"), sample);')
TRACKING_FIRST_ACK_ONLY = TRACKING
TRACKING = TRACKING.replace('if (d_lab_symbol == 32)', 'if (!d_lab_configuration_sent)').replace(
    '                    sample = pmt::dict_add(sample, pmt::mp("configuration"), configuration);',
    '                    sample = pmt::dict_add(sample, pmt::mp("configuration"), configuration);\n                    d_lab_configuration_sent = true;')
HELPER += r'''
static int trailmate_lab_integer(const char* name, int fallback, int low, int high)
{
    const float value = trailmate_lab_parameter(name, fallback, low, high);
    if (value != std::floor(value)) throw std::invalid_argument(std::string("Non-integer OFDM parameter: ") + name);
    return static_cast<int>(value);
}
'''
FIELDS += '''
                int d_lab_average = 1;
                float d_lab_equalizer = 0;
                bool d_lab_channel_ready = false;
'''
FIELDS_WITHOUT_ACK_FLAG = FIELDS
FIELDS += '                bool d_lab_configuration_sent = false;\n'
CONFIGURATION = CONFIGURATION.replace(
    'static_cast<int>(trailmate_lab_parameter("TRAIL_MATE_OFDM_INTEGER_SEARCH", 10, 1, 32))',
    'trailmate_lab_integer("TRAIL_MATE_OFDM_INTEGER_SEARCH", 10, 1, 32)').replace(
    'static_cast<int>(trailmate_lab_parameter("TRAIL_MATE_OFDM_OFFSET_CONFIRMATION", 4, 1, 64))',
    'trailmate_lab_integer("TRAIL_MATE_OFDM_OFFSET_CONFIRMATION", 4, 1, 64)') + '''
                  message_port_register_out(pmt::mp("ofdm_controls_v1"));
                  d_lab_average = trailmate_lab_integer("TRAIL_MATE_OFDM_CHANNEL_AVERAGE", 1, 1, 32);
                  d_lab_equalizer = trailmate_lab_parameter("TRAIL_MATE_OFDM_EQUALIZER_FLOOR", 0, 0, 1);
                  d_est_delta = trailmate_lab_parameter("TRAIL_MATE_OFDM_SAMPLE_CLOCK_PPM", 0, -500, 500) / 1000000.0f;
                  d_samp_inc = 1.0f - d_est_delta;
'''


def source_replacements(legacy=False, forecast=True, readback=True, reliable_ack=True):
    result = [
        ("#include <complex>\n", "#include <complex>\n#include <cmath>\n#include <cstdlib>\n#include <cstring>\n#include <stdexcept>\n#include <string>\n#include <pmt/pmt.h>\n"),
        ("    namespace isdbt {\n", "    namespace isdbt {\n" + (LEGACY_HELPER if legacy else HELPER)),
        ("                  float loop_bw_freq = 3.14159/100; ",
         '                  float loop_bw_freq = trailmate_lab_parameter("TRAIL_MATE_OFDM_CARRIER_BW", 3.14159/100, 0.00001, 0.5);'),
        ("                  float loop_bw_timing = 3.14159/10000; ",
         '                  float loop_bw_timing = trailmate_lab_parameter("TRAIL_MATE_OFDM_TIMING_BW", 3.14159/10000, 0.000001, 0.1);'),
        ("                  float damping = sqrtf(2.0f)/2.0f; ",
         '                  float damping = trailmate_lab_parameter("TRAIL_MATE_OFDM_DAMPING", sqrtf(2.0f)/2.0f, 0.1, 3);'),
        ("                  d_est_freq = 0; ", "                  d_est_freq = 0; " + (LEGACY_CONFIGURATION if legacy else CONFIGURATION)),
        ("            d_freq_aux = d_freq_aux + d_beta_freq*error; \n            d_est_freq = d_est_freq + d_freq_aux + d_alpha_freq*error; ", ((TRACKING if reliable_ack else TRACKING_FIRST_ACK_ONLY) if readback and not legacy else TRACKING_FORECAST_ONLY) if forecast else TRACKING_V1),
        ("                gr_complex result_1st = gr_complex(0.0, 0.0);  ",
         "                ++d_lab_symbol;\n                gr_complex result_1st = gr_complex(0.0, 0.0);  "),
        ("if (d_freq_offset_disagree_count > 3)", "if (d_freq_offset_disagree_count >= d_lab_confirmation)"),
    ]
    if not legacy:
        result += [
            ('                        linearly_estimate_channel_taps(d_current_symbol, d_channel_gain);',
             '                        linearly_estimate_channel_taps(d_current_symbol, d_channel_gain);\n'
             '                        if (d_lab_average > 1 && d_lab_channel_ready && d_symbol_acq && !d_moved_cp)\n'
             '                            for (int carrier = 0; carrier < d_active_carriers; ++carrier)\n'
             '                                d_channel_gain[carrier] = (d_channel_gain[carrier] + float(d_lab_average - 1) * d_previous_channel_gain[carrier]) / float(d_lab_average);\n'
             '                        d_lab_channel_ready = d_symbol_acq && !d_moved_cp;'),
            ('out[i*d_active_carriers +carrier] = d_integer_freq_derotated[carrier]/d_channel_gain[carrier];',
             'out[i*d_active_carriers +carrier] = d_lab_equalizer == 0 ? d_integer_freq_derotated[carrier]/d_channel_gain[carrier]\n'
             '                                : d_integer_freq_derotated[carrier] * std::conj(d_channel_gain[carrier]) / (std::norm(d_channel_gain[carrier]) + d_lab_equalizer);')]
    return result


def header_replacements(legacy=False, readback=True):
    return [("            private:\n", "            private:\n" + (LEGACY_FIELDS if legacy else FIELDS if readback else FIELDS_WITHOUT_ACK_FLAG))]


def transform(text, replacements, reverse=False):
    for before, after in (reversed(replacements) if reverse else replacements):
        old, new = (after, before) if reverse else (before, after)
        if text.count(old) != 1:
            raise RuntimeError("OFDM lab adaptation anchor differs: " + old[:70])
        text = text.replace(old, new, 1)
    return text


def verified_baseline(data, kind):
    expected = BASE_SOURCE_SHA if kind == "source" else BASE_HEADER_SHA
    text = data.decode()
    if MARKER in text:
        for legacy in (False, True):
            for forecast in (True, False):
                for readback in (True, False):
                    for reliable_ack in (True, False):
                        try:
                            candidate = transform(text, source_replacements(legacy, forecast, readback, reliable_ack) if kind == "source" else header_replacements(legacy, readback), reverse=True)
                        except RuntimeError:
                            continue
                        if hashlib.sha256(candidate.encode()).hexdigest() == expected:
                            return candidate
        raise RuntimeError("Preserving modified OFDM " + kind + "; adaptation hash differs")
    if hashlib.sha256(text.encode()).hexdigest() != expected:
        raise RuntimeError("Preserving modified OFDM " + kind + "; baseline hash differs")
    return text


def is_instrumented_source(data):
    if MARKER not in data.decode():
        return False
    verified_baseline(data, "source")
    return True


def adapt(source, header):
    source, header = Path(source), Path(header)
    changes = []
    for path, kind, replacements in ((source, "source", source_replacements()), (header, "header", header_replacements())):
        data = path.read_bytes()
        base = verified_baseline(data, kind)
        output = transform(base, replacements).encode()
        changes.append((path, output))
    # Validate both files before changing either one.
    for path, output in changes:
        if path.read_bytes() != output:
            path.write_bytes(output)
    return dict(schema=2, configuration_schema=1, source_sha256=hashlib.sha256(changes[0][1]).hexdigest(),
                header_sha256=hashlib.sha256(changes[1][1]).hexdigest(),
                phase_unit="radians_per_useful_symbol", message_port="ofdm_tracking")
