"""Reversible qualified Mode3 FFT576 and SIMDe Viterbi build adaptations.

GPL source remains in the private build tree; unknown edits are never replaced.
"""
import hashlib
import importlib.util
from pathlib import Path

MARKER = 'TRAILMATE_CM0_OPTIMIZATION_1'
HEADER_SHA = 'a6eb495bf4000d0784f0c7b1137cef7ae93b469a36a78d434f496f2bbc5b6d3e'
CMAKE_SHA = '2dc992af47ec890545e7c3b45da59cd2fb6d1928579224e57eeafdd98bca4752'
COMPACT = '''
        // TRAILMATE_CM0_OPTIMIZATION_1
        namespace {
        int trailmate_cm0_fft_length(int mode) {
            const char* selected = std::getenv("TRAIL_MATE_ONESEG_COMPACT_FFT");
            return mode == 3 && selected && std::strcmp(selected, "576") == 0
                ? 576 : static_cast<int>(std::pow(2.0, 7 + mode));
        }
        }
'''
SIMDE_INCLUDE = '// TRAILMATE_CM0_OPTIMIZATION_1\n#include <simde/x86/sse2.h>'
SIMDE_CMAKE = '''
# TRAILMATE_CM0_OPTIMIZATION_1
if(TRAIL_MATE_SIMDE_VITERBI)
    set_source_files_properties(viterbi_decoder_impl.cc PROPERTIES
        COMPILE_DEFINITIONS "DTV_SSE2=1;SIMDE_ENABLE_NATIVE_ALIASES")
endif()
'''


def lab():
    spec = importlib.util.spec_from_file_location('cm0_sync_validation', Path(__file__).with_name('oneseg_sync_lab_backend.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compact_transform(text, reverse=False):
    if reverse:
        if text.count(COMPACT) != 1: raise RuntimeError('Compact helper differs')
        text = text.replace(COMPACT, '', 1)
        if text.count('trailmate_cm0_fft_length(mode)') != 2: raise RuntimeError('Compact geometry differs')
        text = text.replace('trailmate_cm0_fft_length(mode)', 'pow(2.0,7+mode)')
        anchor = 'tmcc_and_ac_positions(static_cast<int>(std::pow(2.0, 7 + mode)));'
        if text.count(anchor) != 1: raise RuntimeError('Compact pilot indexing differs')
        return text.replace(anchor, 'tmcc_and_ac_positions(d_fft_length);', 1)
    lab().verified_baseline(text.encode(), 'source')
    if text.count('pow(2.0,7+mode)') != 2: raise RuntimeError('Native FFT anchors differ')
    text = text.replace('pow(2.0,7+mode)', 'trailmate_cm0_fft_length(mode)')
    anchor = 'tmcc_and_ac_positions(d_fft_length);'
    if text.count(anchor) != 1: raise RuntimeError('Pilot indexing anchor differs')
    text = text.replace(anchor, 'tmcc_and_ac_positions(static_cast<int>(std::pow(2.0, 7 + mode)));', 1)
    anchor = '    namespace isdbt {\n'
    if text.count(anchor) != 1: raise RuntimeError('Native namespace differs')
    return text.replace(anchor, anchor+COMPACT, 1)


def original_header(text):
    if MARKER in text:
        if text.count(SIMDE_INCLUDE) != 1: raise RuntimeError('SIMDe include differs')
        text = text.replace(SIMDE_INCLUDE, '#include <xmmintrin.h>', 1)
    if hashlib.sha256(text.encode()).hexdigest() != HEADER_SHA: raise RuntimeError('Preserving modified Viterbi header')
    return text


def original_cmake(text):
    if MARKER in text:
        if text.count(SIMDE_CMAKE) != 1: raise RuntimeError('SIMDe build block differs')
        text = text.replace(SIMDE_CMAKE, '', 1)
    if hashlib.sha256(text.encode()).hexdigest() != CMAKE_SHA: raise RuntimeError('Preserving modified native CMake')
    return text


def restore(source, header, cmake):
    changes = []
    source, header, cmake = Path(source), Path(header), Path(cmake)
    if not any(MARKER in path.read_text() for path in (source, header, cmake)): return
    original_header(header.read_text())
    original_cmake(cmake.read_text())
    text = source.read_text()
    if MARKER in text:
        output = compact_transform(text, reverse=True)
        lab().verified_baseline(output.encode(), 'source')
        changes.append((source, output))
    for path, validator in ((header, original_header), (cmake, original_cmake)):
        text = path.read_text()
        if MARKER in text: changes.append((path, validator(text)))
    for path, output in changes: path.write_text(output)


def adapt(source, header, cmake, simde=False):
    source, header, cmake = Path(source), Path(header), Path(cmake)
    if simde:
        original_header(header.read_text())
        original_cmake(cmake.read_text())
    restore(source, header, cmake)
    changes = [(source, compact_transform(source.read_text()))]
    if simde:
        text = original_header(header.read_text())
        if text.count('#include <xmmintrin.h>') != 1: raise RuntimeError('Viterbi SSE include differs')
        changes.append((header, text.replace('#include <xmmintrin.h>', SIMDE_INCLUDE, 1)))
        text = original_cmake(cmake.read_text())
        anchor = 'include (CheckCCompilerFlag)'
        if text.count(anchor) != 1: raise RuntimeError('Native SIMD build anchor differs')
        changes.append((cmake, text.replace(anchor, SIMDE_CMAKE+anchor, 1)))
    for path, output in changes: path.write_text(output)
    return dict(schema=1, compact_fft_mode3=576, other_modes='original_geometry',
                simde_viterbi=simde, sources={path.name: hashlib.sha256(output.encode()).hexdigest() for path, output in changes})
