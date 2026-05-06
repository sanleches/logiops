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

/*
 * File: backend/hidpp/Device.cpp
 *
 * HID++ transport wrapper that promotes a raw Logitech node into a device that
 * can send and receive HID++ reports, verify device stability, and translate
 * transport-level responses into higher-level errors and device metadata.
 */

#include <backend/hidpp20/Device.h>
#include <backend/hidpp20/features/Root.h>
#include <backend/hidpp20/features/DeviceName.h>
#include <backend/hidpp20/Feature.h>
#include <backend/hidpp10/Receiver.h>
#include <backend/Error.h>
#include <cassert>
#include <utility>

using namespace logid::backend;
using namespace logid::backend::hidpp;

using namespace std::chrono;

// Purpose: Describe why a raw node could not become a HID++ device.
// Inputs: Stored reason code.
// Outputs: Human-readable `what()` text.
// Used by: device discovery and retry logic.
const char* Device::InvalidDevice::what() const noexcept {
    switch (_reason) {
        case NoHIDPPReport:
            return "Invalid HID++ device";
        case InvalidRawDevice:
            return "Invalid raw device";
        case Asleep:
            return "Device asleep";
        case VirtualNode:
            return "Virtual device";
        default:
            return "Invalid device";
    }
}

// Purpose: Expose the failure reason for classification.
// Inputs: None.
// Outputs: A `Reason` enum value.
// Used by: callers that need retry vs skip decisions.
Device::InvalidDevice::Reason Device::InvalidDevice::code() const noexcept {
    return _reason;
}

// Purpose: Build the generic HID++ transport wrapper from a path.
// Inputs: Raw path, device index, device monitor, and timeout.
// Outputs: A transport object ready for probing.
// Used by: direct-device construction paths.
Device::Device(const std::string& path, DeviceIndex index,
               const std::shared_ptr<raw::DeviceMonitor>& monitor, double timeout) :
        io_timeout(duration_cast<milliseconds>(
                duration<double, std::milli>(timeout))),
        _raw_device(raw::RawDevice::make(path, monitor)),
        _receiver(nullptr), _path(path), _index(index) {
}

// Purpose: Build the generic HID++ transport wrapper from an open raw device.
// Inputs: `RawDevice`, device index, and timeout.
// Outputs: A transport object ready for probing.
// Used by: code that already owns the fd.
Device::Device(std::shared_ptr<raw::RawDevice> raw_device, DeviceIndex index,
               double timeout) :
        io_timeout(duration_cast<milliseconds>(
                duration<double, std::milli>(timeout))),
        _raw_device(std::move(raw_device)), _receiver(nullptr),
        _path(_raw_device->rawPath()), _index(index) {
}

// Purpose: Build the generic HID++ transport wrapper for a receiver event.
// Inputs: Receiver, connection event, and timeout.
// Outputs: A transport object attached to the receiver's raw device.
// Used by: `Receiver::addDevice()`.
Device::Device(const std::shared_ptr<hidpp10::Receiver>& receiver,
               hidpp::DeviceConnectionEvent event, double timeout) :
        io_timeout(duration_cast<milliseconds>(
                duration<double, std::milli>(timeout))),
        _raw_device(receiver->rawDevice()), _receiver(receiver),
        _path(receiver->rawDevice()->rawPath()), _index(event.index) {
    // Device will throw an error soon, just do it now
    if (!event.linkEstablished)
        throw InvalidDevice(InvalidDevice::Asleep);

    if (!event.fromTimeoutCheck)
        _pid = event.pid;
    else
        _pid = receiver->getPairingInfo(_index).pid;
}

// Purpose: Build the generic HID++ transport wrapper for an existing slot.
// Inputs: Receiver, slot index, and timeout.
// Outputs: A transport object attached to the receiver's raw device.
// Used by: paired-slot construction paths.
Device::Device(const std::shared_ptr<hidpp10::Receiver>& receiver,
               DeviceIndex index, double timeout) :
        io_timeout(duration_cast<milliseconds>(
                duration<double, std::milli>(timeout))),
        _raw_device(receiver->rawDevice()),
        _receiver(receiver), _path(receiver->rawDevice()->rawPath()),
        _index(index) {
    _pid = receiver->getPairingInfo(_index).pid;
}

// Return the raw device path.
const std::string& Device::devicePath() const {
    return _path;
}

// Return the device index used in HID++ headers.
DeviceIndex Device::deviceIndex() const {
    return _index;
}

// Return the discovered HID++ version.
const std::tuple<uint8_t, uint8_t>& Device::version() const {
    return _version;
}


// Purpose: Promote the raw transport into a live HID++ device.
// Inputs: The already-open raw device state.
// Outputs: Report handlers, version detection, and device metadata.
// References: `getSupportedReports()` and `handleEvent()`.
void Device::_setupReportsAndInit() {
    _event_handlers = std::make_shared<EventHandlerList<Device>>();

    supported_reports = getSupportedReports(_raw_device->reportDescriptor());
    if (!supported_reports)
        throw InvalidDevice(InvalidDevice::NoHIDPPReport);

    // hid_logitech_dj creates virtual /dev/hidraw nodes for receiver devices; ignore them.
    // They are not the real physical device, so we skip them here.
    if (_raw_device->isSubDevice())
        throw InvalidDevice(InvalidDevice::VirtualNode);

    _raw_handler = _raw_device->addEventHandler(
            {[index = _index](
                    const std::vector<uint8_t>& report) -> bool {
                return (report[Offset::Type] == Report::Type::Short ||
                        report[Offset::Type] == Report::Type::Long) &&
                       (report[Offset::DeviceIndex] == index);
            },
        [self_weak = _self](const std::vector<uint8_t>& report) -> void {
                  Report _report(report);
                  if(auto self = self_weak.lock())
                      self->handleEvent(_report);
              }});

    _init();
}

// Purpose: Discover protocol version and cache stable identity fields.
// Inputs: None.
// Outputs: Version, name, and product ID.
// Used by: feature setup and IPC metadata.
void Device::_init() {
    try {
        hidpp20::Root root(this);
        _version = root.getVersion();
    } catch (hidpp10::Error& e) {
        // Valid HID++ 1.0 devices should send an InvalidSubID error.
        if (e.code() != hidpp10::Error::InvalidSubID)
            throw;

        // HID++ 2.0 is not supported, assume HID++ 1.0
        _version = std::make_tuple(1, 0);
    } catch (hidpp20::Error& e) {
        // Should never happen, device not ready?
        throw DeviceNotReady();
    }

    // Check stability before continuing. This avoids configuring a device that
    // only responded briefly and then disappeared or fell asleep.
    if (std::get<0>(_version) >= 2) {
        if (!isStable20()) {
            throw DeviceNotReady();
        }
    } else {
        if (!isStable10()) {
            throw DeviceNotReady();
        }
    }

    if (!_receiver) {
        _pid = _raw_device->productId();
        if (std::get<0>(_version) >= 2) {
            try {
                hidpp20::DeviceName deviceName(this);
                _name = deviceName.getName();
            } catch (hidpp20::UnsupportedFeature& e) {
                _name = _raw_device->name();
            }
        } else {
            _name = _raw_device->name();
        }
    } else {
        if (std::get<0>(_version) >= 2) {
            try {
                hidpp20::DeviceName deviceName(this);
                _name = deviceName.getName();
            } catch (hidpp20::UnsupportedFeature& e) {
                _name = _receiver->getDeviceName(_index);
            }
        } else {
            _name = _receiver->getDeviceName(_index);
        }
    }
}

// Purpose: Register a callback for incoming HID++ reports.
// Inputs: One callable handler.
// Outputs: An RAII lock that keeps the handler alive.
// Used by: feature wrappers and protocol layers.
EventHandlerLock<Device> Device::addEventHandler(EventHandler handler) {
    return {_event_handlers, _event_handlers->add(std::move(handler))};
}

// Purpose: Dispatch one incoming report to the event handlers.
// Inputs: A decoded HID++ report.
// Outputs: Callback execution unless the report matched an in-flight request.
// Used by: the raw report handler.
void Device::handleEvent(Report& report) {
    if (responseReport(report))
        return;

    _event_handlers->run_all(report);
}

// Purpose: Send a HID++ request and wait for the matching response.
// Inputs: One report to transmit.
// Outputs: Matching response report or protocol error/timeout.
// References: `_send_mutex`, `_response_mutex`, and `_response_cv`.
Report Device::sendReport(const Report& report) {
    /* Must complete transaction before next send */
    std::lock_guard send_lock(_send_mutex);
    _sent_sub_id = report.subId();
    _sent_address = report.address();
    std::unique_lock lock(_response_mutex);
    _sendReport(report);

    bool valid = _response_cv.wait_for(
            lock, io_timeout, [this]() {
                return _response.has_value();
            });

    if (!valid) {
        _sent_sub_id.reset();
        throw TimeoutError();
    }

    Response response = _response.value();
    _response.reset();
    _sent_sub_id.reset();
    _sent_address.reset();

    if (std::holds_alternative<Report>(response)) {
        return std::get<Report>(response);
    } else if (std::holds_alternative<Report::Hidpp10Error>(response)) {
        auto error = std::get<Report::Hidpp10Error>(response);
        throw hidpp10::Error(error.error_code, error.device_index);
    } else if (std::holds_alternative<Report::Hidpp20Error>(response)) {
        auto error = std::get<Report::Hidpp20Error>(response);
        throw hidpp20::Error(error.error_code, error.device_index);
    }

    // Should not be reached
    throw std::runtime_error("unhandled variant type");
}

// Purpose: Check whether a report answers the current in-flight request.
// Inputs: One incoming report.
// Outputs: `true` if the report was consumed as a response.
// Used by: `handleEvent()`.
bool Device::responseReport(const Report& report) {
    std::lock_guard lock(_response_mutex);
    Response response = report;
    uint8_t sub_id;
    uint8_t address;

    Report::Hidpp10Error hidpp10_error{};
    Report::Hidpp20Error hidpp20_error{};
    if (report.isError10(hidpp10_error)) {
        sub_id = hidpp10_error.sub_id;
        address = hidpp10_error.address;
        response = hidpp10_error;
    } else if (report.isError20(hidpp20_error)) {
        sub_id = hidpp20_error.feature_index;
        address = (hidpp20_error.function << 4) | (hidpp20_error.software_id & 0x0f);
    } else {
        sub_id = report.subId();
        address = report.address();
    }

    if (sub_id == _sent_sub_id && address == _sent_address) {
        _response = response;
        _response_cv.notify_all();
        return true;
    } else {
        return false;
    }

}

// Return the underlying raw device.
const std::shared_ptr<raw::RawDevice>& Device::rawDevice() const {
    return _raw_device;
}

// Purpose: Apply HID++ fixups and write the request to hardware.
// Inputs: One report to send.
// Outputs: Raw report bytes written to the device.
// Used by: `sendReport()` and `sendReportNoACK()`.
void Device::_sendReport(Report report) {
    reportFixup(report);
    _raw_device->sendReport(report.rawReport());
}

// Purpose: Send a HID++ request without waiting for a reply.
// Inputs: One report to transmit.
// Outputs: Raw report bytes written to the device.
// Used by: one-way operations and host-switch commands.
void Device::sendReportNoACK(const Report& report) {
    std::lock_guard lock(_send_mutex);
    _sendReport(report);
}

// Purpose: Treat HID++ 1.0 devices as stable once detected.
// Inputs: None.
// Outputs: `true`.
// Used by: `_init()`.
bool Device::isStable10() {
    return true;
}

// Purpose: Ping the HID++ 2.0 root feature to confirm liveness.
// Inputs: None.
// Outputs: `true` if the ping round-trip succeeds.
// Used by: `_init()`.
bool Device::isStable20() {
    static const std::string ping_seq = "hello";

    hidpp20::Root root(this);

    try {
        for (auto c: ping_seq) {
            if (root.ping(c) != c)
                return false;
        }
    } catch (std::exception& e) {
        return false;
    }

    return true;
}

void Device::reportFixup(Report& report) const {
    // HID++ 2.0 devices often only support one report style; rewrite short
    // reports to long reports when needed so the firmware accepts them.
    switch (report.type()) {
        case Report::Type::Short:
            if (!(supported_reports & ShortReportSupported))
                report.setType(Report::Type::Long);
            break;
        case Report::Type::Long:
            /* Report can be truncated, but that isn't a good idea. */
            assert(supported_reports & LongReportSupported);
    }
}

const std::string& Device::name() const {
    return _name;
}

uint16_t Device::pid() const {
    return _pid;
}
