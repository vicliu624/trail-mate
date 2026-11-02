"""Reversible GPL native observation of coherent complete OFDM symbols."""
import hashlib
import importlib.util
from pathlib import Path

MARKER = 'TRAILMATE_CARRIER_SCHEMA_1'
FIELD = '\n                // TRAILMATE_CARRIER_SCHEMA_1\n                unsigned long long d_carrier_symbol = 0;\n'
PORT = '\n                  // TRAILMATE_CARRIER_SCHEMA_1\n                  message_port_register_out(pmt::mp("ofdm_carriers"));\n'
EXPORT = r'''
                        // TRAILMATE_CARRIER_SCHEMA_1: observe, never alter DSP.
                        const auto carrier_sequence = ++d_carrier_symbol;
                        const unsigned carrier_stride = 2048 / (d_active_carriers / 108);
                        const unsigned carrier_slot = (carrier_sequence - 1) % carrier_stride;
                        if (d_lab_enabled && carrier_slot < 4)
                        {
                            pmt::pmt_t frame = pmt::make_dict();
                            frame = pmt::dict_add(frame, pmt::mp("schema"), pmt::from_long(1));
                            frame = pmt::dict_add(frame, pmt::mp("sequence"), pmt::from_uint64(carrier_sequence));
                            frame = pmt::dict_add(frame, pmt::mp("burst"), pmt::from_uint64((carrier_sequence - 1) / carrier_stride + 1));
                            frame = pmt::dict_add(frame, pmt::mp("slot"), pmt::from_long(carrier_slot));
                            frame = pmt::dict_add(frame, pmt::mp("phase"), pmt::from_long(d_current_symbol));
                            frame = pmt::dict_add(frame, pmt::mp("phase_valid"), pmt::from_bool(d_symbol_acq));
                            frame = pmt::dict_add(frame, pmt::mp("cp_moved"), pmt::from_bool(d_moved_cp));
                            frame = pmt::dict_add(frame, pmt::mp("fft_length"), pmt::from_long(d_fft_length));
                            frame = pmt::dict_add(frame, pmt::mp("cp_length"), pmt::from_long(d_cp_length));
                            frame = pmt::dict_add(frame, pmt::mp("sample_rate"), pmt::from_double(d_lab_sample_rate));
                            frame = pmt::dict_add(frame, pmt::mp("equalizer_floor"), pmt::from_double(d_lab_equalizer));
                            frame = pmt::dict_add(frame, pmt::mp("raw"), pmt::init_c32vector(d_active_carriers, d_integer_freq_derotated));
                            frame = pmt::dict_add(frame, pmt::mp("equalized"), pmt::init_c32vector(d_active_carriers, out + i*d_active_carriers));
                            frame = pmt::dict_add(frame, pmt::mp("channel"), pmt::init_c32vector(d_active_carriers, d_channel_gain));
                            frame = pmt::dict_add(frame, pmt::mp("pilot_reference"), pmt::init_c32vector(d_active_carriers, d_pilot_values));
                            message_port_pub(pmt::mp("ofdm_carriers"), frame);
                        }
'''
TRACKING_ANCHOR = '            if (d_lab_enabled && d_lab_symbol % 32 == 0)'
TRACKING_BURST = '            if (d_lab_enabled && (d_lab_symbol % 32 == 0 || (d_lab_symbol - 1) % (2048 / (d_active_carriers / 108)) < 4))'


def module(name):
    spec = importlib.util.spec_from_file_location('carrier_'+name, Path(__file__).with_name(name+'.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def baseline(text, kind):
    if kind == 'source' and module('oneseg_cm0_backend').MARKER in text:
        text = module('oneseg_cm0_backend').compact_transform(text, reverse=True)
    module('oneseg_sync_lab_backend').verified_baseline(text.encode(), kind)


def transform(text, kind, reverse=False):
    changes = [( '                bool d_lab_configuration_sent = false;\n',
                 '                bool d_lab_configuration_sent = false;\n'+FIELD)] if kind == 'header' else [
        ('                  message_port_register_out(pmt::mp("ofdm_tracking"));',
         '                  message_port_register_out(pmt::mp("ofdm_tracking"));'+PORT),
        ('                        if(freq_error_output_connected)', EXPORT+'                        if(freq_error_output_connected)'),
        (TRACKING_ANCHOR, TRACKING_BURST)]
    for before, after in reversed(changes) if reverse else changes:
        # Recognize the first cached carrier adaptation, before phase linkage.
        if reverse and before == TRACKING_ANCHOR and text.count(before) == 1 and after not in text:
            continue
        old, new = (after, before) if reverse else (before, after)
        if text.count(old) != 1:
            raise RuntimeError('Preserving modified native carrier '+kind)
        text = text.replace(old, new, 1)
    return text


def restore(source, header):
    paths = ((Path(source),'source'),(Path(header),'header'))
    if not any(MARKER in path.read_text() for path, _ in paths):
        return
    changes = []
    for path, kind in paths:
        text = path.read_text()
        if MARKER not in text:
            raise RuntimeError('Incomplete native carrier adaptation')
        original = transform(text, kind, reverse=True)
        baseline(original, kind)
        changes.append((path,original))
    for path, text in changes:
        path.write_text(text)


def adapt(source, header):
    # Validate every input before replacing either member of the pair.
    changes = []
    for path, kind in ((Path(source),'source'),(Path(header),'header')):
        text = path.read_text()
        if MARKER in text:
            text = transform(text, kind, reverse=True)
        baseline(text, kind)
        changes.append((path,transform(text, kind)))
    for path, text in changes:
        path.write_text(text)
    return dict(schema=1, message_port='ofdm_carriers', consecutive_symbols=4, tracking_symbol_matches_output_sequence=True,
                observation='actual_pre_equalizer_equalized_channel_and_pilot_scrambling_reference',
                sources={path.name:hashlib.sha256(text.encode()).hexdigest() for path,text in changes})
