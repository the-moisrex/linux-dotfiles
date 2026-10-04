// my_msi_driver.cpp — userspace driver for the MSI MEG Coreliquid S360 AIO.
//
// Build:
//   g++ my_msi_driver.cpp -lhidapi-hidraw -lsensors -o my_msi_driver

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <hidapi/hidapi.h>
#include <sensors/sensors.h>

namespace {

using namespace std::chrono_literals;

static constexpr unsigned short Vid = 0x0db0;
static constexpr unsigned short Pid = 0x6a05;
static constexpr std::size_t ReportSize = 65;
static constexpr int DummyFreqMhz = 3000; // AIO ignores frequency; fixed dummy
static constexpr auto PollInterval = 2s;

enum class FanMode : int {
  Silent = 0,
  Balance = 1,
  Game = 2,
  Customize = 3, // not supported by the device
  Default = 4,
  Smart = 5,
};

volatile std::sig_atomic_t g_stop = 0;

void on_sigterm(int) { g_stop = 1; }

// Locate coretemp's package reading (chip "coretemp", feature "temp1").
bool find_package_temp(const sensors_chip_name **chip_out,
                       const sensors_subfeature **sub_out) {
  int nr = 0;
  const sensors_chip_name *chip;
  while ((chip = sensors_get_detected_chips(nullptr, &nr)) != nullptr) {
    if (std::strcmp(chip->prefix, "coretemp") != 0) {
      continue;
    }
    int nf = 0;
    const sensors_feature *feature;
    while ((feature = sensors_get_features(chip, &nf)) != nullptr) {
      if (feature->type != SENSORS_FEATURE_TEMP ||
          std::strcmp(feature->name, "temp1") != 0) {
        continue;
      }
      int ns = 0;
      const sensors_subfeature *sub;
      while ((sub = sensors_get_all_subfeatures(chip, feature, &ns)) !=
             nullptr) {
        if (sub->type == SENSORS_SUBFEATURE_TEMP_INPUT) {
          *chip_out = chip;
          *sub_out = sub;
          return true;
        }
      }
    }
  }
  return false;
}

// Feed CPU temperature to the AIO until signalled to stop.
void monitor_cpu_temperature(hid_device *handle) {
  if (sensors_init(nullptr) != 0) {
    std::fprintf(stderr, "Error while initializing libsensors\n");
    return;
  }

  const sensors_chip_name *chip = nullptr;
  const sensors_subfeature *sub = nullptr;
  if (!find_package_temp(&chip, &sub)) {
    std::fprintf(stderr, "coretemp package reading not found\n");
    sensors_cleanup();
    return;
  }

  std::array<unsigned char, ReportSize> buf{};
  buf[0] = 0xD0;
  buf[1] = 0x85;
  buf[2] = DummyFreqMhz & 0xFF;
  buf[3] = (DummyFreqMhz >> 8) & 0xFF;

  while (!g_stop) {
    double temp = 0.0;
    if (sensors_get_value(chip, sub->number, &temp) == 0) {
      const int itemp = static_cast<int>(temp);
      buf[4] = itemp & 0xFF;
      buf[5] = (itemp >> 8) & 0xFF;
      hid_write(handle, buf.data(), buf.size());
    }
    std::this_thread::sleep_for(PollInterval);
  }

  sensors_cleanup();
}

// Commit a predefined cooling curve to all five fan/pump slots.
void set_fan_mode(hid_device *handle, int fan_mode) {
  std::array<unsigned char, ReportSize> buf{};
  buf[0] = 0xD0;
  buf[1] = 0x40;
  buf[2] = fan_mode;
  buf[10] = fan_mode;
  buf[18] = fan_mode;
  buf[26] = fan_mode;
  buf[34] = fan_mode;
  hid_write(handle, buf.data(), buf.size());
  buf[1] = 0x41;
  hid_write(handle, buf.data(), buf.size());
}

void print_usage() {
  std::puts("Allowed modes:\n"
            "0 : silent\n"
            "1 : balance\n"
            "2 : game\n"
            "4 : default (constant)\n"
            "5 : smart");
}

} // namespace

int main(int argc, char *argv[]) {
  int fan_mode = static_cast<int>(FanMode::Smart);
  bool start_daemon = false;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-M") == 0) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "-M requires a mode argument\n");
        print_usage();
        return 1;
      }
      fan_mode = std::atoi(argv[++i]);
      if (fan_mode < 0 || fan_mode > 5 || fan_mode == 3) {
        print_usage();
        return 1;
      }
    } else if (std::strcmp(argv[i], "startd") == 0) {
      start_daemon = true;
    }
  }

  if (hid_init() != 0) {
    std::fprintf(stderr, "hid_init failed\n");
    return 1;
  }

  hid_device *handle = hid_open(Vid, Pid, nullptr);
  if (handle == nullptr) {
    std::fprintf(stderr,
                 "Cannot open AIO %04x:%04x — device busy or "
                 "insufficient permissions\n",
                 Vid, Pid);
    hid_exit();
    return 1;
  }

  set_fan_mode(handle, fan_mode);

  if (start_daemon) {
    std::signal(SIGTERM, on_sigterm);
    monitor_cpu_temperature(handle);
  }

  hid_close(handle);
  hid_exit();
  return 0;
}
