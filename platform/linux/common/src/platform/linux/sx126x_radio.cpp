#include "platform/linux/sx126x_radio.h"
#include "sx126x_calibration.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
namespace
{

constexpr std::uint8_t kCmdSetStandby = 0x80;
constexpr std::uint8_t kCmdGetStatus = 0xC0;
constexpr std::uint8_t kCmdSetRx = 0x82;
constexpr std::uint8_t kCmdSetTx = 0x83;
constexpr std::uint8_t kCmdSetPacketType = 0x8A;
constexpr std::uint8_t kCmdSetRfFrequency = 0x86;
constexpr std::uint8_t kCmdSetTxParams = 0x8E;
constexpr std::uint8_t kCmdSetModulationParams = 0x8B;
constexpr std::uint8_t kCmdSetPacketParams = 0x8C;
constexpr std::uint8_t kCmdSetBufferBaseAddress = 0x8F;
constexpr std::uint8_t kCmdSetRegulatorMode = 0x96;
constexpr std::uint8_t kCmdSetPaConfig = 0x95;
constexpr std::uint8_t kCmdSetDioIrqParams = 0x08;
constexpr std::uint8_t kCmdGetIrqStatus = 0x12;
constexpr std::uint8_t kCmdClearIrqStatus = 0x02;
constexpr std::uint8_t kCmdSetDio2AsRfSwitchCtrl = 0x9D;
constexpr std::uint8_t kCmdSetDio3AsTcxoCtrl = 0x97;
constexpr std::uint8_t kCmdGetRssiInst = 0x15;
constexpr std::uint8_t kCmdGetRxBufferStatus = 0x13;
constexpr std::uint8_t kCmdGetPacketStatus = 0x14;
constexpr std::uint8_t kCmdReadBuffer = 0x1E;
constexpr std::uint8_t kCmdWriteBuffer = 0x0E;
constexpr std::uint8_t kCmdCalibrateImage = 0x98;
constexpr std::uint8_t kCmdSetRxTxFallbackMode = 0x93;
constexpr std::uint8_t kCmdCalibrate = 0x89;
constexpr std::uint8_t kCmdReadRegister = 0x1D;
constexpr std::uint8_t kCmdWriteRegister = 0x0D;

constexpr std::uint8_t kPacketTypeLoRa = 0x01;
constexpr std::uint8_t kStandbyRc = 0x00;
constexpr std::uint8_t kRegulatorDcDc = 0x01;
constexpr std::uint8_t kFallbackStandbyRc = 0x20;
constexpr std::uint8_t kPaRamp200u = 0x04;
constexpr std::uint8_t kPaConfigDeviceSelSx1262 = 0x00;
constexpr std::uint8_t kPaConfigPaLut = 0x01;
constexpr std::uint8_t kLoRaHeaderExplicit = 0x00;
constexpr std::uint8_t kLoRaCrcOff = 0x00;
constexpr std::uint8_t kLoRaCrcOn = 0x01;
constexpr std::uint8_t kLoRaIqStandard = 0x00;
constexpr std::uint32_t kRxTimeoutInf = 0xFFFFFF;
constexpr std::uint16_t kIrqTxDone = 0x0001;
constexpr std::uint16_t kIrqRxDone = 0x0002;
constexpr std::uint16_t kIrqHeaderErr = 0x0020;
constexpr std::uint16_t kIrqCrcErr = 0x0040;
constexpr std::uint16_t kIrqTimeout = 0x0200;
constexpr std::uint16_t kIrqAll = 0x43FF;
constexpr std::uint16_t kRegOcpConfiguration = 0x08E7;
constexpr std::uint16_t kRegLoraSyncWordMsb = 0x0740;
constexpr float kFrequencyStepHz = 0.9536743164f;

int env_int_or_default(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end != nullptr && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

float env_float_or_default(const char* name, float fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || (end != nullptr && *end != '\0') ||
        !std::isfinite(parsed))
    {
        return fallback;
    }
    return parsed;
}

std::string env_string_or_default(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    if (value != nullptr && value[0] != '\0')
    {
        return value;
    }
    return fallback;
}

bool env_flag_or_default(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

std::uint8_t map_lora_bw(float bw_khz)
{
    const float half = bw_khz / 2.0f;
    const int bw_div2 = static_cast<int>(half + 0.01f);
    switch (bw_div2)
    {
    case 3:
        return 0x00;
    case 5:
        return 0x08;
    case 7:
        return 0x01;
    case 10:
        return 0x09;
    case 15:
        return 0x02;
    case 20:
        return 0x0A;
    case 31:
        return 0x03;
    case 62:
        return 0x04;
    case 125:
        return 0x05;
    case 250:
        return 0x06;
    default:
        return 0x05;
    }
}

std::uint8_t map_lora_cr(std::uint8_t cr)
{
    if (cr < 5) return 0x01;
    if (cr > 8) return 0x04;
    return static_cast<std::uint8_t>(cr - 4);
}

std::uint8_t calc_ldro(std::uint8_t sf, float bw_khz)
{
    const float symbol_ms = static_cast<float>(1UL << sf) / bw_khz;
    return symbol_ms >= 16.0f ? 0x01 : 0x00;
}

std::uint32_t rf_frequency_raw(float freq_mhz)
{
    return static_cast<std::uint32_t>(
        (static_cast<double>(freq_mhz) * 1000000.0) /
        static_cast<double>(kFrequencyStepHz));
}

std::uint8_t ocp_for_60ma()
{
    return static_cast<std::uint8_t>(60.0f / 2.5f);
}

void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void close_fd(int& fd)
{
#if defined(__linux__)
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
#else
    fd = -1;
#endif
}

std::string errno_suffix()
{
#if defined(__linux__)
    return std::string(" errno=") + std::to_string(errno) + " (" +
           std::strerror(errno) + ")";
#else
    return {};
#endif
}

std::string hex_bytes(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size == 0)
    {
        return {};
    }

    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 3U);
    for (std::size_t i = 0; i < size; ++i)
    {
        if (i != 0)
        {
            out.push_back(' ');
        }
        out.push_back(kHex[(data[i] >> 4) & 0x0F]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

const char* bool_name(bool value)
{
    return value ? "true" : "false";
}

std::string describe_radio_config(const Sx126xRadioConfig& config)
{
    char buffer[384] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "spi=%s gpiochip=%s power=%d reset=%d busy=%d irq=%d hz=%lu dio2_rf=%s dio3_tcxo_1v8=%s",
                  config.spi_device.c_str(),
                  config.gpiochip.c_str(),
                  config.power_gpio,
                  config.reset_gpio,
                  config.busy_gpio,
                  config.irq_gpio,
                  static_cast<unsigned long>(config.spi_speed_hz),
                  bool_name(config.dio2_as_rf_switch),
                  bool_name(config.dio3_tcxo_1v8));
    return buffer;
}

#if defined(__linux__)
bool request_gpio_line(int chip_fd,
                       unsigned offset,
                       bool output,
                       int initial_value,
                       const char* label,
                       int* out_fd)
{
    if (out_fd == nullptr)
    {
        return false;
    }
    *out_fd = -1;

    gpiohandle_request req{};
    req.lineoffsets[0] = offset;
    req.lines = 1;
    req.flags = output ? GPIOHANDLE_REQUEST_OUTPUT
                       : GPIOHANDLE_REQUEST_INPUT;
    req.default_values[0] = initial_value ? 1 : 0;
    std::snprintf(req.consumer_label,
                  sizeof(req.consumer_label),
                  "%s",
                  label == nullptr ? "trailmate" : label);

    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) != 0)
    {
        return false;
    }
    *out_fd = req.fd;
    return true;
}

bool set_gpio_value(int fd, int value)
{
    if (fd < 0)
    {
        return false;
    }
    gpiohandle_data data{};
    data.values[0] = value ? 1 : 0;
    return ioctl(fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) == 0;
}

int get_gpio_value(int fd)
{
    if (fd < 0)
    {
        return -1;
    }
    gpiohandle_data data{};
    if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) != 0)
    {
        return -1;
    }
    return data.values[0] ? 1 : 0;
}
#endif

} // namespace

Sx126xRadio& Sx126xRadio::instance()
{
    static Sx126xRadio radio;
    return radio;
}

Sx126xRadio::~Sx126xRadio()
{
    std::lock_guard<std::mutex> lock(mutex_);
    closeLocked();
}

bool Sx126xRadio::hardwareCandidatePresent()
{
    const std::filesystem::path spi =
        env_string_or_default("TRAIL_MATE_LORA_SPI", "/dev/spidev1.0");
    std::error_code ec;
    return std::filesystem::exists(spi, ec) && !ec &&
           !env_flag_or_default("TRAIL_MATE_LORA_DISABLE", false);
}

Sx126xRadioConfig Sx126xRadio::defaultConfigFromEnvironment()
{
    Sx126xRadioConfig config{};
    config.spi_device =
        env_string_or_default("TRAIL_MATE_LORA_SPI", config.spi_device.c_str());
    config.gpiochip =
        env_string_or_default("TRAIL_MATE_LORA_GPIOCHIP", config.gpiochip.c_str());
    config.power_gpio =
        env_int_or_default("TRAIL_MATE_LORA_POWER_GPIO", config.power_gpio);
    config.reset_gpio =
        env_int_or_default("TRAIL_MATE_LORA_RESET_GPIO", config.reset_gpio);
    config.busy_gpio =
        env_int_or_default("TRAIL_MATE_LORA_BUSY_GPIO", config.busy_gpio);
    config.irq_gpio =
        env_int_or_default("TRAIL_MATE_LORA_IRQ_GPIO", config.irq_gpio);
    config.spi_speed_hz = static_cast<std::uint32_t>(
        std::max(100000, env_int_or_default("TRAIL_MATE_LORA_SPI_HZ",
                                            static_cast<int>(config.spi_speed_hz))));
    config.dio2_as_rf_switch =
        env_flag_or_default("TRAIL_MATE_LORA_DIO2_RF_SWITCH",
                            config.dio2_as_rf_switch);
    config.dio3_tcxo_1v8 =
        env_flag_or_default("TRAIL_MATE_LORA_DIO3_TCXO_1V8",
                            config.dio3_tcxo_1v8);
    return config;
}

Sx126xLoRaConfig Sx126xRadio::defaultLoRaConfigFromEnvironment()
{
    Sx126xLoRaConfig config{};
    config.freq_mhz =
        env_float_or_default("TRAIL_MATE_LORA_FREQ_MHZ", config.freq_mhz);
    config.bw_khz =
        env_float_or_default("TRAIL_MATE_LORA_BW_KHZ", config.bw_khz);
    config.sf = static_cast<std::uint8_t>(
        std::clamp(env_int_or_default("TRAIL_MATE_LORA_SF", config.sf), 5, 12));
    config.cr = static_cast<std::uint8_t>(
        std::clamp(env_int_or_default("TRAIL_MATE_LORA_CR", config.cr), 5, 8));
    config.tx_power_dbm = static_cast<std::int8_t>(
        std::clamp(env_int_or_default("TRAIL_MATE_LORA_TX_DBM",
                                      config.tx_power_dbm),
                   -9,
                   22));
    config.preamble_len = static_cast<std::uint16_t>(
        std::clamp(env_int_or_default("TRAIL_MATE_LORA_PREAMBLE",
                                      config.preamble_len),
                   6,
                   65535));
    config.sync_word = static_cast<std::uint8_t>(
        std::clamp(env_int_or_default("TRAIL_MATE_LORA_SYNC_WORD",
                                      config.sync_word),
                   0,
                   255));
    return config;
}

bool Sx126xRadio::acquire(const Sx126xRadioConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initLocked(config))
    {
        return false;
    }
    ++users_;
    return true;
}

void Sx126xRadio::release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (users_ > 0)
    {
        --users_;
    }
    if (users_ == 0 && online_)
    {
        const std::uint8_t mode = kStandbyRc;
        (void)writeCommandLocked(kCmdSetStandby, &mode, 1, true);
    }
}

bool Sx126xRadio::configureLoRa(const Sx126xLoRaConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const bool ok = initLocked(config_) && configureLoRaLocked(config) &&
                    setDioIrqParamsLocked(
                        kIrqRxDone | kIrqTimeout | kIrqCrcErr | kIrqHeaderErr,
                        kIrqRxDone) &&
                    clearIrqLocked(kIrqAll) &&
                    setBufferBaseLocked(0x00, 0x00) && setRxLocked(kRxTimeoutInf);
    if (ok)
    {
        lora_config_ = config;
    }
    return ok;
}

bool Sx126xRadio::startReceive()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!online_)
    {
        return false;
    }
    return setDioIrqParamsLocked(kIrqRxDone | kIrqTimeout | kIrqCrcErr,
                                 kIrqRxDone) &&
           clearIrqLocked(kIrqAll) && setBufferBaseLocked(0x00, 0x00) &&
           setRxLocked(kRxTimeoutInf);
}

bool Sx126xRadio::configureFsk(const FskSettings& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!online_ || !valid_sx126x_fsk_settings(config))
    {
        setErrorLocked("Invalid FSK settings or radio offline");
        return false;
    }
    const uint8_t standby = kStandbyRc;
    if (!writeCommandLocked(kCmdSetStandby, &standby, 1, true) || !setPacketTypeLocked(0x00) ||
        !setRfFrequencyLocked(config.freq_mhz) || !setTxPowerLocked(config.tx_power_dbm)) return false;
    struct Bandwidth
    {
        float value;
        uint8_t code;
    };
    static constexpr Bandwidth widths[] = {{4.8f, 0x1F}, {5.8f, 0x17}, {7.3f, 0x0F}, {9.7f, 0x1E}, {11.7f, 0x16}, {14.6f, 0x0E}, {19.5f, 0x1D}, {23.4f, 0x15}, {29.3f, 0x0D}, {39.0f, 0x1C}, {46.9f, 0x14}, {58.6f, 0x0C}, {78.2f, 0x1B}, {93.8f, 0x13}, {117.3f, 0x0B}, {156.2f, 0x1A}, {187.2f, 0x12}, {234.3f, 0x0A}, {312.0f, 0x19}, {373.6f, 0x11}, {467.0f, 0x09}};
    uint8_t bandwidth = widths[20].code;
    for (const auto& value : widths)
        if (config.rx_bandwidth_khz <= value.value)
        {
            bandwidth = value.code;
            break;
        }
    const uint32_t bitrate = static_cast<uint32_t>(1024000.0f / config.bitrate_kbps + 0.5f);
    const uint32_t deviation = static_cast<uint32_t>(config.deviation_khz * 1000 / kFrequencyStepHz + 0.5f);
    const uint8_t modulation[] = {uint8_t(bitrate >> 16), uint8_t(bitrate >> 8), uint8_t(bitrate), 0,
                                  bandwidth, uint8_t(deviation >> 16), uint8_t(deviation >> 8), uint8_t(deviation)};
    const uint8_t packet[] = {uint8_t(config.preamble_bits >> 8), uint8_t(config.preamble_bits),
                              0x05, uint8_t(config.sync_word_size * 8), 0, 1, 255, 0x06, 0};
    const uint8_t initial[] = {0x1D, 0x0F}, polynomial[] = {0x10, 0x21};
    const bool success = writeCommandLocked(kCmdSetModulationParams, modulation, sizeof(modulation), true) &&
                         writeCommandLocked(kCmdSetPacketParams, packet, sizeof(packet), true) &&
                         writeRegisterLocked(0x06C0, config.sync_word.data(), config.sync_word_size) &&
                         writeRegisterLocked(0x06BC, initial, sizeof(initial)) &&
                         writeRegisterLocked(0x06BE, polynomial, sizeof(polynomial)) &&
                         setDioIrqParamsLocked(kIrqRxDone | kIrqTimeout | kIrqCrcErr, kIrqRxDone) &&
                         clearIrqLocked(kIrqAll) && setBufferBaseLocked(0, 0) && setRxLocked(kRxTimeoutInf);
    if (success) fsk_config_ = config;
    return success;
}

bool Sx126xRadio::transmit(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size == 0 || size > 220)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!online_)
    {
        return false;
    }

    bool ok = setBufferBaseLocked(0x00, 0x00);
    const std::uint8_t packet[6] = {
        static_cast<std::uint8_t>((lora_config_.preamble_len >> 8) & 0xFF),
        static_cast<std::uint8_t>(lora_config_.preamble_len & 0xFF),
        kLoRaHeaderExplicit,
        static_cast<std::uint8_t>(size),
        lora_config_.crc_len ? kLoRaCrcOn : kLoRaCrcOff,
        kLoRaIqStandard,
    };
    if (packet_type_ == 0x00)
    {
        const uint8_t fsk[] = {uint8_t(fsk_config_.preamble_bits >> 8), uint8_t(fsk_config_.preamble_bits),
                               0x05, 16, 0, 1, uint8_t(size), 0x06, 0};
        ok = ok && writeCommandLocked(kCmdSetPacketParams, fsk, sizeof(fsk), true);
    }
    else ok = ok && writeCommandLocked(kCmdSetPacketParams, packet, sizeof(packet), true);

    if (ok)
    {
        std::array<std::uint8_t, 260> tx{};
        tx[0] = kCmdWriteBuffer;
        tx[1] = 0x00;
        std::memcpy(tx.data() + 2, data, size);
        ok = transferLocked(tx.data(), nullptr, size + 2);
    }

    ok = ok && setDioIrqParamsLocked(kIrqTxDone | kIrqTimeout, kIrqTxDone) &&
         clearIrqLocked(kIrqAll) && setTxLocked(0x000000);
    if (!ok)
    {
        (void)setRxLocked(kRxTimeoutInf);
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    while (true)
    {
        const std::uint32_t irq = getIrqFlagsLocked();
        if ((irq & kIrqTxDone) != 0)
        {
            (void)clearIrqLocked(kIrqAll);
            (void)setRxLocked(kRxTimeoutInf);
            ++tx_packets_;
            return true;
        }
        if ((irq & kIrqTimeout) != 0)
        {
            (void)clearIrqLocked(kIrqAll);
            (void)setRxLocked(kRxTimeoutInf);
            setErrorLocked("LoRa TX timeout");
            return false;
        }
        if (std::chrono::steady_clock::now() - start >
            std::chrono::seconds(4))
        {
            (void)setRxLocked(kRxTimeoutInf);
            setErrorLocked("LoRa TX wait timed out");
            return false;
        }
        sleep_ms(10);
    }
}

bool Sx126xRadio::pollReceive(Sx126xPacket* out)
{
    if (out == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!online_)
    {
        return false;
    }

    const std::uint32_t irq = getIrqFlagsLocked();
    updateLastIrqLocked(irq);
    if ((irq & kIrqRxDone) == 0)
    {
        if ((irq & (kIrqTimeout | kIrqCrcErr | kIrqHeaderErr)) != 0)
        {
            if ((irq & kIrqCrcErr) != 0)
            {
                ++rx_crc_errors_;
                setErrorLocked("LoRa RX CRC error");
            }
            if ((irq & kIrqHeaderErr) != 0)
            {
                ++rx_header_errors_;
                setErrorLocked("LoRa RX header error");
            }
            if ((irq & kIrqTimeout) != 0)
            {
                ++rx_timeouts_;
                setErrorLocked("LoRa RX timeout");
            }
            (void)clearIrqLocked(kIrqAll);
            (void)setRxLocked(kRxTimeoutInf);
        }
        return false;
    }

    std::uint8_t offset = 0;
    const int packet_len = getPacketLengthLocked(&offset);
    if (packet_len <= 0 ||
        packet_len > static_cast<int>(out->data.size()))
    {
        ++rx_invalid_lengths_;
        setErrorLocked("LoRa RX invalid packet length");
        (void)clearIrqLocked(kIrqAll);
        (void)setRxLocked(kRxTimeoutInf);
        return false;
    }

    if (readPacketLocked(offset, out->data.data(),
                         static_cast<std::size_t>(packet_len)) != 0)
    {
        ++rx_read_errors_;
        setErrorLocked("LoRa RX buffer read failed");
        (void)clearIrqLocked(kIrqAll);
        (void)setRxLocked(kRxTimeoutInf);
        return false;
    }

    out->size = static_cast<std::size_t>(packet_len);
    (void)readPacketStatusLocked(&out->rssi_dbm, &out->snr_db);
    out->freq_hz =
        static_cast<std::uint32_t>(lora_config_.freq_mhz * 1000000.0f + 0.5f);
    out->bw_hz =
        static_cast<std::uint32_t>(lora_config_.bw_khz * 1000.0f + 0.5f);
    out->sf = lora_config_.sf;
    out->cr = lora_config_.cr;
    if (packet_type_ == 0x00)
    {
        out->freq_hz = static_cast<uint32_t>(fsk_config_.freq_mhz * 1000000.0f + 0.5f);
        out->bw_hz = 0;
        out->sf = out->cr = 0;
    }

    (void)clearIrqLocked(kIrqAll);
    (void)setRxLocked(kRxTimeoutInf);
    ++rx_packets_;
    return true;
}

float Sx126xRadio::readRssi()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!online_)
    {
        return NAN;
    }
    std::uint8_t raw = 0;
    const bool ok = readCommandLocked(kCmdGetRssiInst, nullptr, 0, &raw, 1, true);
    return ok ? (static_cast<float>(raw) / -2.0f) : NAN;
}

bool Sx126xRadio::isOnline() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return online_;
}

const char* Sx126xRadio::lastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

Sx126xLoRaConfig Sx126xRadio::appliedLoRaConfig() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lora_config_;
}

Sx126xRadioStats Sx126xRadio::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return Sx126xRadioStats{
        .online = online_,
        .rx_packets = rx_packets_,
        .tx_packets = tx_packets_,
        .rx_crc_errors = rx_crc_errors_,
        .rx_header_errors = rx_header_errors_,
        .rx_timeouts = rx_timeouts_,
        .rx_invalid_lengths = rx_invalid_lengths_,
        .rx_read_errors = rx_read_errors_,
        .last_irq_flags = last_irq_flags_,
        .lora_config = lora_config_,
    };
}

bool Sx126xRadio::initLocked(const Sx126xRadioConfig& config)
{
#if !defined(__linux__)
    (void)config;
    setErrorLocked("SX126x Linux driver is unavailable on this OS");
    return false;
#else
    if (initialized_)
    {
        return online_;
    }
    config_ = config;
    setErrorLocked("");
    if (!openGpioLocked() || !openSpiLocked())
    {
        closeLocked();
        return false;
    }

    online_ = prepareRadioHardwareLocked() && probeLocked();
    if (!online_)
    {
        if (last_error_[0] == '\0')
        {
            setErrorStringLocked("SX126x probe failed: " +
                                 describe_radio_config(config_));
        }
        closeLocked();
        return false;
    }
    initialized_ = true;

    const std::uint8_t calibrate = 0x7F;
    (void)writeCommandLocked(kCmdCalibrate, &calibrate, 1, true);
    (void)setPacketTypeLocked(kPacketTypeLoRa);
    (void)setBufferBaseLocked(0x00, 0x00);
    const std::uint8_t regulator = kRegulatorDcDc;
    (void)writeCommandLocked(kCmdSetRegulatorMode, &regulator, 1, true);
    if (config_.dio2_as_rf_switch)
    {
        const std::uint8_t dio2 = 0x01;
        (void)writeCommandLocked(kCmdSetDio2AsRfSwitchCtrl, &dio2, 1, true);
    }
    if (config_.dio3_tcxo_1v8)
    {
        const std::uint8_t tcxo[4] = {0x02, 0x00, 0x01, 0x40};
        (void)writeCommandLocked(kCmdSetDio3AsTcxoCtrl, tcxo, sizeof(tcxo), true);
        sleep_ms(5);
    }
    const std::uint8_t fallback = kFallbackStandbyRc;
    (void)writeCommandLocked(kCmdSetRxTxFallbackMode, &fallback, 1, true);
    (void)clearIrqLocked(kIrqAll);
    const std::uint8_t ocp = ocp_for_60ma();
    (void)writeRegisterLocked(kRegOcpConfiguration, &ocp, 1);
    return true;
#endif
}

bool Sx126xRadio::openSpiLocked()
{
#if !defined(__linux__)
    return false;
#else
    spi_fd_ = open(config_.spi_device.c_str(), O_RDWR | O_CLOEXEC);
    if (spi_fd_ < 0)
    {
        setErrorStringLocked("open spidev failed: " + config_.spi_device +
                             errno_suffix());
        return false;
    }

    std::uint8_t mode = SPI_MODE_0;
    std::uint8_t bits = 8;
    if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode) != 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) != 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &config_.spi_speed_hz) != 0)
    {
        setErrorStringLocked("configure spidev failed: " +
                             describe_radio_config(config_) + errno_suffix());
        return false;
    }
    return true;
#endif
}

bool Sx126xRadio::openGpioLocked()
{
#if !defined(__linux__)
    return false;
#else
    chip_fd_ = open(config_.gpiochip.c_str(), O_RDONLY | O_CLOEXEC);
    if (chip_fd_ < 0)
    {
        setErrorStringLocked("open gpiochip failed: " + config_.gpiochip +
                             errno_suffix());
        return false;
    }

    if (config_.power_gpio >= 0 &&
        !request_gpio_line(chip_fd_,
                           static_cast<unsigned>(config_.power_gpio),
                           true,
                           1,
                           "trailmate-lora-power",
                           &power_fd_))
    {
        setErrorStringLocked("request LoRa power GPIO failed: gpio=" +
                             std::to_string(config_.power_gpio) +
                             errno_suffix());
        return false;
    }
    if (config_.reset_gpio >= 0 &&
        !request_gpio_line(chip_fd_,
                           static_cast<unsigned>(config_.reset_gpio),
                           true,
                           1,
                           "trailmate-lora-reset",
                           &reset_fd_))
    {
        setErrorStringLocked("request LoRa reset GPIO failed: gpio=" +
                             std::to_string(config_.reset_gpio) +
                             errno_suffix());
        return false;
    }
    if (config_.busy_gpio >= 0 &&
        !request_gpio_line(chip_fd_,
                           static_cast<unsigned>(config_.busy_gpio),
                           false,
                           0,
                           "trailmate-lora-busy",
                           &busy_fd_))
    {
        setErrorStringLocked("request LoRa busy GPIO failed: gpio=" +
                             std::to_string(config_.busy_gpio) +
                             errno_suffix());
        return false;
    }
    if (config_.irq_gpio >= 0 &&
        !request_gpio_line(chip_fd_,
                           static_cast<unsigned>(config_.irq_gpio),
                           false,
                           0,
                           "trailmate-lora-irq",
                           &irq_fd_))
    {
        setErrorStringLocked("request LoRa IRQ GPIO failed: gpio=" +
                             std::to_string(config_.irq_gpio) +
                             errno_suffix());
        return false;
    }
    return true;
#endif
}

void Sx126xRadio::closeLocked()
{
    close_fd(power_fd_);
    close_fd(reset_fd_);
    close_fd(busy_fd_);
    close_fd(irq_fd_);
    close_fd(chip_fd_);
    close_fd(spi_fd_);
    initialized_ = false;
    online_ = false;
    users_ = 0;
    packet_type_ = 0xFF;
    freq_mhz_ = 0;
    image_calibration_ = {};
    transport_failed_ = false;
}

void Sx126xRadio::updateLastIrqLocked(std::uint32_t irq)
{
    last_irq_flags_ = irq;
}

bool Sx126xRadio::waitReadyLocked()
{
    if (transport_failed_) return false;
#if defined(__linux__)
    if (busy_fd_ >= 0)
    {
        const auto start = std::chrono::steady_clock::now();
        while (true)
        {
            const int busy = get_gpio_value(busy_fd_);
            if (busy == 0) return true;
            if (busy < 0)
            {
                setErrorLocked("SX126x BUSY GPIO read failed");
                break;
            }
            if (std::chrono::steady_clock::now() - start >
                std::chrono::seconds(1))
            {
                setErrorLocked("SX126x BUSY timed out");
                break;
            }
            sleep_ms(1);
        }
        transport_failed_ = true;
        online_ = false;
        return false;
    }
#endif
    std::this_thread::sleep_for(std::chrono::microseconds(250));
    return true;
}

bool Sx126xRadio::transferLocked(const std::uint8_t* tx,
                                 std::uint8_t* rx,
                                 std::size_t size)
{
#if !defined(__linux__)
    (void)tx;
    (void)rx;
    (void)size;
    return false;
#else
    if (spi_fd_ < 0 || tx == nullptr || size == 0 || size > 260)
    {
        setErrorLocked("invalid SPI transfer");
        return false;
    }
    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<unsigned long>(tx);
    transfer.rx_buf = reinterpret_cast<unsigned long>(rx);
    transfer.len = static_cast<__u32>(size);
    transfer.speed_hz = config_.spi_speed_hz;
    transfer.bits_per_word = 8;
    transfer.cs_change = 0;
    if (ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &transfer) < 1)
    {
        transport_failed_ = true;
        online_ = false;
        setErrorStringLocked("SPI transfer failed: bytes=" +
                             std::to_string(size) + " " +
                             describe_radio_config(config_) +
                             errno_suffix());
        return false;
    }
    return true;
#endif
}

bool Sx126xRadio::writeCommandLocked(std::uint8_t cmd,
                                     const std::uint8_t* data,
                                     std::size_t size,
                                     bool wait)
{
    if (!waitReadyLocked()) return false;
    std::array<std::uint8_t, 260> tx{};
    const std::size_t total = 1 + size;
    if (total > tx.size())
    {
        setErrorLocked("SX126x command too large");
        return false;
    }
    tx[0] = cmd;
    if (data != nullptr && size > 0)
    {
        std::memcpy(tx.data() + 1, data, size);
    }
    const bool ok = transferLocked(tx.data(), nullptr, total);
    if (wait && !waitReadyLocked()) return false;
    return ok;
}

bool Sx126xRadio::readCommandLocked(std::uint8_t cmd,
                                    const std::uint8_t* prefix,
                                    std::size_t prefix_size,
                                    std::uint8_t* data,
                                    std::size_t size,
                                    bool wait)
{
    if (!waitReadyLocked()) return false;
    std::array<std::uint8_t, 260> tx{};
    std::array<std::uint8_t, 260> rx{};
    const std::size_t total = 1 + prefix_size + 1 + size;
    if (total > tx.size())
    {
        setErrorLocked("SX126x read command too large");
        return false;
    }
    tx[0] = cmd;
    if (prefix != nullptr && prefix_size > 0)
    {
        std::memcpy(tx.data() + 1, prefix, prefix_size);
    }
    const bool ok = transferLocked(tx.data(), rx.data(), total);
    if (ok && data != nullptr && size > 0)
    {
        std::memcpy(data, rx.data() + 1 + prefix_size + 1, size);
    }
    if (wait && !waitReadyLocked()) return false;
    return ok;
}

bool Sx126xRadio::writeRegisterLocked(std::uint16_t addr,
                                      const std::uint8_t* data,
                                      std::size_t size)
{
    const std::uint8_t prefix[2] = {
        static_cast<std::uint8_t>((addr >> 8) & 0xFF),
        static_cast<std::uint8_t>(addr & 0xFF),
    };
    std::array<std::uint8_t, 260> tx{};
    const std::size_t total = 1 + sizeof(prefix) + size;
    if (total > tx.size())
    {
        setErrorLocked("SX126x register write too large");
        return false;
    }
    tx[0] = kCmdWriteRegister;
    std::memcpy(tx.data() + 1, prefix, sizeof(prefix));
    if (data != nullptr && size > 0)
    {
        std::memcpy(tx.data() + 1 + sizeof(prefix), data, size);
    }
    if (!waitReadyLocked()) return false;
    const bool ok = transferLocked(tx.data(), nullptr, total);
    return waitReadyLocked() && ok;
}

bool Sx126xRadio::readRegisterLocked(std::uint16_t addr,
                                     std::uint8_t* data,
                                     std::size_t size)
{
    const std::uint8_t prefix[2] = {
        static_cast<std::uint8_t>((addr >> 8) & 0xFF),
        static_cast<std::uint8_t>(addr & 0xFF),
    };
    return readCommandLocked(kCmdReadRegister, prefix, sizeof(prefix), data, size, true);
}

bool Sx126xRadio::prepareRadioHardwareLocked()
{
#if !defined(__linux__)
    return false;
#else
    if (power_fd_ >= 0)
    {
        if (!set_gpio_value(power_fd_, 1))
        {
            setErrorStringLocked("set LoRa power GPIO high failed: gpio=" +
                                 std::to_string(config_.power_gpio) +
                                 errno_suffix());
            return false;
        }
        sleep_ms(100);
    }

    if (reset_fd_ >= 0)
    {
        if (!set_gpio_value(reset_fd_, 0))
        {
            setErrorStringLocked("assert LoRa reset failed: gpio=" +
                                 std::to_string(config_.reset_gpio) +
                                 errno_suffix());
            return false;
        }
        sleep_ms(10);
        if (!set_gpio_value(reset_fd_, 1))
        {
            setErrorStringLocked("release LoRa reset failed: gpio=" +
                                 std::to_string(config_.reset_gpio) +
                                 errno_suffix());
            return false;
        }
        sleep_ms(30);
    }

    if (!waitReadyLocked()) return false;

    if (config_.dio3_tcxo_1v8)
    {
        const std::uint8_t tcxo[4] = {0x02, 0x00, 0x01, 0x40};
        if (!writeCommandLocked(kCmdSetDio3AsTcxoCtrl, tcxo, sizeof(tcxo), true))
        {
            setErrorStringLocked("SX1262 TCXO enable failed: " +
                                 describe_radio_config(config_));
            return false;
        }
        sleep_ms(20);
    }

    if (config_.dio2_as_rf_switch)
    {
        const std::uint8_t dio2 = 0x01;
        if (!writeCommandLocked(kCmdSetDio2AsRfSwitchCtrl, &dio2, 1, true))
        {
            setErrorStringLocked("SX1262 DIO2 RF switch enable failed: " +
                                 describe_radio_config(config_));
            return false;
        }
    }

    const std::uint8_t standby = kStandbyRc;
    if (!writeCommandLocked(kCmdSetStandby, &standby, 1, true))
    {
        setErrorStringLocked("SX1262 standby command failed after power/reset: " +
                             describe_radio_config(config_));
        return false;
    }
    sleep_ms(2);
    return true;
#endif
}

bool Sx126xRadio::readStatusLocked(std::uint8_t* out_status)
{
    if (out_status == nullptr)
    {
        setErrorLocked("invalid SX1262 status read");
        return false;
    }
    if (!waitReadyLocked()) return false;
    const std::uint8_t tx[2] = {kCmdGetStatus, 0x00};
    std::uint8_t rx[2] = {};
    if (!transferLocked(tx, rx, sizeof(tx)))
    {
        return false;
    }
    *out_status = rx[1];
    return waitReadyLocked();
}

bool Sx126xRadio::probeLocked()
{
    std::uint8_t status = 0;
    const bool have_status = readStatusLocked(&status);

    std::uint8_t original_ocp = 0;
    if (!readRegisterLocked(kRegOcpConfiguration, &original_ocp, 1))
    {
        setErrorStringLocked("SX1262 probe failed: OCP register read failed; status=" +
                             (have_status ? hex_bytes(&status, 1)
                                          : std::string("unavailable")) +
                             " " + describe_radio_config(config_));
        return false;
    }

    const std::uint8_t probe_ocp =
        static_cast<std::uint8_t>(original_ocp ^ 0x01U);
    if (!writeRegisterLocked(kRegOcpConfiguration, &probe_ocp, 1))
    {
        setErrorStringLocked("SX1262 probe failed: OCP test write failed; status=" +
                             (have_status ? hex_bytes(&status, 1)
                                          : std::string("unavailable")) +
                             " ocp=" + hex_bytes(&original_ocp, 1) +
                             " " + describe_radio_config(config_));
        return false;
    }

    std::uint8_t readback_ocp = 0;
    const bool readback_ok =
        readRegisterLocked(kRegOcpConfiguration, &readback_ocp, 1);
    (void)writeRegisterLocked(kRegOcpConfiguration, &original_ocp, 1);

    if (!readback_ok || readback_ocp != probe_ocp)
    {
        setErrorStringLocked("SX1262 probe failed: OCP readback mismatch; status=" +
                             (have_status ? hex_bytes(&status, 1)
                                          : std::string("unavailable")) +
                             " original=" + hex_bytes(&original_ocp, 1) +
                             " wrote=" + hex_bytes(&probe_ocp, 1) +
                             " read=" + hex_bytes(&readback_ocp, 1) +
                             " busy=" +
                             std::to_string(busy_fd_ >= 0
                                                ? get_gpio_value(busy_fd_)
                                                : -1) +
                             " irq=" +
                             std::to_string(irq_fd_ >= 0
                                                ? get_gpio_value(irq_fd_)
                                                : -1) +
                             " " + describe_radio_config(config_));
        return false;
    }

    return true;
}

bool Sx126xRadio::setPacketTypeLocked(std::uint8_t packet_type)
{
    if (packet_type_ == packet_type)
    {
        return true;
    }
    if (!writeCommandLocked(kCmdSetPacketType, &packet_type, 1, true))
    {
        return false;
    }
    packet_type_ = packet_type;
    return true;
}

bool Sx126xRadio::setRfFrequencyLocked(float freq_mhz)
{
    if (!std::isfinite(freq_mhz) || freq_mhz < 150 || freq_mhz > 960)
    {
        setErrorLocked("SX126x frequency is outside 150-960 MHz");
        return false;
    }
    const auto calibration = detail::sx126x_image_calibration(freq_mhz);
    if (image_calibration_ != calibration)
    {
        if (!writeCommandLocked(kCmdCalibrateImage, calibration.data(), calibration.size(), true)) return false;
        image_calibration_ = calibration;
    }

    const std::uint32_t raw = rf_frequency_raw(freq_mhz);
    const std::uint8_t data[4] = {
        static_cast<std::uint8_t>((raw >> 24) & 0xFF),
        static_cast<std::uint8_t>((raw >> 16) & 0xFF),
        static_cast<std::uint8_t>((raw >> 8) & 0xFF),
        static_cast<std::uint8_t>(raw & 0xFF),
    };
    if (!writeCommandLocked(kCmdSetRfFrequency, data, sizeof(data), true))
    {
        return false;
    }
    freq_mhz_ = freq_mhz;
    return true;
}

bool Sx126xRadio::setTxPowerLocked(std::int8_t tx_power)
{
    const std::int8_t clipped = std::clamp<std::int8_t>(tx_power, -9, 22);
    std::uint8_t ocp = 0;
    (void)readRegisterLocked(kRegOcpConfiguration, &ocp, 1);
    const std::uint8_t pa_config[4] = {
        0x04,
        0x07,
        kPaConfigDeviceSelSx1262,
        kPaConfigPaLut,
    };
    if (!writeCommandLocked(kCmdSetPaConfig, pa_config, sizeof(pa_config), true))
    {
        return false;
    }
    const std::uint8_t tx_params[2] = {
        static_cast<std::uint8_t>(clipped),
        kPaRamp200u,
    };
    const bool ok = writeCommandLocked(kCmdSetTxParams, tx_params, sizeof(tx_params), true);
    (void)writeRegisterLocked(kRegOcpConfiguration, &ocp, 1);
    return ok;
}

bool Sx126xRadio::setDioIrqParamsLocked(std::uint16_t irq_mask,
                                        std::uint16_t dio1_mask)
{
    const std::uint8_t data[8] = {
        static_cast<std::uint8_t>((irq_mask >> 8) & 0xFF),
        static_cast<std::uint8_t>(irq_mask & 0xFF),
        static_cast<std::uint8_t>((dio1_mask >> 8) & 0xFF),
        static_cast<std::uint8_t>(dio1_mask & 0xFF),
        0x00,
        0x00,
        0x00,
        0x00,
    };
    return writeCommandLocked(kCmdSetDioIrqParams, data, sizeof(data), true);
}

bool Sx126xRadio::clearIrqLocked(std::uint16_t flags)
{
    const std::uint8_t data[2] = {
        static_cast<std::uint8_t>((flags >> 8) & 0xFF),
        static_cast<std::uint8_t>(flags & 0xFF),
    };
    return writeCommandLocked(kCmdClearIrqStatus, data, sizeof(data), true);
}

bool Sx126xRadio::setBufferBaseLocked(std::uint8_t tx_base,
                                      std::uint8_t rx_base)
{
    const std::uint8_t data[2] = {tx_base, rx_base};
    return writeCommandLocked(kCmdSetBufferBaseAddress, data, sizeof(data), true);
}

bool Sx126xRadio::setRxLocked(std::uint32_t timeout_raw)
{
    const std::uint8_t data[3] = {
        static_cast<std::uint8_t>((timeout_raw >> 16) & 0xFF),
        static_cast<std::uint8_t>((timeout_raw >> 8) & 0xFF),
        static_cast<std::uint8_t>(timeout_raw & 0xFF),
    };
    return writeCommandLocked(kCmdSetRx, data, sizeof(data), true);
}

bool Sx126xRadio::setTxLocked(std::uint32_t timeout_raw)
{
    const std::uint8_t data[3] = {
        static_cast<std::uint8_t>((timeout_raw >> 16) & 0xFF),
        static_cast<std::uint8_t>((timeout_raw >> 8) & 0xFF),
        static_cast<std::uint8_t>(timeout_raw & 0xFF),
    };
    return writeCommandLocked(kCmdSetTx, data, sizeof(data), true);
}

bool Sx126xRadio::configureLoRaLocked(const Sx126xLoRaConfig& config)
{
    if (!setPacketTypeLocked(kPacketTypeLoRa))
    {
        return false;
    }
    const std::uint8_t standby_mode = kStandbyRc;
    if (!writeCommandLocked(kCmdSetStandby, &standby_mode, 1, true))
    {
        return false;
    }
    if (!setRfFrequencyLocked(config.freq_mhz) ||
        !setTxPowerLocked(config.tx_power_dbm))
    {
        return false;
    }

    const std::uint8_t mod[4] = {
        config.sf,
        map_lora_bw(config.bw_khz),
        map_lora_cr(config.cr),
        calc_ldro(config.sf, config.bw_khz),
    };
    if (!writeCommandLocked(kCmdSetModulationParams, mod, sizeof(mod), true))
    {
        return false;
    }

    const std::uint8_t packet[6] = {
        static_cast<std::uint8_t>((config.preamble_len >> 8) & 0xFF),
        static_cast<std::uint8_t>(config.preamble_len & 0xFF),
        kLoRaHeaderExplicit,
        0xFF,
        config.crc_len ? kLoRaCrcOn : kLoRaCrcOff,
        kLoRaIqStandard,
    };
    if (!writeCommandLocked(kCmdSetPacketParams, packet, sizeof(packet), true))
    {
        return false;
    }

    const std::uint8_t sync[2] = {
        static_cast<std::uint8_t>((config.sync_word & 0xF0) | 0x04),
        static_cast<std::uint8_t>(((config.sync_word & 0x0F) << 4) | 0x04),
    };
    return writeRegisterLocked(kRegLoraSyncWordMsb, sync, sizeof(sync));
}

std::uint32_t Sx126xRadio::getIrqFlagsLocked()
{
    std::uint8_t irq[2] = {};
    if (!readCommandLocked(kCmdGetIrqStatus, nullptr, 0, irq, sizeof(irq), true))
    {
        return 0;
    }
    return (static_cast<std::uint32_t>(irq[0]) << 8) | irq[1];
}

int Sx126xRadio::getPacketLengthLocked(std::uint8_t* out_offset)
{
    std::uint8_t status[2] = {};
    if (!readCommandLocked(kCmdGetRxBufferStatus,
                           nullptr,
                           0,
                           status,
                           sizeof(status),
                           true))
    {
        return -1;
    }
    if (out_offset != nullptr)
    {
        *out_offset = status[1];
    }
    return static_cast<int>(status[0]);
}

int Sx126xRadio::readPacketLocked(std::uint8_t offset,
                                  std::uint8_t* buffer,
                                  std::size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        return -1;
    }
    return readCommandLocked(kCmdReadBuffer, &offset, 1, buffer, size, true) ? 0
                                                                             : -1;
}

bool Sx126xRadio::readPacketStatusLocked(float* out_rssi_dbm,
                                         float* out_snr_db)
{
    std::uint8_t status[3] = {};
    if (!readCommandLocked(kCmdGetPacketStatus,
                           nullptr,
                           0,
                           status,
                           sizeof(status),
                           true))
    {
        return false;
    }
    if (out_rssi_dbm != nullptr)
    {
        *out_rssi_dbm = static_cast<float>(status[packet_type_ == 0x00 ? 2 : 0]) / -2.0f;
    }
    if (out_snr_db != nullptr)
    {
        *out_snr_db = packet_type_ == 0x00 ? 0.0f : static_cast<float>(static_cast<std::int8_t>(status[1])) / 4.0f;
    }
    return true;
}

void Sx126xRadio::setErrorLocked(const char* error)
{
    std::snprintf(last_error_, sizeof(last_error_), "%s",
                  error == nullptr ? "" : error);
}

void Sx126xRadio::setErrorStringLocked(const std::string& error)
{
    setErrorLocked(error.c_str());
}

} // namespace platform::linux_runtime
