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
 * File: DeviceMonitor.cpp
 *
 * Hotplug monitor for hidraw devices. This layer owns the shared raw I/O
 * monitor, subscribes to udev notifications, and retries devices that appear
 * before they are fully ready so the higher-level daemon code sees stable
 * device objects rather than transient kernel state.
 */

#include <backend/raw/DeviceMonitor.h>
#include <backend/raw/IOMonitor.h>
#include <backend/raw/RawDevice.h>
#include <backend/hidpp/Device.h>
#include <backend/Error.h>
#include <util/task.h>
#include <util/log.h>
#include <system_error>

extern "C"
{
#include <libudev.h>
}

using namespace logid;
using namespace logid::backend::raw;

// Purpose: Initialize udev and the shared raw-device monitor.
// Inputs: None.
// Outputs: A hotplug monitor ready to enumerate hidraw nodes.
// Used by: `DeviceManager` and other raw backends.
DeviceMonitor::DeviceMonitor() : _io_monitor(std::make_shared<IOMonitor>()),
                                  _ready(false) {
    int ret;
    _udev_context = udev_new();
    if (!_udev_context)
        throw std::runtime_error("udev_new failed");

    _udev_monitor = udev_monitor_new_from_netlink(_udev_context,
                                                  "udev");
    if (!_udev_monitor) {
        if (_udev_context)
            udev_unref(_udev_context);
        throw std::runtime_error("udev_monitor_new_from_netlink failed");
    }

    ret = udev_monitor_filter_add_match_subsystem_devtype(
            _udev_monitor, "hidraw", nullptr);
    if (0 != ret) {
        if (_udev_monitor)
            udev_monitor_unref(_udev_monitor);
        if (_udev_context)
            udev_unref(_udev_context);
        throw std::system_error(
                -ret, std::system_category(),
                "udev_monitor_filter_add_match_subsystem_devtype");
    }

    ret = udev_monitor_enable_receiving(_udev_monitor);
    if (0 != ret) {
        if (_udev_monitor)
            udev_monitor_unref(_udev_monitor);
        if (_udev_context)
            udev_unref(_udev_context);
        throw std::system_error(-ret, std::system_category(),
                                "udev_monitor_enable_receiving");
    }

    _fd = udev_monitor_get_fd(_udev_monitor);
}

// Purpose: Tear down udev and I/O monitor state.
// Inputs: None.
// Outputs: Kernel and udev resources released.
// Used by: object destruction.
DeviceMonitor::~DeviceMonitor() {
    if (_ready)
        _io_monitor->remove(_fd);

    if (_udev_monitor)
        udev_monitor_unref(_udev_monitor);
    if (_udev_context)
        udev_unref(_udev_context);
}

// Purpose: Enable hotplug callbacks after subclass setup completes.
// Inputs: None.
// Outputs: Udev events start flowing into `_addHandler()` and `_removeHandler()`.
// Used by: derived constructors.
void DeviceMonitor::ready() {
    if (_ready)
        return;
    _ready = true;

    _io_monitor->add(_fd, {
            [self_weak = _self]() {
                if (auto self = self_weak.lock()) {
                    struct udev_device* device = udev_monitor_receive_device(self->_udev_monitor);
                    std::string action = udev_device_get_action(device);
                    std::string dev_node = udev_device_get_devnode(device);

                    if (action == "add")
                        run_task([self_weak, dev_node]() {
                            if (auto self = self_weak.lock())
                                self->_addHandler(dev_node);
                        });
                    else if (action == "remove")
                        run_task([self_weak, dev_node]() {
                            if (auto self = self_weak.lock())
                                self->_removeHandler(dev_node);
                        });

                    udev_device_unref(device);
                }
            },
            []() {
                throw std::runtime_error("udev hangup");
            },
            []() {
                throw std::runtime_error("udev error");
            }
    });
}

// Purpose: Enumerate hidraw nodes that already exist at startup.
// Inputs: None.
// Outputs: Each node is sent through the same add path as hotplug events.
// Used by: daemon startup.
void DeviceMonitor::enumerate() {
    int ret;
    struct udev_enumerate* udev_enum = udev_enumerate_new(_udev_context);
    ret = udev_enumerate_add_match_subsystem(udev_enum, "hidraw");
    if (0 != ret)
        throw std::system_error(-ret, std::system_category(),
                                "udev_enumerate_add_match_subsystem");

    ret = udev_enumerate_scan_devices(udev_enum);
    if (0 != ret)
        throw std::system_error(-ret, std::system_category(),
                                "udev_enumerate_scan_devices");

    struct udev_list_entry* udev_enum_entry;
    udev_list_entry_foreach(udev_enum_entry,
                            udev_enumerate_get_list_entry(udev_enum)) {
        const char* name = udev_list_entry_get_name(udev_enum_entry);

        struct udev_device* device = udev_device_new_from_syspath(_udev_context, name);
        if (device) {
            const char* dev_node_cstr = udev_device_get_devnode(device);
            if (dev_node_cstr) {
                const std::string dev_node {dev_node_cstr};
                udev_device_unref(device);

                _addHandler(dev_node);
            } else {
                udev_device_unref(device);
            }
        }
    }

    udev_enumerate_unref(udev_enum);
}

// Purpose: Add one hidraw node with retry/backoff.
// Inputs: Kernel path and attempt counter.
// Outputs: A device add, a deferred retry, or a logged skip.
// References: `backend::hidpp::getSupportedReports()` and `DeviceNotReady`.
void DeviceMonitor::_addHandler(const std::string& device, int tries) {
    try {
        auto supported_reports = backend::hidpp::getSupportedReports(
                RawDevice::getReportDescriptor(device));
        if (supported_reports)
            addDevice(device);
        else
            logPrintf(DEBUG, "Unsupported device %s ignored", device.c_str());
    } catch (backend::DeviceNotReady& e) {
        if (tries == max_tries) {
            logPrintf(WARN, "Failed to add device %s after %d tries. Treating as failure.",
                      device.c_str(), max_tries);
        } else {
            // Exponential backoff keeps transient startup failures from looking
            // like permanent device errors.
            std::chrono::milliseconds wait((1 << tries) * ready_backoff);
            logPrintf(DEBUG, "Failed to add device %s on try %d, backing off for %dms",
                      device.c_str(), tries + 1, wait.count());
            run_task_after([self_weak = _self, device, tries]() {
                if (auto self = self_weak.lock())
                    self->_addHandler(device, tries + 1);
            }, wait);
        }
    } catch (std::exception& e) {
        logPrintf(WARN, "Error adding device %s: %s", device.c_str(), e.what());
    }
}

// Purpose: Remove one hidraw node from the derived backend.
// Inputs: Kernel path.
// Outputs: Backend-specific teardown.
// Used by: udev remove events.
void DeviceMonitor::_removeHandler(const std::string& device) {
    try {
        removeDevice(device);
    } catch (std::exception& e) {
        logPrintf(WARN, "Error removing device %s: %s",
                  device.c_str(), e.what());
    }
}

std::shared_ptr<IOMonitor> DeviceMonitor::ioMonitor() const {
    return _io_monitor;
}
