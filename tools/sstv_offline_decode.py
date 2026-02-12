import math
import struct
from array import array
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


SAMPLE_RATE = 44100
BITS_PER_SAMPLE = 16
CHANNELS = 1

K_IN_WIDTH = 320
K_IN_HEIGHT_SCOTTIE = 256
K_OUT_WIDTH = 288
K_OUT_HEIGHT = 192
K_OUT_IMAGE_WIDTH = 240
K_PAD_X = (K_OUT_WIDTH - K_OUT_IMAGE_WIDTH) // 2

K_PANEL_BG = 0xFAF0D8

K_PORCH_MS = 1.5
K_SYNC_PULSE_MS = 9.0
K_COLOR_MS_SCOTTIE1 = 138.24

K_MIN_SYNC_GAP_MS = 420.0
K_SYNC_WINDOW_SAMPLES = 400
K_SYNC_HOP_SAMPLES = 80
K_SYNC_TONE_DETECT_RATIO = 1.6
K_SYNC_TONE_TOTAL_RATIO = 0.55

K_FREQ_MIN = 1500.0
K_FREQ_MAX = 2300.0
K_FREQ_SPAN = K_FREQ_MAX - K_FREQ_MIN
K_PIXEL_BIN_STEP = 25.0
K_PIXEL_BIN_COUNT = int((K_FREQ_MAX - K_FREQ_MIN) / K_PIXEL_BIN_STEP) + 1
K_MAX_PIXEL_SAMPLES = 64
PIXEL_WINDOW_SCALE = 3.0
K_SYNC_WINDOW_PCT = 0.12
K_SYNC_PHASE_OFFSET = K_SYNC_WINDOW_SAMPLES
WINDOWED_SYNC = False
K_SYNC_SCORE_WINDOW = 64
K_SYNC_SCORE_HOP = 13
K_SYNC_SCORE_RATIO = 1.6
K_SYNC_PHASE_BINS = 512
K_SYNC_PHASE_MIN_HITS = 64
SMOOTH_SYNC = True
USE_SLOWRX = True
MIN_SLANT = 30
MAX_SLANT = 150
SYNC_HOP_SAMPLES = 13
WINDOW_LENGTHS = [48, 64, 96, 128, 256, 512, 1024]


class SyncLineTracker:
    def __init__(self, expected_samples, window_pct=0.12, max_fit=24):
        self.expected_samples = expected_samples
        self.window_pct = window_pct
        self.max_fit = max_fit
        self.count = 0
        self.fit_count = 0
        self.sum_x = 0.0
        self.sum_y = 0.0
        self.sum_xx = 0.0
        self.sum_xy = 0.0
        self.slope = 0.0
        self.intercept = 0.0
        self.fit_ready = False
        self.last_sample = -1
        self.miss = 0

    def reset(self):
        self.count = 0
        self.fit_count = 0
        self.sum_x = 0.0
        self.sum_y = 0.0
        self.sum_xx = 0.0
        self.sum_xy = 0.0
        self.slope = 0.0
        self.intercept = 0.0
        self.fit_ready = False
        self.last_sample = -1
        self.miss = 0

    def accept(self, sample_index):
        if self.expected_samples <= 0:
            self.last_sample = sample_index
            self.count += 1
            return True
        min_window = int(self.expected_samples * (1.0 - self.window_pct))
        max_window = int(self.expected_samples * (1.0 + self.window_pct))
        if self.count == 0 or self.last_sample < 0:
            self.last_sample = sample_index
            self.fit_count += 1
            self.count = 1
            return True
        delta = sample_index - self.last_sample
        if not self.fit_ready:
            if delta < min_window:
                self.miss += 1
                return False
        else:
            if delta < min_window or delta > max_window:
                self.miss += 1
                return False
        x = float(self.count)
        y = float(sample_index)
        if not self.fit_ready:
            self.sum_x += x
            self.sum_y += y
            self.sum_xx += x * x
            self.sum_xy += x * y
            self.fit_count += 1
            if self.fit_count >= self.max_fit:
                denom = self.fit_count * self.sum_xx - self.sum_x * self.sum_x
                if denom != 0.0:
                    self.slope = (self.fit_count * self.sum_xy - self.sum_x * self.sum_y) / denom
                    self.intercept = (self.sum_y - self.slope * self.sum_x) / self.fit_count
                    min_slope = self.expected_samples * (1.0 - self.window_pct)
                    max_slope = self.expected_samples * (1.0 + self.window_pct)
                    if self.slope < min_slope:
                        self.slope = min_slope
                    if self.slope > max_slope:
                        self.slope = max_slope
                    self.fit_ready = True
        else:
            pred = self.slope * x + self.intercept
            err = y - pred
            window = self.expected_samples * self.window_pct
            if window > 0.0 and (err < -window or err > window):
                self.miss += 1
                if self.miss > 3:
                    self.reset()
                return False
            alpha = 0.02
            self.slope = self.slope * (1.0 - alpha) + float(delta) * alpha
            self.intercept = y - self.slope * x
            self.miss = 0
        self.last_sample = sample_index
        self.count += 1
        return True
K_HEADER_WINDOW_SAMPLES = 512
K_HEADER_HOP_SAMPLES = 256
K_LEADER_MS = 300.0
K_BREAK_MS = 10.0
K_VIS_BIT_MS = 30.0
K_VIS_BITS = 8
K_HEADER_TONE_DETECT_RATIO = 1.3
K_HEADER_TONE_TOTAL_RATIO = 0.45


class GoertzelBin:
    __slots__ = ("freq", "cos_w", "sin_w", "coeff")

    def __init__(self, freq: float):
        self.freq = freq
        w = 2.0 * math.pi * freq / SAMPLE_RATE
        self.cos_w = math.cos(w)
        self.sin_w = math.sin(w)
        self.coeff = 2.0 * self.cos_w


def goertzel_power(data, bin_obj: GoertzelBin):
    q0 = 0.0
    q1 = 0.0
    q2 = 0.0
    coeff = bin_obj.coeff
    for x in data:
        q0 = coeff * q1 - q2 + float(x)
        q2 = q1
        q1 = q0
    real = q1 - q2 * bin_obj.cos_w
    imag = q2 * bin_obj.sin_w
    return real * real + imag * imag


def hann_window(length):
    if length <= 1:
        return [1.0] * max(1, length)
    return [0.5 * (1.0 - math.cos(2.0 * math.pi * i / (length - 1)))
            for i in range(length)]


def build_bins_for_window(length, fmin=K_FREQ_MIN, fmax=K_FREQ_MAX):
    kmin = int(math.ceil(fmin * length / SAMPLE_RATE))
    kmax = int(math.floor(fmax * length / SAMPLE_RATE))
    bins = []
    indices = []
    for k in range(kmin, kmax + 1):
        freq = k * SAMPLE_RATE / length
        bins.append(GoertzelBin(freq))
        indices.append(k)
    return bins, indices


WINDOW_CACHE = {}


def get_window_cache(length):
    cached = WINDOW_CACHE.get(length)
    if cached:
        return cached
    hann = hann_window(length)
    bins, indices = build_bins_for_window(length)
    WINDOW_CACHE[length] = (hann, bins, indices)
    return WINDOW_CACHE[length]


def estimate_freq_bins(window, hann, bins, indices, length):
    weighted = [float(x) * hann[i] for i, x in enumerate(window)]
    powers = []
    max_idx = 0
    max_val = 0.0
    for i, b in enumerate(bins):
        val = goertzel_power(weighted, b)
        powers.append(val)
        if val > max_val:
            max_val = val
            max_idx = i
    if max_idx <= 0 or max_idx >= len(powers) - 1:
        peak_bin = indices[max_idx]
    else:
        p0 = powers[max_idx - 1]
        p1 = powers[max_idx]
        p2 = powers[max_idx + 1]
        peak_bin = indices[max_idx]
        if p0 > 0.0 and p1 > 0.0 and p2 > 0.0:
            denom = 2.0 * math.log((p1 * p1) / (p0 * p2))
            if denom != 0.0:
                peak_bin = indices[max_idx] + math.log(p2 / p0) / denom
    return peak_bin * SAMPLE_RATE / length


def estimate_snr(samples, center_idx, hann, bins_video, bins_noise, length):
    half = length // 2
    if center_idx < half or center_idx + half >= len(samples):
        return None
    window = samples[center_idx - half:center_idx - half + length]
    weighted = [float(x) * hann[i] for i, x in enumerate(window)]
    p_video = 0.0
    for b in bins_video:
        p_video += goertzel_power(weighted, b)
    p_noise = 0.0
    for b in bins_noise:
        p_noise += goertzel_power(weighted, b)
    video_bins = len(bins_video)
    noise_bins = len(bins_noise)
    if noise_bins == 0 or video_bins == 0:
        return None
    receiver_bins = video_bins + noise_bins
    p_noise_est = p_noise * (receiver_bins / noise_bins)
    p_signal = p_video - p_noise * (video_bins / noise_bins)
    if p_noise_est <= 0.0:
        return 0.0
    ratio = p_signal / p_noise_est
    if ratio < 0.01:
        ratio = 0.01
    return 10.0 * math.log10(ratio)


def select_window_index(snr_db):
    if snr_db is None:
        return 3
    if snr_db >= 20.0:
        return 0
    if snr_db >= 10.0:
        return 1
    if snr_db >= 9.0:
        return 2
    if snr_db >= 3.0:
        return 3
    if snr_db >= -5.0:
        return 4
    if snr_db >= -10.0:
        return 5
    return 6

def estimate_freq(window, bins):
    max_val = 0.0
    max_idx = 0
    mags = []
    for i, b in enumerate(bins):
        val = goertzel_power(window, b)
        mags.append(val)
        if val > max_val:
            max_val = val
            max_idx = i
    left = max_idx - 1 if max_idx > 0 else max_idx
    right = max_idx + 1 if max_idx + 1 < len(bins) else max_idx
    y1 = mags[left]
    y2 = mags[max_idx]
    y3 = mags[right]
    denom = y1 + y2 + y3
    peak = float(max_idx)
    if denom > 0.0:
        peak += (y3 - y1) / denom
    freq = K_FREQ_MIN + peak * K_PIXEL_BIN_STEP
    if freq < K_FREQ_MIN:
        freq = K_FREQ_MIN
    if freq > K_FREQ_MAX:
        freq = K_FREQ_MAX
    return freq


def freq_to_intensity(freq: float) -> int:
    if freq < K_FREQ_MIN:
        freq = K_FREQ_MIN
    if freq > K_FREQ_MAX:
        freq = K_FREQ_MAX
    ratio = (freq - K_FREQ_MIN) / K_FREQ_SPAN
    value = int(ratio * 255.0 + 0.5)
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


class ModeSpec:
    def __init__(self, name, sync_time, porch_time, septr_time, pixel_time,
                 line_time, img_width, num_lines, color_enc):
        self.name = name
        self.sync_time = sync_time
        self.porch_time = porch_time
        self.septr_time = septr_time
        self.pixel_time = pixel_time
        self.line_time = line_time
        self.img_width = img_width
        self.num_lines = num_lines
        self.color_enc = color_enc


MODE_S1 = ModeSpec(
    name="Scottie S1",
    sync_time=9e-3,
    porch_time=1.5e-3,
    septr_time=1.5e-3,
    pixel_time=0.4320e-3,
    line_time=428.38e-3,
    img_width=320,
    num_lines=256,
    color_enc="GBR",
)

def calc_pixel_window_samples(color_samples_in: int) -> int:
    samples = int((color_samples_in / K_IN_WIDTH) * PIXEL_WINDOW_SCALE)
    if samples < 8:
        samples = 8
    if samples > K_MAX_PIXEL_SAMPLES:
        samples = K_MAX_PIXEL_SAMPLES
    return samples


def detect_header_tone(p1100, p1200, p1300, p1900):
    total = p1100 + p1200 + p1300 + p1900
    max_val = p1100
    tone = 1100
    if p1200 > max_val:
        max_val = p1200
        tone = 1200
    if p1300 > max_val:
        max_val = p1300
        tone = 1300
    if p1900 > max_val:
        max_val = p1900
        tone = 1900
    other_max = 0.0
    if tone != 1100 and p1100 > other_max:
        other_max = p1100
    if tone != 1200 and p1200 > other_max:
        other_max = p1200
    if tone != 1300 and p1300 > other_max:
        other_max = p1300
    if tone != 1900 and p1900 > other_max:
        other_max = p1900
    if max_val > other_max * K_HEADER_TONE_DETECT_RATIO and max_val > total * K_HEADER_TONE_TOTAL_RATIO:
        return tone
    return None


class HeaderDetector:
    def __init__(self):
        self.bin_1100 = GoertzelBin(1100.0)
        self.bin_1200 = GoertzelBin(1200.0)
        self.bin_1300 = GoertzelBin(1300.0)
        self.bin_1900 = GoertzelBin(1900.0)
        self.buf = [0] * K_HEADER_WINDOW_SAMPLES
        self.window = [0] * K_HEADER_WINDOW_SAMPLES
        self.pos = 0
        self.fill = 0
        self.hop = 0
        self.state = "Leader1"
        self.count = 0
        header_window_ms = 1000.0 * K_HEADER_HOP_SAMPLES / SAMPLE_RATE
        self.leader_windows = max(1, int(K_LEADER_MS / header_window_ms + 0.5))
        self.break_windows = max(1, int(K_BREAK_MS / header_window_ms + 0.5))
        self.vis_start_windows = max(1, int(K_VIS_BIT_MS / header_window_ms + 0.5))
        self.header_end = None

    def push(self, sample, sample_index):
        if self.header_end is not None:
            return self.header_end
        self.buf[self.pos] = sample
        self.pos += 1
        if self.pos >= K_HEADER_WINDOW_SAMPLES:
            self.pos = 0
        if self.fill < K_HEADER_WINDOW_SAMPLES:
            self.fill += 1
        if self.fill < K_HEADER_WINDOW_SAMPLES:
            return None
        self.hop += 1
        if self.hop < K_HEADER_HOP_SAMPLES:
            return None
        self.hop = 0
        for j in range(K_HEADER_WINDOW_SAMPLES):
            idx = self.pos + j
            if idx >= K_HEADER_WINDOW_SAMPLES:
                idx -= K_HEADER_WINDOW_SAMPLES
            self.window[j] = self.buf[idx]
        p1100 = goertzel_power(self.window, self.bin_1100)
        p1200 = goertzel_power(self.window, self.bin_1200)
        p1300 = goertzel_power(self.window, self.bin_1300)
        p1900 = goertzel_power(self.window, self.bin_1900)
        tone = detect_header_tone(p1100, p1200, p1300, p1900)
        if self.state == "Leader1":
            if tone == 1900:
                self.count += 1
                if self.count >= self.leader_windows:
                    self.state = "Break"
                    self.count = 0
            else:
                self.count = 0
        elif self.state == "Break":
            if tone == 1200:
                self.count += 1
                if self.count >= self.break_windows:
                    self.state = "Leader2"
                    self.count = 0
            else:
                self.count = 0
        elif self.state == "Leader2":
            if tone == 1900:
                self.count += 1
                if self.count >= self.leader_windows:
                    self.state = "VisStart"
                    self.count = 0
            else:
                self.count = 0
        elif self.state == "VisStart":
            if tone == 1200:
                self.count += 1
                if self.count >= self.vis_start_windows:
                    vis_start_sample = sample_index - K_HEADER_WINDOW_SAMPLES
                    if vis_start_sample < 0:
                        vis_start_sample = 0
                    self.header_end = vis_start_sample + int(
                        SAMPLE_RATE * (K_VIS_BIT_MS / 1000.0) * (K_VIS_BITS + 2)
                    )
                    return self.header_end
            else:
                self.count = 0
        return None


class SyncDetector:
    def __init__(self):
        self.bin_1100 = GoertzelBin(1100.0)
        self.bin_1200 = GoertzelBin(1200.0)
        self.bin_1150 = GoertzelBin(1150.0)
        self.bin_1175 = GoertzelBin(1175.0)
        self.bin_1225 = GoertzelBin(1225.0)
        self.bin_1250 = GoertzelBin(1250.0)
        self.bin_1275 = GoertzelBin(1275.0)
        self.bin_1300 = GoertzelBin(1300.0)
        self.sync_bins = [GoertzelBin(1100.0 + i * 25.0) for i in range(9)]
        self.sync_buf = [0] * K_SYNC_WINDOW_SAMPLES
        self.sync_window = [0] * K_SYNC_WINDOW_SAMPLES
        self.sync_pos = 0
        self.sync_fill = 0
        self.sync_hop = 0
        self.video_bins = [
            GoertzelBin(1500.0),
            GoertzelBin(1700.0),
            GoertzelBin(1900.0),
            GoertzelBin(2100.0),
            GoertzelBin(2300.0),
        ]

    def push(self, sample, can_sync, sample_index, min_sync_gap, last_sync_index,
             expected_samples=0, window_pct=0.0):
        self.sync_buf[self.sync_pos] = sample
        self.sync_pos += 1
        if self.sync_pos >= K_SYNC_WINDOW_SAMPLES:
            self.sync_pos = 0
        if self.sync_fill < K_SYNC_WINDOW_SAMPLES:
            self.sync_fill += 1

        if not can_sync or self.sync_fill < K_SYNC_WINDOW_SAMPLES:
            return False, last_sync_index

        self.sync_hop += 1
        if self.sync_hop < K_SYNC_HOP_SAMPLES:
            return False, last_sync_index
        self.sync_hop = 0

        for j in range(K_SYNC_WINDOW_SAMPLES):
            idx = self.sync_pos + j
            if idx >= K_SYNC_WINDOW_SAMPLES:
                idx -= K_SYNC_WINDOW_SAMPLES
            self.sync_window[j] = self.sync_buf[idx]

        p1100 = goertzel_power(self.sync_window, self.bin_1100)
        p1200 = goertzel_power(self.sync_window, self.bin_1200)
        p1150 = goertzel_power(self.sync_window, self.bin_1150)
        p1175 = goertzel_power(self.sync_window, self.bin_1175)
        p1225 = goertzel_power(self.sync_window, self.bin_1225)
        p1250 = goertzel_power(self.sync_window, self.bin_1250)
        p1275 = goertzel_power(self.sync_window, self.bin_1275)
        p1200 = max(p1200, p1150, p1175, p1225, p1250, p1275)
        p1300 = goertzel_power(self.sync_window, self.bin_1300)
        total = p1100 + p1200 + p1300
        other_max = p1100 if p1100 > p1300 else p1300
        pvideo = 0.0
        for b in self.video_bins:
            pvideo += goertzel_power(self.sync_window, b)
        pvideo /= len(self.video_bins)
        if pvideo < 1e-9:
            pvideo = 1e-9
        ratio = p1200 / pvideo
        score_hit = ratio > K_SYNC_SCORE_RATIO
        sync_hit = score_hit and (p1200 > other_max * K_SYNC_TONE_DETECT_RATIO and
                                  p1200 > total * K_SYNC_TONE_TOTAL_RATIO)
        if sync_hit and sample_index - last_sync_index > min_sync_gap:
            if expected_samples > 0 and last_sync_index >= 0 and window_pct > 0.0:
                delta = sample_index - last_sync_index
                min_window = int(expected_samples * max(0.0, 1.0 - window_pct))
                max_window = int(expected_samples * (1.0 + window_pct))
                if delta < min_window:
                    return False, last_sync_index
                if max_window > 0 and delta > max_window:
                    return False, last_sync_index
            return True, sample_index
        return False, last_sync_index

    def estimate_sync_freq(self):
        return estimate_freq(self.sync_window, self.sync_bins)

    def estimate_offset(self):
        seg = 20
        segments = K_SYNC_WINDOW_SAMPLES // seg
        if segments < 8:
            return 0
        energies = []
        for i in range(segments):
            start = i * seg
            window = self.sync_window[start:start + seg]
            energies.append(goertzel_power(window, self.bin_1200))
        max_conv = 0.0
        max_idx = 0
        for i in range(segments - 7):
            conv = sum(energies[i:i + 4]) - sum(energies[i + 4:i + 8])
            if conv > max_conv:
                max_conv = conv
                max_idx = i + 4
        fall_sample = max_idx * seg
        if fall_sample < 0:
            fall_sample = 0
        if fall_sample >= K_SYNC_WINDOW_SAMPLES:
            fall_sample = K_SYNC_WINDOW_SAMPLES - 1
        return (K_SYNC_WINDOW_SAMPLES - 1) - fall_sample

    def score_at(self, samples, end_idx):
        if end_idx < K_SYNC_SCORE_WINDOW:
            return None
        start = end_idx - K_SYNC_SCORE_WINDOW
        window = samples[start:end_idx]
        p1200 = goertzel_power(window, self.bin_1200)
        p1150 = goertzel_power(window, self.bin_1150)
        p1175 = goertzel_power(window, self.bin_1175)
        p1225 = goertzel_power(window, self.bin_1225)
        p1250 = goertzel_power(window, self.bin_1250)
        p1275 = goertzel_power(window, self.bin_1275)
        p1200 = max(p1200, p1150, p1175, p1225, p1250, p1275)
        pvideo = 0.0
        for b in self.video_bins:
            pvideo += goertzel_power(window, b)
        pvideo /= len(self.video_bins)
        score = p1200 / (pvideo + 1e-9)
        valid = score > K_SYNC_SCORE_RATIO
        return score, valid


def build_has_sync(samples, rate, freq_shift=0.0):
    win = 64
    hop = SYNC_HOP_SAMPLES
    if len(samples) < win:
        return []
    hann = hann_window(win)
    has_sync = []
    bin_width = rate / win
    k_sync = int(round((1200.0 + freq_shift) / bin_width))
    kmin = int(math.ceil((1500.0 + freq_shift) / bin_width))
    kmax = int(math.floor((2300.0 + freq_shift) / bin_width))
    sync_bins = []
    for k in range(k_sync - 1, k_sync + 2):
        if k >= 0:
            sync_bins.append((k, 1.0 - 0.5 * abs(k - k_sync)))
    video_bins = [k for k in range(kmin, kmax + 1) if k >= 0]
    sync_goertzels = {k: GoertzelBin(k * bin_width) for k, _ in sync_bins}
    video_goertzels = {k: GoertzelBin(k * bin_width) for k in video_bins}
    for start in range(0, len(samples) - win, hop):
        window = samples[start:start + win]
        weighted = [float(x) * hann[i] for i, x in enumerate(window)]
        p_sync = 0.0
        for k, w in sync_bins:
            p_sync += goertzel_power(weighted, sync_goertzels[k]) * w
        p_raw = 0.0
        for k in video_bins:
            p_raw += goertzel_power(weighted, video_goertzels[k])
        if video_bins:
            p_raw /= len(video_bins)
        if p_raw < 1e-9:
            p_raw = 1e-9
        has_sync.append(p_sync > 2.0 * p_raw)
    return has_sync


def estimate_header_shift(samples):
    win = 1024
    hop = 256
    hann, bins, indices = get_window_cache(win)
    peaks = []
    max_samples = min(len(samples), int(SAMPLE_RATE * 2.0))
    for start in range(0, max_samples - win, hop):
        window = samples[start:start + win]
        freq = estimate_freq_bins(window, hann, bins, indices, win)
        if 1700.0 <= freq <= 2100.0:
            peaks.append(freq)
            if len(peaks) >= 20:
                break
    if len(peaks) < 5:
        return 0.0
    avg = sum(peaks) / len(peaks)
    return avg - 1900.0


def estimate_sync_offset(samples, has_sync):
    bins = [GoertzelBin(1100.0 + i * 25.0) for i in range(9)]
    win = K_SYNC_SCORE_WINDOW
    hop = SYNC_HOP_SAMPLES
    half = win // 2
    total = 0.0
    count = 0
    for i, hit in enumerate(has_sync):
        if not hit:
            continue
        sample_idx = i * hop + half
        if sample_idx < half or sample_idx + half >= len(samples):
            continue
        window = samples[sample_idx - half:sample_idx - half + win]
        freq = estimate_freq(window, bins)
        total += freq
        count += 1
        if count >= 200:
            break
    if count == 0:
        return 0.0
    return total / count - 1200.0


def find_sync_slowrx(mode, rate, has_sync):
    if not has_sync:
        return rate, 0
    line_width = int(mode.line_time / mode.sync_time * 4.0 + 0.5)
    if line_width <= 0:
        return rate, 0

    retries = 0
    while True:
        sync_img = [[False] * mode.num_lines for _ in range(line_width)]
        for y in range(mode.num_lines):
            for x in range(line_width):
                t = (y + (x / float(line_width))) * mode.line_time
                idx = int(t * rate / SYNC_HOP_SAMPLES)
                if 0 <= idx < len(has_sync) and has_sync[idx]:
                    sync_img[x][y] = True

        lines = [[0] * ((MAX_SLANT - MIN_SLANT) * 2) for _ in range(line_width + 1)]
        d_most = 0
        q_most = 0
        for cy in range(mode.num_lines):
            for cx in range(line_width):
                if not sync_img[cx][cy]:
                    continue
                for q in range(MIN_SLANT * 2, MAX_SLANT * 2):
                    angle = math.radians(q / 2.0)
                    d = int(round(line_width + (-cx * math.sin(angle) + cy * math.cos(angle))))
                    if 0 < d < line_width:
                        idx = q - MIN_SLANT * 2
                        lines[d][idx] += 1
                        if lines[d][idx] > lines[d_most][q_most - MIN_SLANT * 2]:
                            d_most = d
                            q_most = q

        if q_most == 0:
            break

        slant_angle = q_most / 2.0
        if 89.0 < slant_angle < 91.0:
            break
        if retries >= 3:
            rate = SAMPLE_RATE
            break
        rate += math.tan(math.radians(90.0 - slant_angle)) / line_width * rate
        retries += 1

    x_acc = [0] * 700
    for y in range(mode.num_lines):
        for x in range(700):
            t = y * mode.line_time + x / 700.0 * mode.line_time
            idx = int(t * rate / SYNC_HOP_SAMPLES)
            if 0 <= idx < len(has_sync) and has_sync[idx]:
                x_acc[x] += 1

    max_conv = None
    xmax = 0
    for x in range(700 - 8):
        conv = sum(x_acc[x:x + 4]) - sum(x_acc[x + 4:x + 8])
        if max_conv is None or conv > max_conv:
            max_conv = conv
            xmax = x + 4
    if xmax > 350:
        xmax -= 350

    s = xmax / 700.0 * mode.line_time - mode.sync_time
    if mode.name.startswith("Scottie"):
        s = s - mode.pixel_time * mode.img_width / 2.0 + mode.porch_time * 2.0
    skip = int(round(s * rate))
    return rate, skip


def demod_stored_lum(samples, freq_shift, mode_name):
    stored = [0] * len(samples)
    last_val = 0
    win_idx = 3
    next_snr = 0

    hann1024 = hann_window(1024)
    bins_video_1024, _ = build_bins_for_window(1024, 1500.0 + freq_shift, 2300.0 + freq_shift)
    bins_noise_1024 = []
    bins_noise_1024.extend(build_bins_for_window(1024, 400.0 + freq_shift, 800.0 + freq_shift)[0])
    bins_noise_1024.extend(build_bins_for_window(1024, 2700.0 + freq_shift, 3400.0 + freq_shift)[0])

    for i in range(0, len(samples), 6):
        if i >= next_snr:
            snr_db = estimate_snr(samples, i, hann1024,
                                  bins_video_1024, bins_noise_1024, 1024)
            win_idx = select_window_index(snr_db)
            if "DX" in mode_name and win_idx < 6:
                win_idx += 1
            next_snr += 256

        win_len = WINDOW_LENGTHS[win_idx]
        hann, bins, indices = get_window_cache(win_len)
        half = win_len // 2
        if i < half or i + half >= len(samples):
            val = last_val
        else:
            window = samples[i - half:i - half + win_len]
            freq = estimate_freq_bins(window, hann, bins, indices, win_len)
            if freq < 1500.0 + freq_shift:
                freq = 1500.0 + freq_shift
            if freq > 2300.0 + freq_shift:
                freq = 2300.0 + freq_shift
            freq -= freq_shift
            val = freq_to_intensity(freq)
            last_val = val
        end = i + 6
        if end > len(samples):
            end = len(samples)
        for j in range(i, end):
            stored[j] = val

    return stored


def decode_mode_pixelgrid(samples, mode, rate, skip, stored_lum):
    chan_len = mode.pixel_time * mode.img_width
    chan_start = [0.0, 0.0, 0.0]
    if mode.name.startswith("Scottie"):
        chan_start[0] = mode.septr_time
        chan_start[1] = chan_start[0] + chan_len + mode.septr_time
        chan_start[2] = chan_start[1] + chan_len + mode.sync_time + mode.porch_time
    else:
        chan_start[0] = mode.sync_time + mode.porch_time
        chan_start[1] = chan_start[0] + chan_len + mode.septr_time
        chan_start[2] = chan_start[1] + chan_len + mode.septr_time

    if stored_lum is None or len(stored_lum) != len(samples):
        stored_lum = [0] * len(samples)

    rgb = [[(0, 0, 0) for _ in range(mode.img_width)] for _ in range(mode.num_lines)]
    for y in range(mode.num_lines):
        for chan in range(3):
            for x in range(mode.img_width):
                t = y * mode.line_time + chan_start[chan] + (x - 0.5) / mode.img_width * chan_len
                sample_idx = int(round(rate * t)) + skip
                if sample_idx < 0 or sample_idx >= len(samples):
                    continue
                val = stored_lum[sample_idx]
                r, g, b = rgb[y][x]
                if mode.color_enc == "GBR":
                    if chan == 0:
                        g = val
                    elif chan == 1:
                        b = val
                    else:
                        r = val
                else:
                    if chan == 0:
                        r = val
                    elif chan == 1:
                        g = val
                    else:
                        b = val
                rgb[y][x] = (r, g, b)

    base = Image.new("RGB", (mode.img_width, mode.num_lines))
    pix = base.load()
    for y in range(mode.num_lines):
        for x in range(mode.img_width):
            pix[x, y] = rgb[y][x]

    out = Image.new("RGB", (K_OUT_WIDTH, K_OUT_HEIGHT), panel_rgb())
    scaled = base.resize((K_OUT_IMAGE_WIDTH, K_OUT_HEIGHT), Image.BILINEAR)
    out.paste(scaled, (K_PAD_X, 0))
    return out


class Scottie1Decoder:
    def __init__(self):
        self.line_count = K_IN_HEIGHT_SCOTTIE
        self.line_index = 0
        self.phase = "Idle"
        self.phase_samples = 0
        self.line_samples = 0
        self.frame_done = False

        self.porch_samples = int(SAMPLE_RATE * (K_PORCH_MS / 1000.0))
        self.sync_samples = int(SAMPLE_RATE * (K_SYNC_PULSE_MS / 1000.0))
        self.color_samples = int(SAMPLE_RATE * (K_COLOR_MS_SCOTTIE1 / 1000.0))
        self.pixel_window_samples = calc_pixel_window_samples(self.color_samples)

        self.base_porch_samples = self.porch_samples
        self.base_sync_samples = self.sync_samples
        self.base_color_samples = self.color_samples
        self.timing_scale = 1.0
        self.sync_phase_offset = 0
        self.freq_min = None
        self.freq_max = None
        self.freq_samples = 0

        self.pixel_bins = [GoertzelBin(K_FREQ_MIN + i * K_PIXEL_BIN_STEP)
                           for i in range(K_PIXEL_BIN_COUNT)]
        self.pixel_buf = [0] * self.pixel_window_samples
        self.pixel_window = [0] * self.pixel_window_samples
        self.pixel_pos = 0
        self.pixel_fill = 0
        self.last_pixel = -1

        self.accum = [[0] * K_IN_WIDTH, [0] * K_IN_WIDTH, [0] * K_IN_WIDTH]
        self.count = [[0] * K_IN_WIDTH, [0] * K_IN_WIDTH, [0] * K_IN_WIDTH]

        self.out_img = Image.new("RGB", (K_OUT_WIDTH, K_OUT_HEIGHT), panel_rgb())
        self.last_output_y = -1

    def apply_timing_scale(self):
        scale = self.timing_scale
        self.porch_samples = int(self.base_porch_samples * scale + 0.5)
        self.sync_samples = int(self.base_sync_samples * scale + 0.5)
        self.color_samples = int(self.base_color_samples * scale + 0.5)
        self.pixel_window_samples = calc_pixel_window_samples(self.color_samples)
        if len(self.pixel_buf) != self.pixel_window_samples:
            self.pixel_buf = [0] * self.pixel_window_samples
            self.pixel_window = [0] * self.pixel_window_samples
            self.pixel_pos = 0
            self.pixel_fill = 0
            self.last_pixel = -1

    def clear_accum(self):
        for c in range(3):
            self.accum[c] = [0] * K_IN_WIDTH
            self.count[c] = [0] * K_IN_WIDTH

    def reset_pixel_state(self):
        self.last_pixel = -1
        self.pixel_pos = 0
        self.pixel_fill = 0

    def start_frame(self):
        self.line_index = 0
        self.phase = "Porch1"
        self.phase_samples = 0
        self.line_samples = 0
        self.apply_timing_scale()
        self.clear_accum()
        self.reset_pixel_state()
        self.frame_done = False
        self.last_output_y = -1
        self.out_img = Image.new("RGB", (K_OUT_WIDTH, K_OUT_HEIGHT), panel_rgb())

    def on_sync(self, was_receiving):
        if not was_receiving:
            self.start_frame()
            offset = self.sync_phase_offset
            if offset < self.sync_samples:
                self.phase = "Sync"
                self.phase_samples = offset
            elif offset < self.sync_samples + self.porch_samples:
                self.phase = "Porch3"
                self.phase_samples = offset - self.sync_samples
            else:
                self.phase = "Red"
                self.phase_samples = offset - self.sync_samples - self.porch_samples
                if self.phase_samples < 0:
                    self.phase_samples = 0
                if self.phase_samples > self.color_samples:
                    self.phase_samples = self.color_samples
                self.reset_pixel_state()
            return True

        if self.phase != "Blue":
            return False
        guard = self.color_samples // 4
        if guard < 1:
            guard = 1
        if self.phase_samples < (self.color_samples - guard):
            return False
        if self.line_samples > 0:
            expected = self.base_porch_samples * 3 + self.base_color_samples * 3 + self.base_sync_samples
            adjusted = self.line_samples - self.sync_phase_offset
            if adjusted < 1:
                adjusted = 1
            window = 0.12
            min_samples = int(expected * (1.0 - window))
            max_samples = int(expected * (1.0 + window))
            if min_samples <= adjusted <= max_samples:
                ratio = float(expected) / float(adjusted)
                if ratio < 0.95:
                    ratio = 0.95
                if ratio > 1.05:
                    ratio = 1.05
                self.timing_scale = self.timing_scale * 0.98 + ratio * 0.02
                self.apply_timing_scale()
        self.line_samples = 0

        offset = self.sync_phase_offset
        if offset < self.sync_samples:
            self.phase = "Sync"
            self.phase_samples = offset
        elif offset < self.sync_samples + self.porch_samples:
            self.phase = "Porch3"
            self.phase_samples = offset - self.sync_samples
        else:
            self.phase = "Red"
            self.phase_samples = offset - self.sync_samples - self.porch_samples
            if self.phase_samples < 0:
                self.phase_samples = 0
            if self.phase_samples > self.color_samples:
                self.phase_samples = self.color_samples
            self.reset_pixel_state()
        return False

    def render_line(self):
        out_y = (self.line_index * K_OUT_HEIGHT) // self.line_count
        if out_y == self.last_output_y or out_y < 0 or out_y >= K_OUT_HEIGHT:
            return
        self.last_output_y = out_y
        row = self.out_img.load()
        bg = panel_rgb()
        for x in range(K_OUT_WIDTH):
            row[x, out_y] = bg
        for out_x in range(K_OUT_IMAGE_WIDTH):
            in_x = (out_x * K_IN_WIDTH) // K_OUT_IMAGE_WIDTH
            if in_x < 0:
                in_x = 0
            if in_x >= K_IN_WIDTH:
                in_x = K_IN_WIDTH - 1
            g = self.accum[0][in_x] // self.count[0][in_x] if self.count[0][in_x] else 0
            b = self.accum[1][in_x] // self.count[1][in_x] if self.count[1][in_x] else 0
            r = self.accum[2][in_x] // self.count[2][in_x] if self.count[2][in_x] else 0
            row[K_PAD_X + out_x, out_y] = (r, g, b)

    def step_color(self, mono):
        pixel = (self.phase_samples * K_IN_WIDTH) // self.color_samples
        if 0 <= pixel < K_IN_WIDTH:
            self.pixel_buf[self.pixel_pos] = int(mono)
            self.pixel_pos += 1
            if self.pixel_pos >= self.pixel_window_samples:
                self.pixel_pos = 0
            if self.pixel_fill < self.pixel_window_samples:
                self.pixel_fill += 1

            if pixel != self.last_pixel and self.pixel_fill == self.pixel_window_samples:
                self.last_pixel = pixel
                for j in range(self.pixel_window_samples):
                    idx = self.pixel_pos + j
                    if idx >= self.pixel_window_samples:
                        idx -= self.pixel_window_samples
                    self.pixel_window[j] = self.pixel_buf[idx]
                freq = estimate_freq(self.pixel_window, self.pixel_bins)
                if self.freq_min is None or freq < self.freq_min:
                    self.freq_min = freq
                if self.freq_max is None or freq > self.freq_max:
                    self.freq_max = freq
                self.freq_samples += 1
                intensity = freq_to_intensity(freq)
                if self.phase == "Green":
                    channel = 0
                elif self.phase == "Blue":
                    channel = 1
                else:
                    channel = 2
                self.accum[channel][pixel] += intensity
                self.count[channel][pixel] += 1

        self.phase_samples += 1
        if self.phase_samples >= self.color_samples:
            self.phase_samples = 0
            if self.phase == "Green":
                self.phase = "Porch2"
            elif self.phase == "Blue":
                self.phase = "Sync"
            elif self.phase == "Red":
                self.render_line()
                self.line_index += 1
                self.clear_accum()
                if self.line_index >= self.line_count:
                    self.frame_done = True
                    self.phase = "Idle"
                    self.phase_samples = 0
                else:
                    self.phase = "Porch1"
                    self.phase_samples = 0
                    self.reset_pixel_state()

    def step(self, mono):
        if self.phase == "Idle":
            return
        self.line_samples += 1
        if self.phase == "Porch1":
            self.phase_samples += 1
            if self.phase_samples >= self.porch_samples:
                self.phase = "Green"
                self.phase_samples = 0
                self.reset_pixel_state()
        elif self.phase == "Porch2":
            self.phase_samples += 1
            if self.phase_samples >= self.porch_samples:
                self.phase = "Blue"
                self.phase_samples = 0
                self.reset_pixel_state()
        elif self.phase == "Sync":
            self.phase_samples += 1
            if self.phase_samples >= self.sync_samples:
                self.phase = "Porch3"
                self.phase_samples = 0
        elif self.phase == "Porch3":
            self.phase_samples += 1
            if self.phase_samples >= self.porch_samples:
                self.phase = "Red"
                self.phase_samples = 0
                self.reset_pixel_state()
        else:
            self.step_color(mono)


def panel_rgb():
    r = (K_PANEL_BG >> 16) & 0xFF
    g = (K_PANEL_BG >> 8) & 0xFF
    b = K_PANEL_BG & 0xFF
    return (r, g, b)


def compute_sync_phase_offset(has_sync_positions, base_sample, line_samples, sync_samples):
    if base_sample is None or line_samples <= 0 or sync_samples <= 0:
        return 0
    bins = [0] * K_SYNC_PHASE_BINS
    hits = 0
    for pos in has_sync_positions:
        if pos < base_sample:
            continue
        phase = (pos - base_sample) % line_samples
        bin_idx = int(phase * K_SYNC_PHASE_BINS / line_samples)
        if bin_idx < 0:
            bin_idx = 0
        if bin_idx >= K_SYNC_PHASE_BINS:
            bin_idx = K_SYNC_PHASE_BINS - 1
        if bins[bin_idx] < 0xFFFF:
            bins[bin_idx] += 1
        hits += 1
    if hits < K_SYNC_PHASE_MIN_HITS:
        return 0

    max_conv = None
    max_idx = 0
    sync_bins = int(sync_samples * K_SYNC_PHASE_BINS / line_samples)
    search_bins = sync_bins * 2
    if search_bins < 8:
        search_bins = 8
    if search_bins > K_SYNC_PHASE_BINS // 2:
        search_bins = K_SYNC_PHASE_BINS // 2
    for i in range(K_SYNC_PHASE_BINS - 7):
        if i > search_bins and i < (K_SYNC_PHASE_BINS - search_bins):
            continue
        conv = sum(bins[i:i + 4]) - sum(bins[i + 4:i + 8])
        if max_conv is None or conv > max_conv:
            max_conv = conv
            max_idx = i + 4
    fall_bin = max_idx
    if fall_bin > K_SYNC_PHASE_BINS // 2:
        fall_bin -= K_SYNC_PHASE_BINS
    fall_samples = int(fall_bin * line_samples / K_SYNC_PHASE_BINS)
    offset = sync_samples - fall_samples
    if offset < 0:
        offset = 0
    if offset > line_samples:
        offset = line_samples
    return offset


def decode_scottie1(wav_path: Path, out_path: Path):
    data = wav_path.read_bytes()
    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise RuntimeError("WAV header invalid")

    pos = 12
    fmt = None
    data_start = None
    data_size = None
    while pos + 8 <= len(data):
        chunk_id = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        pos += 8
        if chunk_id == b"fmt ":
            fmt = data[pos:pos + size]
        elif chunk_id == b"data":
            data_start = pos
            data_size = size if size > 0 else len(data) - pos
            break
        pos += size

    if fmt is None or data_start is None:
        raise RuntimeError("WAV chunks missing")

    audio_format, channels, sample_rate, byte_rate, block_align, bits = struct.unpack(
        "<HHIIHH", fmt[:16]
    )
    if audio_format != 1:
        raise RuntimeError("WAV not PCM")
    if channels != CHANNELS:
        raise RuntimeError("WAV channels mismatch")
    if sample_rate != SAMPLE_RATE:
        raise RuntimeError("WAV rate mismatch")
    if bits != BITS_PER_SAMPLE:
        raise RuntimeError("WAV bits mismatch")

    raw = data[data_start:data_start + data_size]
    samples = array("h")
    samples.frombytes(raw)

    if USE_SLOWRX:
        header = HeaderDetector()
        header_end = None
        for idx, mono in enumerate(samples):
            header_end = header.push(mono, idx)
            if header_end is not None:
                break
        if header_end is None:
            header_end = 0
        expected_frame = int(SAMPLE_RATE * MODE_S1.line_time * MODE_S1.num_lines + 0.5)
        if header_end > 0 and header_end < len(samples):
            remaining = len(samples) - header_end
            if remaining >= int(expected_frame * 0.8):
                samples = samples[header_end:]
        freq_shift = estimate_header_shift(samples)
        if abs(freq_shift) > 200.0:
            freq_shift = 0.0
        has_sync = build_has_sync(samples, SAMPLE_RATE, freq_shift=freq_shift)
        rate, skip = find_sync_slowrx(MODE_S1, SAMPLE_RATE, has_sync)
        stored = demod_stored_lum(samples, freq_shift, MODE_S1.name)
        out_img = decode_mode_pixelgrid(samples, MODE_S1, rate, skip, stored)
        smooth_frame(out_img)
        out_img.save(out_path)
        return out_img, True, [], None

    sync = SyncDetector()
    header = HeaderDetector()
    decoder = Scottie1Decoder()
    receiving = False
    min_sync_gap = int(SAMPLE_RATE * (K_MIN_SYNC_GAP_MS / 1000.0))
    expected_line_samples = int(
        SAMPLE_RATE
        * ((K_PORCH_MS * 3 + K_COLOR_MS_SCOTTIE1 * 3 + K_SYNC_PULSE_MS) / 1000.0)
    )
    last_sync_index = -min_sync_gap
    header_ok = False
    header_end = None
    header_timeout = int(SAMPLE_RATE * 5)

    sync_positions = []
    if WINDOWED_SYNC:
        # Locate header end first.
        for idx, mono in enumerate(samples):
            if header_end is None:
                header_end = header.push(mono, idx)
            if header_end is not None:
                break
        if header_end is None:
            header_end = 0

        def score_at(end_idx):
            return sync.score_at(samples, end_idx)

        # First sync search: near header end.
        search_start = header_end
        search_end = header_end + expected_line_samples // 2
        best = None
        best_idx = None
        for end_idx in range(search_start, search_end, K_SYNC_SCORE_HOP):
            res = score_at(end_idx)
            if res is None:
                continue
            score, valid = res
            if not valid:
                continue
            if best is None or score > best:
                best = score
                best_idx = end_idx
        if best_idx is None:
            best = None
            best_idx = header_end
            for end_idx in range(search_start, search_end, K_SYNC_SCORE_HOP):
                res = score_at(end_idx)
                if res is None:
                    continue
                score, _ = res
                if best is None or score > best:
                    best = score
                    best_idx = end_idx
        sync_positions.append(best_idx)

        # Subsequent syncs.
        max_lines = K_IN_HEIGHT_SCOTTIE
        for _ in range(max_lines):
            expected = sync_positions[-1] + expected_line_samples
            window = int(expected_line_samples * K_SYNC_WINDOW_PCT)
            start = expected - window
            end = expected + window
            if start < 0:
                start = 0
            best = None
            best_idx = None
            for end_idx in range(start, end, K_SYNC_SCORE_HOP):
                res = score_at(end_idx)
                if res is None:
                    continue
                score, valid = res
                if not valid:
                    continue
                if best is None or score > best:
                    best = score
                    best_idx = end_idx
            if best_idx is None:
                best = None
                best_idx = expected
                for end_idx in range(start, end, K_SYNC_SCORE_HOP):
                    res = score_at(end_idx)
                    if res is None:
                        continue
                    score, _ = res
                    if best is None or score > best:
                        best = score
                        best_idx = end_idx
            sync_positions.append(best_idx)
            if len(sync_positions) >= max_lines + 1:
                break

        has_sync_positions = []
        score_start = header_end if header_end is not None else 0
        if score_start < K_SYNC_SCORE_WINDOW:
            score_start = K_SYNC_SCORE_WINDOW
        for end_idx in range(score_start, len(samples), K_SYNC_SCORE_HOP):
            res = score_at(end_idx)
            if res is None:
                continue
            score, valid = res
            if valid:
                has_sync_positions.append(end_idx)

        line_samples = expected_line_samples
        if len(sync_positions) > 1:
            diffs = [b - a for a, b in zip(sync_positions, sync_positions[1:])]
            if diffs:
                line_samples = int(sum(diffs) / len(diffs) + 0.5)
        base_sample = sync_positions[0] if sync_positions else (header_end or 0)
        sync_samples = int(SAMPLE_RATE * (K_SYNC_PULSE_MS / 1000.0) + 0.5)
        decoder.sync_phase_offset = compute_sync_phase_offset(
            has_sync_positions, base_sample, line_samples, sync_samples
        )
        if line_samples > 0:
            scale = float(expected_line_samples) / float(line_samples)
            if scale < 0.95:
                scale = 0.95
            if scale > 1.05:
                scale = 1.05
            decoder.timing_scale = scale
            decoder.apply_timing_scale()

        next_sync_idx = 0
        next_sync = sync_positions[next_sync_idx] if sync_positions else None
        for idx, mono in enumerate(samples):
            if next_sync is not None and idx == next_sync:
                started = decoder.on_sync(receiving)
                if started:
                    receiving = True
                next_sync_idx += 1
                if next_sync_idx < len(sync_positions):
                    next_sync = sync_positions[next_sync_idx]
                else:
                    next_sync = None
            if receiving:
                decoder.step(mono)
                if decoder.frame_done:
                    break
    else:
        tracker = SyncLineTracker(expected_line_samples, window_pct=K_SYNC_WINDOW_PCT)
        for idx, mono in enumerate(samples):
            if not header_ok:
                if header_end is None:
                    header_end = header.push(mono, idx)
                if header_end is not None and idx >= header_end:
                    header_ok = True
                    sync = SyncDetector()
                    last_sync_index = -min_sync_gap
                elif idx >= header_timeout:
                    header_ok = True
                    sync = SyncDetector()
                    last_sync_index = -min_sync_gap
            if not header_ok:
                continue

            hit, last_sync_index = sync.push(
                mono,
                True,
                idx,
                min_sync_gap,
                last_sync_index,
                expected_samples=expected_line_samples,
                window_pct=K_SYNC_WINDOW_PCT,
            )
            if hit:
                if tracker.accept(idx):
                    sync_positions.append(idx)

        if SMOOTH_SYNC and len(sync_positions) >= 2:
            n = len(sync_positions)
            xs = list(range(n))
            ys = sync_positions
            sum_x = sum(xs)
            sum_y = sum(ys)
            sum_xx = sum(x * x for x in xs)
            sum_xy = sum(x * y for x, y in zip(xs, ys))
            denom = n * sum_xx - sum_x * sum_x
            if denom != 0:
                a = (n * sum_xy - sum_x * sum_y) / denom
                b = (sum_y - a * sum_x) / n
                sync_positions = [int(a * i + b + 0.5) for i in range(n)]

        has_sync_positions = []
        score_start = header_end if header_end is not None else 0
        if score_start < K_SYNC_SCORE_WINDOW:
            score_start = K_SYNC_SCORE_WINDOW
        for end_idx in range(score_start, len(samples), K_SYNC_SCORE_HOP):
            res = sync.score_at(samples, end_idx)
            if res is None:
                continue
            score, valid = res
            if valid:
                has_sync_positions.append(end_idx)

        line_samples = expected_line_samples
        if tracker.fit_ready and tracker.slope > 0:
            line_samples = int(tracker.slope + 0.5)
        elif len(sync_positions) > 1:
            diffs = [b - a for a, b in zip(sync_positions, sync_positions[1:])]
            if diffs:
                line_samples = int(sum(diffs) / len(diffs) + 0.5)

        base_sample = sync_positions[0] if sync_positions else (header_end or 0)
        sync_samples = int(SAMPLE_RATE * (K_SYNC_PULSE_MS / 1000.0) + 0.5)
        decoder.sync_phase_offset = compute_sync_phase_offset(
            has_sync_positions, base_sample, line_samples, sync_samples
        )
        if line_samples > 0:
            scale = float(expected_line_samples) / float(line_samples)
            if scale < 0.95:
                scale = 0.95
            if scale > 1.05:
                scale = 1.05
            decoder.timing_scale = scale
            decoder.apply_timing_scale()

        next_sync_idx = 0
        next_sync = sync_positions[next_sync_idx] if sync_positions else None
        for idx, mono in enumerate(samples):
            if next_sync is not None and idx == next_sync:
                started = decoder.on_sync(receiving)
                if started:
                    receiving = True
                next_sync_idx += 1
                if next_sync_idx < len(sync_positions):
                    next_sync = sync_positions[next_sync_idx]
                else:
                    next_sync = None
            if receiving:
                decoder.step(mono)
                if decoder.frame_done:
                    break

    smooth_frame(decoder.out_img)
    decoder.out_img.save(out_path)
    return decoder.out_img, receiving, sync_positions, decoder


def smooth_frame(img: Image.Image):
    w, h = img.size
    if w < K_PAD_X + K_OUT_IMAGE_WIDTH:
        return
    pix = img.load()
    for y in range(h):
        r_line = [0] * K_OUT_IMAGE_WIDTH
        g_line = [0] * K_OUT_IMAGE_WIDTH
        b_line = [0] * K_OUT_IMAGE_WIDTH
        for x in range(K_OUT_IMAGE_WIDTH):
            r, g, b = pix[K_PAD_X + x, y]
            r_line[x] = r
            g_line[x] = g
            b_line[x] = b
        for x in range(K_OUT_IMAGE_WIDTH):
            x0 = x - 1 if x > 0 else x
            x2 = x + 1 if x + 1 < K_OUT_IMAGE_WIDTH else x
            r = (r_line[x0] + r_line[x] + r_line[x2]) // 3
            g = (g_line[x0] + g_line[x] + g_line[x2]) // 3
            b = (b_line[x0] + b_line[x] + b_line[x2]) // 3
            pix[K_PAD_X + x, y] = (r, g, b)


def compare_images(decoded: Image.Image, reference: Image.Image, out_dir: Path):
    ref = reference.convert("RGB")
    crop = decoded.crop((K_PAD_X, 0, K_PAD_X + K_OUT_IMAGE_WIDTH, K_OUT_HEIGHT))
    ref_resized = ref.resize((K_OUT_IMAGE_WIDTH, K_OUT_HEIGHT), Image.BILINEAR)
    diff = ImageChops.difference(crop, ref_resized)
    stat = ImageStat.Stat(diff)
    mean = stat.mean
    rms = stat.rms
    crop_mean = ImageStat.Stat(crop).mean
    ref_mean = ImageStat.Stat(ref_resized).mean
    best_shift = 0
    best_rms = None
    for shift in range(-60, 61):
        shifted = ImageChops.offset(crop, shift, 0)
        d = ImageChops.difference(shifted, ref_resized)
        r = ImageStat.Stat(d).rms
        score = sum(r)
        if best_rms is None or score < best_rms:
            best_rms = score
            best_shift = shift
    out_dir.mkdir(parents=True, exist_ok=True)
    crop_path = out_dir / "decoded_crop.png"
    diff_path = out_dir / "decoded_diff.png"
    ref_path = out_dir / "reference_resized.png"
    crop.save(crop_path)
    diff.save(diff_path)
    ref_resized.save(ref_path)
    return {
        "decoded_crop": crop_path,
        "diff": diff_path,
        "ref_resized": ref_path,
        "mean": mean,
        "rms": rms,
        "best_shift": best_shift,
        "best_shift_rms": best_rms,
        "crop_mean": crop_mean,
        "ref_mean": ref_mean,
    }


def main():
    import sys

    wav_arg = sys.argv[1] if len(sys.argv) > 1 else "docs/sstv/2026-02-11_004.wav"
    wav_path = Path(wav_arg)
    ref_path = Path("docs/sstv/dog_emo.png")
    stem = wav_path.stem
    out_img_path = Path(f"docs/sstv/decoded_{stem}.png")
    out_dir = Path(f"docs/sstv/compare_{stem}")

    img, ok, sync_positions, decoder = decode_scottie1(wav_path, out_img_path)
    print(f"decoded: {out_img_path} receiving={ok}")
    if decoder and decoder.freq_min is not None and decoder.freq_max is not None:
        print(f"freq range: min={decoder.freq_min:.1f} max={decoder.freq_max:.1f}")
    if len(sync_positions) > 1:
        diffs = [b - a for a, b in zip(sync_positions, sync_positions[1:])]
        avg = sum(diffs) / len(diffs)
        min_v = min(diffs)
        max_v = max(diffs)
        var = sum((d - avg) ** 2 for d in diffs) / len(diffs)
        std = math.sqrt(var)
        expected = int(SAMPLE_RATE * ((K_PORCH_MS * 3 + K_COLOR_MS_SCOTTIE1 * 3 + K_SYNC_PULSE_MS) / 1000.0))
        print(f"sync stats: count={len(diffs)} avg={avg:.1f} min={min_v} max={max_v} std={std:.1f} expected={expected}")
    ref = Image.open(ref_path)
    stats = compare_images(img, ref, out_dir)
    print("compare:")
    print(f"  crop: {stats['decoded_crop']}")
    print(f"  ref:  {stats['ref_resized']}")
    print(f"  diff: {stats['diff']}")
    print(f"  mean: {stats['mean']}")
    print(f"  rms:  {stats['rms']}")
    print(f"  best_shift_x: {stats['best_shift']} score={stats['best_shift_rms']:.1f}")
    print(f"  decoded_mean: {stats['crop_mean']}")
    print(f"  ref_mean:     {stats['ref_mean']}")
    # report freq range from decoder if available


if __name__ == "__main__":
    main()
