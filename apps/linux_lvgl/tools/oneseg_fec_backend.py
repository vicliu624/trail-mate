"""Exact-source GPL-3.0-or-later RS outcome instrumentation; no decode changes."""
import hashlib
from pathlib import Path

SOURCE_SHA = '7a7050e788463716c18c7c268d4a9fe1060f6eee5aba05c38a00e4e46ac2e6f9'
HEADER_SHA = '42fc9cf4f3d5c798593b54f1d663da67758e7022bf5e72559fe2584435301f07'
MARKER = 'TRAILMATE_RS_DIAGNOSTICS_1'
FIELDS = '''
      // TRAILMATE_RS_DIAGNOSTICS_1: decoder-owner cumulative outcomes.
      bool d_lab_fec_enabled = false;
      std::uint64_t d_lab_words = 0, d_lab_clean = 0, d_lab_corrected = 0;
      std::uint64_t d_lab_uncorrectable = 0, d_lab_symbols = 0, d_lab_maximum = 0;
'''
OBSERVATION = '''
                    // TRAILMATE_RS_DIAGNOSTICS_1
                    if (d_lab_fec_enabled)
                    {
                        ++d_lab_words;
                        if (nerrors_corrected < 0) ++d_lab_uncorrectable;
                        else if (nerrors_corrected == 0) ++d_lab_clean;
                        else
                        {
                            ++d_lab_corrected;
                            d_lab_symbols += nerrors_corrected;
                            if (static_cast<std::uint64_t>(nerrors_corrected) > d_lab_maximum)
                                d_lab_maximum = nerrors_corrected;
                        }
                        if (d_lab_words == 1 || d_lab_words % 64 == 0)
                        {
                            pmt::pmt_t stats = pmt::make_dict();
                            stats = pmt::dict_add(stats, pmt::mp("schema"), pmt::from_long(1));
'''
for key in ('words', 'clean', 'corrected', 'uncorrectable', 'symbols', 'maximum'):
    OBSERVATION += '                            stats = pmt::dict_add(stats, pmt::mp("'+key+'"), pmt::from_uint64(d_lab_'+key+'));\n'
OBSERVATION += '''                            message_port_pub(pmt::mp("fec_statistics"), stats);
                        }
                    }
'''


def replacements(kind):
    if kind == 'header':
        return [('#include <gnuradio/isdbt/reed_solomon_dec_isdbt.h>', '#include <gnuradio/isdbt/reed_solomon_dec_isdbt.h>\n#include <cstdint>'),
                ('     private:\n', '     private:\n'+FIELDS)]
    return [('#include <stdio.h>', '#include <stdio.h>\n#include <cstdlib>\n#include <cstring>\n#include <pmt/pmt.h>'),
            ('\n            d_last_ber_out = 0.5;',
             '\n            message_port_register_out(pmt::mp("fec_statistics"));\n'
             '            const char* lab = std::getenv("TRAIL_MATE_OFDM_DIAGNOSTICS");\n'
             '            d_lab_fec_enabled = lab && std::strcmp(lab, "1") == 0;\n\n'
             '            d_last_ber_out = 0.5;'),
            ('                float *ber_out = (float *) output_items[1];',
             '                float *ber_out = output_items.size() >= 2 ? (float *) output_items[1] : nullptr;'),
            ('                    nerrors_corrected = decode(out[k], in[j]);',
             '                    nerrors_corrected = decode(out[k], in[j]);'+OBSERVATION)]


def transform(text, kind, reverse=False):
    pairs = replacements(kind)
    for before, after in reversed(pairs) if reverse else pairs:
        old, new = (after, before) if reverse else (before, after)
        if text.count(old) != 1: raise RuntimeError('RS adaptation anchor differs: '+old[:60])
        text = text.replace(old, new, 1)
    return text


def baseline(data, kind):
    text = data.decode()
    if MARKER in text: text = transform(text, kind, reverse=True)
    expected = HEADER_SHA if kind == 'header' else SOURCE_SHA
    if hashlib.sha256(text.encode()).hexdigest() != expected:
        raise RuntimeError('Preserving modified RS '+kind+'; source hash differs')
    return text


def adapt(source, header):
    changes = []
    for path, kind in ((Path(source), 'source'), (Path(header), 'header')):
        changes.append((path, transform(baseline(path.read_bytes(), kind), kind).encode()))
    for path, data in changes:
        if path.read_bytes() != data: path.write_bytes(data)
    return dict(schema=1, source_sha256=hashlib.sha256(changes[0][1]).hexdigest(),
                header_sha256=hashlib.sha256(changes[1][1]).hexdigest(),
                message_port='fec_statistics', metric='RS_codeword_and_reported_symbol_counts_not_BER')
