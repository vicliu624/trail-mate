#!/usr/bin/env python3
"""1seg decoder: signed IQ8 stdin (576 ksample/s) to raw TS stdout.

Select automatic parameter acquisition or the explicit OneSegCast profile.
No hardware is opened and no RF lock or playback success state is emitted.
"""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import queue
import select
import signal
import sys
import threading
import time

PROFILE = "onesegcast-mode3-gi16-qpsk23-ti4"
AUTO_PROFILE = "auto"
REQUIRED = ("ofdm_synchronization_1seg", "tmcc_decoder_1seg",
            "frequency_deinterleaver_1seg", "time_deinterleaver_1seg",
            "symbol_demapper_1seg", "bit_deinterleaver", "viterbi_decoder",
            "byte_deinterleaver", "energy_descrambler", "reed_solomon_dec_isdbt")


def ofdm_candidates(native_iq, limit=3):
    """Rank mode/GI hypotheses using periodic cyclic-prefix correlation.

    Input is complex IQ at 64 MHz / 63, the private backend's native rate.
    Correlation does not establish TMCC validity, service discovery or RF lock.
    Work/storage are bounded independently of the length of the input recording.
    """
    import numpy as np
    if not 1 <= limit <= 12:
        raise ValueError("Invalid OFDM candidate limit")
    samples = np.asarray(native_iq)
    if samples.ndim != 1:
        raise ValueError("OFDM acquisition requires one-dimensional complex IQ")
    samples = samples[:131072].astype(np.complex128)
    if len(samples) < 21504 or not np.all(np.isfinite(samples)):
        return []
    samples -= np.mean(samples)
    energy = float(np.mean(np.abs(samples) ** 2))
    if not np.isfinite(energy) or energy <= 1e-24:
        return []
    samples /= energy ** .5
    ranked = []
    for mode in (1, 2, 3):
        fft = 2 ** (7 + mode)
        product = samples[:-fft] * np.conj(samples[fft:])
        left_energy, right_energy = np.abs(samples[:-fft]) ** 2, np.abs(samples[fft:]) ** 2
        for guard in (4, 8, 16, 32):
            prefix = fft // guard
            period = fft + prefix
            cycles = min(64, len(product) // period)
            if cycles < 16:
                continue

            def fold(value):
                return value[:cycles * period].reshape(cycles, period).sum(axis=0)

            def windows(value):
                extended = np.concatenate((value, value[:prefix - 1]))
                sums = np.concatenate(([0], np.cumsum(extended)))
                return sums[prefix:] - sums[:-prefix]

            folded = fold(product)
            left, right = fold(left_energy), fold(right_energy)
            correlation, le, re = windows(folded), windows(left), windows(right)
            coherence = np.abs(correlation) / np.sqrt(np.maximum(le * re, 1e-24))
            # A carrier/tone correlates everywhere; require periodic CP contrast.
            background = np.abs(folded.sum() - correlation) / np.sqrt(
                np.maximum((left.sum() - le) * (right.sum() - re), 1e-24))
            scores = np.clip(coherence - background, 0, 1)
            offset = int(np.argmax(scores))
            score = float(scores[offset])
            if score >= .25:
                ranked.append(dict(mode=mode, guard_denominator=guard, score=score,
                                   prefix_offset=offset, symbols=cycles))
    ranked.sort(key=lambda candidate: candidate["score"], reverse=True)
    return ranked[:limit]


def _complex_capture_source(samples):
    """Hand complex IQ to native blocks without element-wise Python conversion.

    vector_source's Python binding holds the GIL while copying large sequences,
    which prevents the live IQ reader from servicing its pipe. An anonymous
    memory file used only for data lets the native block read independently.
    """
    import numpy as np
    from gnuradio import blocks, gr
    data = np.ascontiguousarray(samples, dtype=np.complex64)
    if data.ndim != 1 or data.nbytes > 2031746 * 8:
        raise ValueError("Invalid native IQ capture size")
    descriptor = os.memfd_create("trailmate-iq-capture", os.MFD_CLOEXEC)
    try:
        view = memoryview(data).cast("B")
        while view:
            count = os.write(descriptor, view)
            if count <= 0:
                raise OSError("Native IQ capture accepted zero bytes")
            view = view[count:]
        source = blocks.file_source(gr.sizeof_gr_complex, "/proc/self/fd/" + str(descriptor), False)
        if not source.seek(0, os.SEEK_SET):
            source.close()
            raise OSError("Cannot rewind native IQ capture")
        return source  # The native block owns its independent open descriptor.
    finally:
        os.close(descriptor)


def acquire_tmcc_parameters(native_iq, candidates=None, cancel=None):
    """Confirm blind OFDM hypotheses against the actual native TMCC decoder.

    The bounded capture is replayed into the acquisition blocks, never RF. A
    result requires two matching native parity-validated current-layer messages.
    Programme/transport confirmation remains a separate reception requirement.
    """
    import numpy as np
    import pmt
    from gnuradio import blocks, gr, isdbt
    samples = np.asarray(native_iq)
    if samples.ndim != 1 or len(samples) > 2031746 or not np.all(np.isfinite(samples)):
        raise ValueError("Invalid or oversized native acquisition capture")
    samples = samples.astype(np.complex64)
    cancel = cancel if cancel is not None else threading.Event()
    candidates = ofdm_candidates(samples) if candidates is None else candidates
    if len(candidates) > 3:
        raise ValueError("Too many OFDM acquisition hypotheses")
    for hypothesis in candidates:
        if cancel.is_set():
            return None
        mode, guard = hypothesis["mode"], hypothesis["guard_denominator"]
        if mode not in (1, 2, 3) or guard not in (4, 8, 16, 32):
            raise ValueError("Invalid OFDM acquisition hypothesis")

        class Collector(gr.basic_block):
            def __init__(self):
                gr.basic_block.__init__(self, "Acquisition TMCC confirmation", in_sig=None, out_sig=None)
                self.confirmed = threading.Event()
                self.candidate, self.count, self.result = None, 0, None
                self.message_port_register_in(pmt.intern("in"))
                self.set_msg_handler(pmt.intern("in"), self.accept)

            def accept(self, message):
                try:
                    schema = pmt.to_long(pmt.dict_ref(message, pmt.intern("schema"), pmt.PMT_NIL))
                    native_mode = pmt.to_long(pmt.dict_ref(message, pmt.intern("mode"), pmt.PMT_NIL))
                    bits = pmt.u8vector_elements(pmt.dict_ref(message, pmt.intern("bits"), pmt.PMT_NIL))
                    if schema != 1 or native_mode != mode:
                        raise ValueError("Unexpected native acquisition schema/mode")
                    parameters = tmcc_parameters(bits, native_mode, guard)
                    fingerprint = tuple(sorted(parameters.items()))
                    self.count = self.count + 1 if fingerprint == self.candidate else 1
                    self.candidate = fingerprint
                    if self.count >= 2:
                        self.result = parameters
                        self.confirmed.set()
                except (ValueError, TypeError, RuntimeError):
                    self.candidate, self.count = None, 0

        graph = gr.top_block("Trail Mate blind TMCC acquisition")
        source = _complex_capture_source(samples)
        synchronization = isdbt.ofdm_synchronization_1seg(mode, 1 / guard)
        tmcc = isdbt.tmcc_decoder_1seg(mode, False)
        ports = tmcc.message_ports_out()
        if not any(pmt.symbol_to_string(pmt.vector_ref(ports, i)) == "tmcc_parameters"
                   for i in range(pmt.length(ports))):
            raise RuntimeError("Automatic acquisition requires the native TMCC message capability")
        sink = blocks.null_sink(gr.sizeof_gr_complex * 96 * 2 ** (mode - 1))
        collector = Collector()
        graph.connect(source, synchronization, tmcc, sink)
        graph.msg_connect(tmcc, "tmcc_parameters", collector, "in")
        finished, failures = threading.Event(), []

        def wait():
            try:
                graph.wait()
            except BaseException as error:
                failures.append(str(error))
            finally:
                finished.set()

        graph.start()
        waiter = threading.Thread(target=wait, name="oneseg-acquisition-wait", daemon=True)
        waiter.start()
        deadline = time.monotonic() + 5
        try:
            while not finished.wait(.01) and not collector.confirmed.is_set() and not cancel.is_set():
                if time.monotonic() >= deadline:
                    raise RuntimeError("Native TMCC acquisition exceeded five seconds")
        finally:
            graph.stop()
            if not finished.wait(5):
                raise RuntimeError("Native TMCC acquisition did not stop")
            waiter.join()
            source.close()
        if failures:
            raise RuntimeError("Native TMCC acquisition: " + failures[0])
        if cancel.is_set():
            return None
        if collector.confirmed.is_set():
            return collector.result
    return None


def resample_acquisition_iq(capture, cancel):
    """Resample a bounded signed-IQ prefix with the production GNU Radio filter."""
    import numpy as np
    from gnuradio import blocks, filter, gr
    if len(capture) % 2 or len(capture) > 1154560:
        raise ValueError("Invalid acquisition IQ prefix size")
    if cancel.is_set():
        return None
    raw = np.frombuffer(capture, dtype=np.int8)
    iq = (raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)) / 128
    with os.fdopen(os.memfd_create("trailmate-iq-resampled", os.MFD_CLOEXEC), "w+b", buffering=0) as captured:
        graph = gr.top_block("Trail Mate bounded acquisition resampler")
        source = _complex_capture_source(iq)
        sink = blocks.file_sink(gr.sizeof_gr_complex, "/proc/self/fd/" + str(captured.fileno()), False)
        try:
            graph.connect(source, filter.rational_resampler_ccc(interpolation=1000, decimation=567), sink)
            finished, failures = threading.Event(), []

            def wait():
                try:
                    graph.wait()
                except BaseException as error:
                    failures.append(str(error))
                finally:
                    finished.set()

            graph.start()
            waiter = threading.Thread(target=wait, name="oneseg-resampler-wait", daemon=True)
            waiter.start()
            deadline = time.monotonic() + 5
            try:
                while not finished.wait(.01) and not cancel.is_set():
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Acquisition resampling exceeded five seconds")
            finally:
                graph.stop()
                if not finished.wait(5):
                    raise RuntimeError("Acquisition resampler did not stop")
                waiter.join()
            if failures:
                raise RuntimeError("Acquisition resampler: " + failures[0])
            sink.close()
            sink = None
            data = os.pread(captured.fileno(), 2031746 * 8 + 8, 0)
            if len(data) % 8 or len(data) > 2031746 * 8:
                raise RuntimeError("Invalid resampled IQ capture size")
            return None if cancel.is_set() else np.frombuffer(data, dtype=np.complex64)
        finally:
            source.close()
            if sink is not None:
                sink.close()


def build_decoder_chain(source, sink, parameters, native_rate=False):
    """Build the actual receive chain from validated acquisition parameters."""
    from gnuradio import filter, isdbt
    mode, guard = parameters["mode"], parameters["guard_denominator"]
    constellation, rate = parameters["constellation"], parameters["code_rate_index"]
    interleaving = parameters["time_interleaving"]
    if mode not in (1, 2, 3) or guard not in (4, 8, 16, 32):
        raise ValueError("Invalid decoder OFDM parameters")
    if constellation not in (4, 16, 64) or rate not in range(5):
        raise ValueError("Unsupported decoder modulation/code rate")
    if interleaving not in tuple(depth * 2 ** (3 - mode) for depth in (0, 1, 2, 4)):
        raise ValueError("Unsupported decoder time interleaving")
    tmcc = isdbt.tmcc_decoder_1seg(mode, False)
    chain = [source]
    if not native_rate:
        chain.append(filter.rational_resampler_ccc(interpolation=1000, decimation=567))
    chain.extend([isdbt.ofdm_synchronization_1seg(mode, 1 / guard), tmcc,
                  isdbt.frequency_deinterleaver_1seg(mode), isdbt.time_deinterleaver_1seg(mode, interleaving),
                  isdbt.symbol_demapper_1seg(mode, constellation), isdbt.bit_deinterleaver(mode, 1, constellation),
                  isdbt.viterbi_decoder(constellation, rate), isdbt.byte_deinterleaver(),
                  isdbt.energy_descrambler(), isdbt.reed_solomon_dec_isdbt(), sink])
    return chain, tmcc


def tmcc_parameters(bits, mode, guard_denominator):
    """Read current layer A from the native parity-validated schema-1 frame.

    Mode/GI belong to the acquisition hypothesis, not TMCC's layer payload.
    This function cannot establish RF lock or a receivable television service.
    """
    if mode not in (1, 2, 3) or guard_denominator not in (4, 8, 16, 32):
        raise ValueError("Unsupported OFDM acquisition hypothesis")
    if len(bits) != 204 or any(value not in (0, 1) for value in bits):
        raise ValueError("Invalid native TMCC bit vector")
    sync = tuple(bits[1:17])
    even = (0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0)
    if sync not in (even, tuple(1 - bit for bit in even)):
        raise ValueError("Invalid TMCC frame synchronization")

    def field(begin, end):
        value = 0
        for bit in bits[begin:end]:
            value = value * 2 + bit
        return value

    modulation, rate, interleaving, segments = field(28, 31), field(31, 34), field(34, 37), field(37, 41)
    if field(20, 22) != 0 or not bits[27] or segments != 1:
        raise ValueError("TMCC does not describe a one-segment partial-reception TV layer")
    if modulation not in (1, 2, 3) or rate not in range(5) or interleaving not in range(4):
        raise ValueError("Unsupported current TMCC layer parameters")
    return dict(mode=mode, guard_denominator=guard_denominator,
                modulation={1: "QPSK", 2: "16QAM", 3: "64QAM"}[modulation],
                constellation={1: 4, 2: 16, 3: 64}[modulation],
                code_rate=("1/2", "2/3", "3/4", "5/6", "7/8")[rate], code_rate_index=rate,
                time_interleaving=(0, 1, 2, 4)[interleaving] * (2 ** (3 - mode)),
                partial_reception=True, emergency=bool(bits[26]))


def emit(event, **fields):
    print(json.dumps(dict(event=event, **fields)), file=sys.stderr, flush=True)


def run(arguments):
    # Isolate upstream native printf output from the binary transport channel.
    output = os.dup(sys.stdout.fileno())
    os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
    os.set_blocking(output, False)
    automatic = arguments.profile == AUTO_PROFILE
    try:
        import numpy as np
        import pmt
        # Load native bindings before readiness, while no live IQ needs draining.
        from gnuradio import blocks, gr, filter, isdbt
        missing = [name for name in REQUIRED if not hasattr(isdbt, name)]
        if missing:
            raise RuntimeError("Missing 1seg backend blocks: " + ", ".join(missing))
        tmcc = isdbt.tmcc_decoder_1seg(3, False)
        ports = tmcc.message_ports_out()
        tmcc_port = any(pmt.symbol_to_string(pmt.vector_ref(ports, i)) == "tmcc_parameters"
                        for i in range(pmt.length(ports)))
        if automatic and not tmcc_port:
            raise RuntimeError("Automatic acquisition requires the native TMCC message capability")
    except (ImportError, RuntimeError) as error:
        emit("error", message=str(error))
        os.close(output)
        return 2
    if arguments.check_backend:
        emit("backend_ready", profile=arguments.profile, input_rate=576000,
             native_rate=64000000 / 63, backend_path=str(isdbt.__file__),
             parameter_autodetection=automatic, parameter_observation=tmcc_port)
        os.close(output)
        return 0

    stop = threading.Event()
    failures = []
    failure_exit = 3
    fault_lock = threading.Lock()
    configuration = dict(mode=3, guard_denominator=16, constellation=4, code_rate_index=1, time_interleaving=4)

    def fail(message, exit_code=3):
        nonlocal failure_exit
        with fault_lock:
            if not failures:
                failures.append(message)
                failure_exit = exit_code
                emit("error", message=message)
        stop.set()

    class IqInput(gr.sync_block):
        def __init__(self):
            gr.sync_block.__init__(self, "Signed IQ8 pipe", in_sig=None, out_sig=[np.complex64])
            # Automatic acquisition retains live data while probing the prefix.
            # 2048 * 1280 bytes is bounded to about 2.28 seconds of IQ, not drops.
            self.queue = queue.Queue(2048 if automatic else 8)
            self.replay = deque()
            self.pending = b""
            self.eof = threading.Event()
            self.read_bytes = self.processed_pairs = self.high_water = 0
            self.reader = threading.Thread(target=self.read_pipe, name="oneseg-iq-input", daemon=True)

        def read_pipe(self):
            try:
                while not stop.is_set():
                    if not select.select([sys.stdin.fileno()], [], [], .05)[0]:
                        continue
                    data = os.read(sys.stdin.fileno(), 1280)
                    if not data:
                        self.eof.set()
                        return
                    self.read_bytes += len(data)
                    deadline = time.monotonic() + .25
                    while not stop.is_set():
                        try:
                            self.queue.put(data, timeout=.025)
                            self.high_water = max(self.high_water, self.queue.qsize())
                            break
                        except queue.Full:
                            if time.monotonic() >= deadline:
                                fail("IQ decoder queue remained full for 250 ms")
            except (OSError, ValueError) as error:
                fail("IQ input: " + str(error))
            finally:
                self.eof.set()

        def work(self, inputs, outputs):
            if stop.is_set():
                return -1
            while len(self.pending) < 2:
                try:
                    self.pending += self.replay.popleft() if self.replay else self.queue.get(timeout=.025)
                except queue.Empty:
                    if stop.is_set():
                        return -1
                    if self.eof.is_set():
                        if self.pending:
                            fail("Incomplete signed IQ8 pair at end of input")
                        return -1
            count = min(len(outputs[0]), len(self.pending) // 2)
            iq = np.frombuffer(self.pending, dtype=np.int8, count=count * 2).reshape((-1, 2))
            # LR2021 IQ8 is signed, unlike the unsigned RTL-SDR research input.
            outputs[0][:count].real = iq[:, 0].astype(np.float32) / 128
            outputs[0][:count].imag = iq[:, 1].astype(np.float32) / 128
            self.pending = self.pending[count * 2:]
            self.processed_pairs += count
            return count

    class TsOutput(gr.sync_block):
        def __init__(self):
            gr.sync_block.__init__(self, "Bounded TS pipe", in_sig=[(np.uint8, 188)], out_sig=None)
            self.packets = self.tei = self.bytes_written = 0
            self.set_max_noutput_items(64)

        def work(self, inputs, outputs):
            if stop.is_set():
                return -1
            rows = inputs[0]
            if np.any(rows[:, 0] != 0x47):
                fail("Native decoder emitted an invalid TS sync byte")
                return -1
            self.tei += int(np.count_nonzero(rows[:, 1] & 0x80))
            view = memoryview(rows.tobytes())
            deadline = time.monotonic() + .25
            while view and not stop.is_set():
                if time.monotonic() >= deadline:
                    fail("TS consumer remained blocked for 250 ms")
                    return -1
                if not select.select([], [output], [], .025)[1]:
                    continue
                try:
                    count = os.write(output, view)
                    if count <= 0:
                        fail("TS consumer accepted zero bytes")
                        return -1
                    self.bytes_written += count
                    view = view[count:]
                except BlockingIOError:
                    continue
                except OSError as error:
                    fail("TS output: " + str(error))
                    return -1
            if stop.is_set():
                return -1
            self.packets += len(rows)
            return len(rows)

    class TmccMonitor(gr.basic_block):
        def __init__(self):
            gr.basic_block.__init__(self, "Validated TMCC parameters", in_sig=None, out_sig=None)
            self.candidate = None
            self.confirmations = 0
            self.last_rejection = None
            self.message_port_register_in(pmt.intern("in"))
            self.set_msg_handler(pmt.intern("in"), self.accept)

        def accept(self, message):
            try:
                schema = pmt.to_long(pmt.dict_ref(message, pmt.intern("schema"), pmt.PMT_NIL))
                if schema != 1:
                    raise ValueError("Unsupported native TMCC message schema")
                mode = pmt.to_long(pmt.dict_ref(message, pmt.intern("mode"), pmt.PMT_NIL))
                bits = pmt.u8vector_elements(pmt.dict_ref(message, pmt.intern("bits"), pmt.PMT_NIL))
                parameters = tmcc_parameters(bits, mode, configuration["guard_denominator"])
                candidate = tuple(sorted(parameters.items()))
                self.confirmations = self.confirmations + 1 if candidate == self.candidate else 1
                self.candidate = candidate
                self.last_rejection = None
                if self.confirmations == 2:
                    emit("tmcc_parameters", parity_validated=True, confirmation_frames=2, **parameters)
            except (ValueError, TypeError, RuntimeError) as error:
                self.candidate, self.confirmations = None, 0
                if str(error) != self.last_rejection:
                    emit("tmcc_rejected", message=str(error))
                    self.last_rejection = str(error)

    source, sink = IqInput(), TsOutput()
    graph = None
    done = threading.Event()

    def ready():
        if arguments.ready_fd is not None:
            # Ready means able to accept IQ, not TMCC lock or programme discovery.
            descriptor = arguments.ready_fd
            arguments.ready_fd = None
            try:
                if os.write(descriptor, b"R") != 1:
                    raise OSError("Ready handshake was not accepted")
            finally:
                os.close(descriptor)

    def wait_graph():
        try:
            graph.wait()
        except BaseException as error:
            fail("Native decoder: " + str(error))
        finally:
            done.set()

    old_handlers = {value: signal.signal(value, lambda *_: stop.set())
                    for value in (signal.SIGINT, signal.SIGTERM)}
    started = time.monotonic()
    graph_started = False
    duration_timer = threading.Timer(arguments.seconds, stop.set) if arguments.seconds else None
    if duration_timer is not None:
        duration_timer.daemon = True
        duration_timer.start()
    try:
        if automatic:
            source.reader.start()
            ready()
            emit("acquiring", profile=AUTO_PROFILE, input_rate=576000)
            prefix = bytearray()
            acquisition_deadline = started + 3
            while len(prefix) < 1152000 and not stop.is_set():
                now = time.monotonic()
                if arguments.seconds and now - started >= arguments.seconds:
                    stop.set()
                    break
                if now >= acquisition_deadline:
                    fail("IQ acquisition prefix did not arrive within three seconds")
                    break
                try:
                    data = source.queue.get(timeout=.025)
                    source.replay.append(data)
                    prefix.extend(data)
                except queue.Empty:
                    if source.eof.is_set():
                        break
            if not stop.is_set():
                if not prefix:
                    fail("No IQ samples for automatic acquisition")
                elif len(prefix) % 2 and source.eof.is_set():
                    fail("Incomplete signed IQ8 pair at end of acquisition input")
                else:
                    # A split pair is retained in replay and completed by live IQ.
                    emit("acquisition_probe", iq_bytes=len(prefix))
                    native = resample_acquisition_iq(prefix[:len(prefix) // 2 * 2], stop)
                    acquired = None if native is None else acquire_tmcc_parameters(native, cancel=stop)
                    if acquired is None and not stop.is_set():
                        fail("No validated 1seg parameters in acquisition prefix", 4)
                    elif acquired is not None:
                        configuration = acquired
                        emit("parameters_acquired", profile=AUTO_PROFILE, **configuration)
        if not stop.is_set():
            graph = gr.top_block("Trail Mate parameter-configured 1seg decoder")
            chain, tmcc = build_decoder_chain(source, sink, configuration)
            graph.connect(*chain)
            monitor = TmccMonitor() if tmcc_port else None
            if monitor is not None:
                graph.msg_connect(tmcc, "tmcc_parameters", monitor, "in")
            graph.start()
            graph_started = True
            threading.Thread(target=wait_graph, name="oneseg-native-wait", daemon=True).start()
            if source.reader.ident is None:
                source.reader.start()
            ready()
            emit("started", profile=arguments.profile, input_rate=576000, parameter_autodetection=automatic)
        next_report = started
        while graph_started and not done.wait(.05) and not stop.is_set():
            now = time.monotonic()
            if arguments.seconds and now - started >= arguments.seconds:
                stop.set()
            if now >= next_report:
                emit("metrics", iq_bytes=source.read_bytes, iq_pairs=source.processed_pairs,
                     queue_high_water=source.high_water, ts_packets=sink.packets, tei_packets=sink.tei)
                next_report = now + 1
    finally:
        stop.set()
        if duration_timer is not None:
            duration_timer.cancel()
        if graph_started:
            graph.stop()
        else:
            done.set()
        if source.reader.ident is not None:
            source.reader.join(timeout=1)
        if graph_started and not done.wait(5):
            emit("error", message="Native decoder did not stop within 5 seconds")
            # A decoder is an isolated child. Do not keep its radio owner waiting.
            os._exit(3)
        for value, handler in old_handlers.items():
            signal.signal(value, handler)
        if arguments.ready_fd is not None:
            os.close(arguments.ready_fd)
            arguments.ready_fd = None
        os.close(output)
    emit("stopped", iq_bytes=source.read_bytes, iq_pairs=source.processed_pairs,
         ts_packets=sink.packets, ts_bytes=sink.bytes_written, tei_packets=sink.tei,
         errors=failures, elapsed_seconds=time.monotonic() - started)
    return failure_exit if failures else 0


def activate_packaged_backend():
    """Restart once so the dynamic loader sees the installed private library path."""
    backend = Path(__file__).resolve().parent / "oneseg-backend"
    if not backend.is_dir():
        return
    identity = str(backend)
    if os.environ.get("TRAIL_MATE_ONESEG_BACKEND_ACTIVE") == identity:
        return
    environment = os.environ.copy()
    for key, directory in (("PYTHONPATH", "python"), ("LD_LIBRARY_PATH", "lib")):
        path = str(backend / directory)
        previous = environment.get(key)
        environment[key] = path + (os.pathsep + previous if previous else "")
    environment["TRAIL_MATE_ONESEG_BACKEND_ACTIVE"] = identity
    os.execve(sys.executable, [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]], environment)


def main():
    activate_packaged_backend()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, choices=[PROFILE, AUTO_PROFILE])
    parser.add_argument("--check-backend", action="store_true")
    parser.add_argument("--ready-fd", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--seconds", type=float, default=0, help="Zero runs until EOF or interrupted")
    arguments = parser.parse_args()
    if not 0 <= arguments.seconds <= 3600:
        parser.error("--seconds must be finite and between zero and 3600")
    if arguments.ready_fd is not None and arguments.ready_fd < 3:
        parser.error("--ready-fd must be a private descriptor")
    try:
        return run(arguments)
    except Exception as error:
        emit("error", message=str(error))
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
