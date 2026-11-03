# 1seg receiver views

The CM0 receiver retains its seven stage buttons, 320 by 240 programme viewport,
direct parameter controls and lower analysis plots. The stage-local view picker
selects **Actual** or an independent **MODEL** view. The `?` button opens formulas,
variable definitions, relationships and applicability conditions. Closing it
returns to the same receiver page. Formulas no longer consume permanent plot area.

The maintained display is 1232 by 568 in landscape, with the accepted 270-degree
rotation. These views do not add an orientation control. The factory reception
settings remain 450000 IQ pairs/s, requested SPI clock 8700000 Hz and gain step 9.

## View catalogue

| Stage | Additional receiver views |
|---|---|
| Input | Sine projections; complex I/Q and downconversion; sampling, quantization, aliasing and ADC overload; fixed point; FIFO and TS throughput |
| Filter | Finite Hann-windowed FIR response and integer decimation |
| Symbol | Cyclic prefix, multipath and FFT-window protection; sliding CP correlation and a separate clock-drift model |
| Subcarriers | DFT reference multiplication and partial accumulation; finite-window orthogonality |
| Tracking | Same-symbol observation, prior prediction, posterior correction, innovation and residual |
| Channel | Pilot-derived complex interpolation, amplitude/phase and ZF/MMSE equalization |
| Decoder | Received constellation decisions; signed soft bits; deinterleaving and Viterbi; RS byte-error/erasure guarantee |

The existing complete four-symbol carrier resource grid remains under Actual in
Subcarriers, with its data/SP/TMCC/AC roles, raw/equalized vectors, channel estimates
and exact-sequence native phase association. Actual can represent live, recorded,
or computed synthetic reception; the original producer provenance is retained.
The separate model views never borrow native timestamps or claim radio lock.

There are no transmitter-synthesis, IFFT-construction, transmitter-clipping or
PAPR pages. ADC overload remains relevant to receiving. Internally generated
reference samples are fixtures for receiver calculations, not a transmission API.

## Parameter isolation

Model controls change only `OneSegReceiverModel`. They never change
`OneSegOfdmExperiment`, acquire a radio lease or call the workbench's receiver
Apply, Save, Load or factory-reset callbacks. Hardware reception selectors are
disabled in model views. Selecting Actual restores the original controls and
recorded-source restrictions. Programme presentation is independent of the plots.

In a model view, Default restores that view's model values. Save and Load are
explicitly session-local model snapshots; they do not write a reception profile.
Actual retains the original persistent profile behavior. Model parameters are
remembered between view changes within the current workbench instance.

## Mathematical boundaries

- The ADC model uses endpoint-inclusive levels, including the correct offset in
  its quantizer equation. Higher precision does not repair aliasing or clipping.
- DFT is calculated from the samples; the displayed spectrum uses all 64 samples
  while the vector path uses the selected partial count. FFT is an algorithm for
  DFT rather than a different transform.
- The CP model has distinct adjacent symbols. For echo delay `d`, guard `G` and
  integer window shift, the safe interval is `d-G <= shift <= 0`. The diagonal
  comparison includes the window's frequency-dependent phase factor.
- CP CFO uses `r[n+N] * conj(r[n])`, making a positive phase argument a positive
  normalized CFO. Integer-bin ambiguity is not hidden. Clock drift is labelled as
  an independent first-order model, not resampled IQ from the correlation plot.
- Channel estimates use the same pilot observations as the received vector.
  EVM is calculated on data carriers, excluding self-fitted pilots. Channel truth
  and payload references are available only in the model.
- The phase model observes absolute model phase in radians. Native tracking
  continues to report phase advance in rad/Tu; these definitions are not mixed.
- LLR is `log(P(bit=0|Y)/P(bit=1|Y))`; positive means zero. The normalized QPSK
  expression is exact for equal priors and AWGN. Higher-order max-log is approximate.
- The executable Viterbi example uses K=3, octal generators 7/5, rate 1/2, a
  reversible block permutation and zero-tail traceback. It is distinguished from
  ISDB-T K=7, puncturing, native interleavers and the subsequent RS decoder.
  Burst reliability scales damaged observations independently of unaffected
  observations. The decision plot draws the QPSK/16-QAM/64-QAM region boundaries.
- RS shows the guarantee `2e+s <= 16` for RS(204,188). It does not run an RS solver
  or manufacture corrected/uncorrectable statistics. Byte-symbol outcomes are not BER.
- Fixed-point results use the documented rounding convention and clamp fractional
  bits below total word width. The view is not a complete fixed-point FFT.
- FIFO is a constant-rate bounded queue. The capacity example includes GI,
  convolutional-code rate and RS overhead, but is not measured programme bitrate.
  DMA is a transfer mechanism that can operate with SPI, not a replacement interface.

## Verification

`tests/oneseg/test_receiver_model.cpp` checks every model at parameter limits and
independent invariants for DFT projection, CP protection, synchronization, flat-
channel recovery, phase innovation/residual, LLR, Viterbi, RS, quantization and FIFO.

`oneseg_receiver_views_test.cpp` exercises all model controls and help popups at
1232 by 568. `oneseg_receiver_workbench_test.cpp` exercises the actual stage picker,
receiver callback isolation, source restrictions, factory settings and fullscreen.
Existing workbench, coherent-carrier and synthetic-IQ UI tests remain enabled.
The native CI job builds and executes both new UI targets explicitly.

Native and ARM integration must be checked before installing a release. Recorded
IQ validates decoding and presentation without operating a transmitter. It must
not be described as renewed live RF endurance.
