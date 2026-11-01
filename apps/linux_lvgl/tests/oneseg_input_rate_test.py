"""Check exact receive clocks and the lower-rate physical passband contract."""
import ast
from fractions import Fraction
import math
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest.mock import patch

tree = ast.parse(Path(sys.argv[1]).read_text(encoding='utf-8'))
definition = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'input_resampler')
calls = []
def low_pass(*parameters):
    return parameters
def resampler(**parameters):
    calls.append(parameters)
    return parameters
fake = SimpleNamespace(filter=SimpleNamespace(firdes=SimpleNamespace(low_pass=low_pass), rational_resampler_ccf=resampler))
context = dict(math=math)
exec(compile(ast.Module(body=[definition], type_ignores=[]), 'production-resampler', 'exec'), context)
with patch.dict(sys.modules, {'gnuradio':fake}):
    for rate in (450000,500000,576000):
        for numerator,denominator in ((64000000,63),(4000000,7)):
            result = context['input_resampler'](rate,numerator,denominator)
            assert Fraction(rate*result['interpolation'],result['decimation']) == Fraction(numerator,denominator)
            if rate in (450000,500000):
                gain,clock,cutoff,transition = result['taps']
                assert gain == result['interpolation'] and clock == gain*rate
                assert cutoff-transition/2 > 6000000/14/2
                assert cutoff+transition/2 < rate/2
    before = len(calls)
    try:
        context['input_resampler'](499999)
        raise AssertionError('Unsupported input clock accepted')
    except ValueError:
        pass
    assert len(calls) == before
print('All IQ clocks, exact native/compact conversion, 1seg passband and Nyquist margin passed')
