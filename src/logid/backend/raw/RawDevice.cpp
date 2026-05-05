/*
 * Copyright 2019-2023 PixlOne
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <backend/raw/RawDevice.h>
#include <backend/raw/DeviceMonitor.h>
#include <backend/raw/IOMonitor.h>
#include <util/log.h>

#include <string>
#include <system_error>
#include <utility>
#include <regex>

extern "C"
{
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
}

using namespace logid::backend::raw;
using namespace logid::backend;
using namespace std::chrono;

static constexpr int max_write_tries = 8;

static const std::regex virtual_path_regex(R"~((.*\/)(.*:)([0-9]+))~");

// Open the hidraw node with non-blocking read/write access.
// Non-blocking mode keeps the event loop from hanging if the device disappears.
int get_fd(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd == -1)
        throw std::system_error(errno, std::system_category(),
                                "RawDevice open failed");

    return fd;
}

// Read the kernel-reported vendor/product and bus metadata.
// This is the cheap probe that tells us what kind of device we are looking at.
RawDevice::dev_info get_dev_info(int fd) {
    hidraw_devinfo dev_info{};
    if (-1 == ::ioctl(fd, HIDIOCGRAWINFO, &dev_info)) {
        int err = errno;
        ::close(fd);
        throw std::system_error(err, std::system_category(),
                                "RawDevice HIDIOCGRAWINFO failed");
    }

    RawDevice::BusType type = RawDevice::OtherBus;

    switch (dev_info.bustype) {
        case BUS_USB:
            type = RawDevice::USB;
            break;
        case BUS_BLUETOOTH:
            type = RawDevice::Bluetooth;
            break;
        default:;
    }


    return {
            .vid = dev_info.vendor,
            .pid = dev_info.product,
            .bus_type = type
    };
}

// Read the physical path string used to detect sub-devices.
// Logitech receivers expose synthetic hidraw nodes that should usually be ignored.
std::string get_phys(int fd) {
    ssize_t len;
    char buf[256];
    if (-1 == (len = ::ioctl(fd, HIDIOCGRAWPHYS(sizeof(buf)), buf))) {
        int err = errno;
        ::close(fd);
        throw std::system_error(err, std::system_category(),
                                "RawDevice HIDIOCGRAWPHYS failed");
    }

    return {buf, static_cast<size_t>(len) - 1};
}

// Read the device name string from the hidraw node.
// This is the fallback name used when HID++ name lookup is unavailable.
std::string get_name(int fd) {
    ssize_t len;
    char name_buf[256];
    if (-1 == (len = ::ioctl(fd, HIDIOCGRAWNAME(sizeof(name_buf)), name_buf))) {
        int err = errno;
        ::close(fd);
        throw std::system_error(err, std::system_category(),
                                "RawDevice HIDIOCGRAWNAME failed");
    }
    return {name_buf, static_cast<size_t>(len) - 1};
}

// Open the raw device and cache the identifying information needed by higher layers.
// The constructor intentionally does the minimum needed to identify the device;
// the rest of the setup happens after it is handed off to the I/O monitor.
RawDevice::RawDevice(std::string path, const std::shared_ptr<DeviceMonitor>& monitor) :
        _valid(true), _path(std::move(path)), _fd(get_fd(_path)),
        _dev_info(get_dev_info(_fd)), _name(get_name(_fd)),
        _report_desc(getReportDescriptor(_fd)), _io_monitor(monitor->ioMonitor()),
        _event_handlers(std::make_shared<EventHandlerList<RawDevice>>()) {

    if (busType() == USB) {
        auto phys = get_phys(_fd);
        _sub_device = std::regex_match(phys, virtual_path_regex);
    }
}

// Register read/error callbacks with the shared I/O monitor.
// The monitor tells us when the fd is readable, hung up, or has an error.
void RawDevice::_ready() {
    _io_monitor->add(_fd, {
            [self_weak = _self]() {
                if (auto self = self_weak.lock())
                    self->_readReports();
            },
            [self_weak = _self]() {
                if (auto self = self_weak.lock())
                    self->_valid = false;
            },
            [self_weak = _self]() {
                if (auto self = self_weak.lock())
                    self->_valid = false;
            }
    });
}

// Release the I/O monitor registration and close the file descriptor.
RawDevice::~RawDevice() noexcept {
    _io_monitor->remove(_fd);
    ::close(_fd);
}

// Return the path used to open this raw device.
const std::string& RawDevice::rawPath() const {
    return _path;
}

// Return the human-readable kernel name.
const std::string& RawDevice::name() const {
    return _name;
}

[[maybe_unused]]
// Return the vendor id exposed by the kernel.
int16_t RawDevice::vendorId() const {
    return _dev_info.vid;
}

// Return the product id exposed by the kernel.
int16_t RawDevice::productId() const {
    return _dev_info.pid;
}

// Return the detected bus type.
RawDevice::BusType RawDevice::busType() const {
    return _dev_info.bus_type;
}

// Detect whether this device is a USB sub-device exposed via a virtual path.
bool RawDevice::isSubDevice() const {
    return _sub_device;
}

// Read the full HID report descriptor from a path.
std::vector<uint8_t> RawDevice::getReportDescriptor(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd == -1)
        throw std::system_error(errno, std::system_category(),
                                "open failed");

    auto report_desc = getReportDescriptor(fd);
    ::close(fd);
    return report_desc;
}

// Read the full HID report descriptor from an already-open descriptor.
std::vector<uint8_t> RawDevice::getReportDescriptor(int fd) {
    hidraw_report_descriptor report_desc{};
    if (-1 == ::ioctl(fd, HIDIOCGRDESCSIZE, &report_desc.size)) {
        int err = errno;
        ::close(fd);
        throw std::system_error(err, std::system_category(),
                                "RawDevice HIDIOCGRDESCSIZE failed");
    }
    if (-1 == ::ioctl(fd, HIDIOCGRDESC, &report_desc)) {
        int err = errno;
        ::close(fd);
        throw std::system_error(err, std::system_category(),
                                "RawDevice HIDIOCGRDESC failed");
    }
    return {report_desc.value, report_desc.value + report_desc.size};
}

// Return the cached report descriptor used by the higher-level HID++ parsers.
const std::vector<uint8_t>& RawDevice::reportDescriptor() const {
    return _report_desc;
}

// Send one HID report, retrying transient pipe errors where appropriate.
// The retry loop helps with devices that temporarily drop writes while waking up.
void RawDevice::sendReport(const std::vector<uint8_t>& report) {
    if (!_valid) {
        // We could throw an error here, but this will likely be closed soon.
        return;
    }

    if (logid::global_loglevel <= LogLevel::RAWREPORT) {
        printf("[RAWREPORT] %s OUT: ", _path.c_str());
        for (auto& i: report)
            printf("%02x ", i);
        printf("\n");
    }


    for (int i = 0; i < max_write_tries && write(_fd, report.data(), report.size()) == -1; ++i) {
        auto err = errno;
        if (err != EPIPE)
            throw std::system_error(err, std::system_category(),
                                    "sendReport write failed");
    }
}

// Register an event handler and keep it alive via an RAII token.
EventHandlerLock<RawDevice> RawDevice::addEventHandler(RawEventHandler handler) {
    return {_event_handlers, _event_handlers->add(std::forward<RawEventHandler>(handler))};
}

// Read all available reports and forward them to registered handlers.
// Each raw report is copied into a vector so higher layers can parse it safely.
void RawDevice::_readReports() {
    uint8_t buf[max_data_length];
    ssize_t len;

    while (-1 != (len = ::read(_fd, buf, max_data_length))) {
        assert(len <= max_data_length);
        std::vector<uint8_t> report(buf, buf + len);

        if (logid::global_loglevel <= LogLevel::RAWREPORT) {
            printf("[RAWREPORT] %s IN:  ", _path.c_str());
            for (auto& i: report)
                printf("%02x ", i);
            printf("\n");
        }

        _handleEvent(report);
    }
}

// Dispatch one report to the active callback list.
void RawDevice::_handleEvent(const std::vector<uint8_t>& report) {
    _event_handlers->run_all(report);
}
