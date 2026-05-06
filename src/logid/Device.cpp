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
 * File: Device.cpp
 *
 * High-level representation of a single Logitech device. This class bridges
 * the HID++ transport layer, the per-device profile/configuration state, the
 * feature wrappers that apply settings, and the IPC surface exposed to client
 * applications. It is the main object that turns daemon configuration into
 * device-specific behavior.
 */

#include <Device.h>
#include <DeviceManager.h>
#include <features/SmartShift.h>
#include <features/DPI.h>
#include <features/RemapButton.h>
#include <features/HiresScroll.h>
#include <features/DeviceStatus.h>
#include <features/ThumbWheel.h>
#include <backend/hidpp20/features/Reset.h>
#include <util/task.h>
#include <util/log.h>
#include <thread>
#include <utility>
#include <ipc_defs.h>

using namespace logid;
using namespace logid::backend;

// Purpose: Provide a stable short name for one device IPC node.
// Inputs: The owning `DeviceManager`.
// Outputs: A unique integer nickname that is converted to a string.
// Used by: `Device::make()` and the device IPC path builder.
DeviceNickname::DeviceNickname(const std::shared_ptr<DeviceManager>& manager) :
        _nickname(manager->newDeviceNickname()), _manager(manager) {
}

DeviceNickname::operator std::string() const {
    return std::to_string(_nickname);
}

DeviceNickname::~DeviceNickname() {
    if (auto manager = _manager.lock()) {
        std::lock_guard<std::mutex> lock(manager->_nick_lock);
        manager->_device_nicknames.erase(_nickname);
    }
}

namespace logid {
    class DeviceWrapper : public Device {
    public:
        template<typename... Args>
        explicit DeviceWrapper(Args... args) : Device(std::forward<Args>(args)...) {}
    };
}

// Purpose: Build a direct-device wrapper from a hidraw path.
// Inputs: Raw device path, HID++ index, and owning `DeviceManager`.
// Outputs: A fully wired `Device` with IPC registration and self-ownership.
// Used by: `DeviceManager::addDevice()`.
std::shared_ptr<Device> Device::make(
        std::string path, backend::hidpp::DeviceIndex index,
        std::shared_ptr<DeviceManager> manager) {
    auto ret = std::make_shared<DeviceWrapper>(std::move(path),
                                               index,
                                               std::move(manager));
    ret->_self = ret;
    ret->_ipc_node->manage(ret);
    ret->_ipc_interface = ret->_ipc_node->make_interface<IPC>(ret.get());
    return ret;
}

// Purpose: Build a device wrapper from an existing raw device.
// Inputs: Open `RawDevice`, HID++ index, and owning `DeviceManager`.
// Outputs: A fully wired `Device` with IPC registration and self-ownership.
// Used by: code paths that already probed the raw node.
std::shared_ptr<Device> Device::make(
        std::shared_ptr<backend::raw::RawDevice> raw_device,
        backend::hidpp::DeviceIndex index,
        std::shared_ptr<DeviceManager> manager) {
    auto ret = std::make_shared<DeviceWrapper>(std::move(raw_device),
                                               index,
                                               std::move(manager));
    ret->_self = ret;
    ret->_ipc_node->manage(ret);
    ret->_ipc_interface = ret->_ipc_node->make_interface<IPC>(ret.get());
    return ret;
}

// Purpose: Build a receiver-backed device wrapper.
// Inputs: `Receiver`, slot index, and owning `DeviceManager`.
// Outputs: A fully wired `Device` that uses the receiver's raw transport.
// Used by: `Receiver::addDevice()`.
std::shared_ptr<Device> Device::make(
        Receiver* receiver, backend::hidpp::DeviceIndex index,
        std::shared_ptr<DeviceManager> manager) {
    auto ret = std::make_shared<DeviceWrapper>(receiver, index, std::move(manager));
    ret->_self = ret;
    ret->_ipc_node->manage(ret);
    ret->_ipc_interface = ret->_ipc_node->make_interface<IPC>(ret.get());
    return ret;
}

Device::Device(std::string path, backend::hidpp::DeviceIndex index,
               const std::shared_ptr<DeviceManager>& manager) :
        _hidpp20(hidpp20::Device::make(path, index, manager,
                                       manager->config()->io_timeout.value_or(
                                               defaults::io_timeout))),
        _path(std::move(path)), _index(index),
        _config(_getConfig(manager, _hidpp20->name())),
        _profile_name(ipcgull::property_readable, ""),
        _manager(manager),
        _nickname(manager),
        _ipc_node(manager->devicesNode()->make_child(_nickname)),
        _awake(ipcgull::property_readable, true) {
    _init();
}

Device::Device(std::shared_ptr<backend::raw::RawDevice> raw_device,
               hidpp::DeviceIndex index, const std::shared_ptr<DeviceManager>& manager) :
        _hidpp20(hidpp20::Device::make(
                std::move(raw_device), index,
                manager->config()->io_timeout.value_or(defaults::io_timeout))),
        _path(raw_device->rawPath()), _index(index),
        _config(_getConfig(manager, _hidpp20->name())),
        _profile_name(ipcgull::property_readable, ""),
        _manager(manager),
        _nickname(manager),
        _ipc_node(manager->devicesNode()->make_child(_nickname)),
        _awake(ipcgull::property_readable, true) {
    _init();
}

Device::Device(Receiver* receiver, hidpp::DeviceIndex index,
               const std::shared_ptr<DeviceManager>& manager) :
        _hidpp20(hidpp20::Device::make(
                receiver->rawReceiver(), index,
                manager->config()->io_timeout.value_or(defaults::io_timeout))),
        _path(receiver->path()), _index(index),
        _config(_getConfig(manager, _hidpp20->name())),
        _profile_name(ipcgull::property_readable, ""),
        _manager(manager),
        _nickname(manager),
        _ipc_node(manager->devicesNode()->make_child(_nickname)),
        _awake(ipcgull::property_readable, true) {
    _init();
}

// Purpose: Load per-device state, instantiate features, and apply the initial
// profile.
// Inputs: None beyond the object state built by the constructor.
// Outputs: A ready-to-use device with features configured and IPC state set.
// References: `features::*`, `reset()`, and `configure()/listen()`.
void Device::_init() {
    logPrintf(INFO, "Device found: %s on %s:%d", name().c_str(),
              hidpp20().devicePath().c_str(), _index);

    {
        std::unique_lock lock(_profile_mutex);
        // Start on the configured default profile, creating it if needed. The
        // device always needs one active profile before features can be built
        // because feature configuration reads from that profile snapshot.
        _profile = _config.profiles.find(_config.default_profile);
        if (_profile == _config.profiles.end())
            _profile = _config.profiles.insert({_config.default_profile, {}}).first;
        _profile_name = _config.default_profile;
    }

// Optional features are attached opportunistically; unsupported ones are
// skipped. This keeps the runtime feature set aligned with the actual
// hardware rather than with model-specific assumptions.
    _addFeature<features::DPI>("dpi");
    _addFeature<features::SmartShift>("smartshift");
    _addFeature<features::HiresScroll>("hiresscroll");
    _addFeature<features::RemapButton>("remapbutton");
    _addFeature<features::DeviceStatus>("devicestatus");
    _addFeature<features::ThumbWheel>("thumbwheel");

    _makeResetMechanism();
    reset();

    for (auto& feature: _features) {
        feature.second->configure();
        feature.second->listen();
    }
}

// Purpose: Return the device's human-readable name.
// Inputs: None.
// Outputs: Name reported by the HID++ layer.
// Used by: IPC metadata and logging.
std::string Device::name() {
    return _hidpp20->name();
}

// Purpose: Return the device product ID.
// Inputs: None.
// Outputs: HID product ID.
// Used by: IPC metadata and filtering.
uint16_t Device::pid() {
    return _hidpp20->pid();
}

// Purpose: Mark the device asleep and notify IPC listeners.
// Inputs: None.
// Outputs: `_awake` becomes false when the state changes.
// Used by: receiver power-state handling.
void Device::sleep() {
    std::lock_guard<std::mutex> lock(_state_lock);
    if (_awake) {
        logPrintf(INFO, "%s:%d fell asleep.", _path.c_str(), _index);
        _awake = false;
        _ipc_interface->notifyStatus();
    }
}

// Purpose: Reapply the active profile after the device wakes up.
// Inputs: None.
// Outputs: A reset device with feature state reapplied and awake status updated.
// Used by: `Receiver::addDevice()` and power-state transitions.
void Device::wakeup() {
    std::lock_guard<std::mutex> lock(_state_lock);

    reconfigure();

    if (!_awake) {
        _awake = true;
        _ipc_interface->notifyStatus();
    }

    logPrintf(INFO, "%s:%d woke up.", _path.c_str(), _index);
}

// Purpose: Re-run the full device configuration pass.
// Inputs: None.
// Outputs: Hardware reset plus feature reconfiguration.
// Used by: `wakeup()`, `setProfile()`, and `clearProfile()`.
void Device::reconfigure() {
    reset();

    for (auto& feature: _features)
        feature.second->configure();
}

// Purpose: Trigger the device reset feature when available.
// Inputs: None.
// Outputs: A hardware reset request, or a debug log when unsupported.
// Used by: `reconfigure()`.
void Device::reset() {
    if (_reset_mechanism)
        (*_reset_mechanism)();
    else
        logPrintf(DEBUG, "%s:%d tried to reset, but no reset mechanism was "
                         "available.", _path.c_str(), _index);
}

// Purpose: Return the shared virtual input device.
// Inputs: None.
// Outputs: The daemon's synthetic input target.
// Used by: feature action code.
std::shared_ptr<InputDevice> Device::virtualInput() const {
    if (auto manager = _manager.lock()) {
        return manager->virtualInput();
    } else {
        logPrintf(ERROR, "Device manager lost");
        logPrintf(ERROR,
                  "Fatal error occurred, file a bug report,"
                  " the program will now exit.");
        std::terminate();
    }
}

// Purpose: Return the device's IPC node.
// Inputs: None.
// Outputs: The exported node object.
// Used by: callers that need to attach child interfaces.
std::shared_ptr<ipcgull::node> Device::ipcNode() const {
    return _ipc_node;
}

// Purpose: List all configured profile names.
// Inputs: None.
// Outputs: A stable snapshot of profile names.
// Used by: Device IPC clients.
std::vector<std::string> Device::getProfiles() const {
    std::shared_lock lock(_profile_mutex);

    std::vector<std::string> ret;
    for (auto& profile : _config.profiles) {
        ret.push_back(profile.first);
    }

    return ret;
}

// Purpose: Change the active profile and rebind feature config.
// Inputs: The target profile name.
// Outputs: A new active profile plus an immediate reconfiguration.
// Used by: IPC `SetProfile`.
void Device::setProfile(const std::string& profile) {
    std::unique_lock lock(_profile_mutex);

    _profile = _config.profiles.find(profile);
    if (_profile == _config.profiles.end())
        _profile = _config.profiles.insert({profile, {}}).first;
    _profile_name = profile;

    for (auto& feature : _features)
        feature.second->setProfile(_profile->second);

    reconfigure();
}

// Purpose: Schedule a profile switch on the worker queue.
// Inputs: Target profile name.
// Outputs: A deferred task that switches profile later.
// Used by: action code that should not block input handling.
void Device::setProfileDelayed(const std::string& profile) {
    run_task([self_weak = _self, profile](){
        if (auto self = self_weak.lock())
            self->setProfile(profile);
    });
}

// Purpose: Remove a stored profile when it is safe to do so.
// Inputs: Profile name to remove.
// Outputs: Profile erased or `invalid_argument` if the request is unsafe.
// Used by: IPC `RemoveProfile`.
void Device::removeProfile(const std::string& profile) {
    std::unique_lock lock(_profile_mutex);

    if (profile == (std::string)_profile_name)
        throw std::invalid_argument("cannot remove active profile");
    else if (profile == (std::string)_config.default_profile)
        throw std::invalid_argument("cannot remove default profile");

    _config.profiles.erase(profile);
}

// Purpose: Reset a profile to defaults without deleting the entry.
// Inputs: Profile name to clear.
// Outputs: An empty profile record or `invalid_argument` for unknown names.
// Used by: IPC `ClearProfile`.
void Device::clearProfile(const std::string& profile) {
    std::unique_lock lock(_profile_mutex);

    if (profile == (std::string)_profile_name) {
        _profile->second = config::Profile();

        for (auto& feature : _features)
            feature.second->setProfile(_profile->second);

        reconfigure();
    } else {
        auto it = _config.profiles.find(profile);
        if (it != _config.profiles.end()) {
            it->second = config::Profile();
        } else {
            throw std::invalid_argument("unknown profile");
        }
    }
}

// Purpose: Return the currently active profile.
// Inputs: None.
// Outputs: Mutable profile reference under lock.
// Used by: feature configuration.
config::Profile& Device::activeProfile() {
    std::shared_lock lock(_profile_mutex);
    return _profile->second;
}

// Purpose: Return the underlying HID++ 2.0 device wrapper.
// Inputs: None.
// Outputs: Reference to the transport object.
// Used by: feature wrappers and IPC helpers.
hidpp20::Device& Device::hidpp20() {
    return *_hidpp20;
}

// Purpose: Cache a callable reset path if the hardware exposes the feature.
// Inputs: None beyond the device's HID++ feature map.
// Outputs: A reset lambda or no reset mechanism.
// Used by: `reset()`.
void Device::_makeResetMechanism() {
    try {
        hidpp20::Reset reset(_hidpp20.get());
        _reset_mechanism = std::make_unique<std::function<void()>>(
                [dev = _hidpp20] {
                    hidpp20::Reset reset(dev.get());
                    reset.reset(reset.getProfile());
                });
    } catch (hidpp20::UnsupportedFeature& e) {
        // Reset unsupported, ignore.
    }
}

Device::IPC::IPC(Device* device) :
        ipcgull::interface(
                SERVICE_ROOT_NAME ".Device",
                {
                        {"GetProfiles", {device, &Device::getProfiles, {"profiles"}}},
                        {"SetProfile", {device, &Device::setProfile, {"profile"}}},
                        {"RemoveProfile", {device, &Device::removeProfile, {"profile"}}},
                        {"ClearProfile", {device, &Device::clearProfile, {"profile"}}}
                },
                {
                        {"Name",           ipcgull::property<std::string>(
                                ipcgull::property_readable, device->name())},
                        {"ProductID",      ipcgull::property<uint16_t>(
                                ipcgull::property_readable, device->pid())},
                        {"Active",         device->_awake},
                        {"DefaultProfile", device->_config.default_profile},
                        {"ActiveProfile", device->_profile_name}
                }, {
                        {"StatusChanged", ipcgull::signal::make_signal<bool>({"active"})}
                }), _device(*device) {
}

// Purpose: Emit the current awake/asleep state to IPC clients.
// Inputs: None.
// Outputs: `StatusChanged(active)` on the device IPC interface.
// Used by: `sleep()` and `wakeup()`.
void Device::IPC::notifyStatus() const {
    emit_signal("StatusChanged", (bool) (_device._awake));
}

// Purpose: Normalize device config storage for runtime use.
// Inputs: Manager config and the device name.
// Outputs: A concrete `config::Device` entry with at least one profile.
// References: legacy single-profile configs and the current multi-profile form.
config::Device& Device::_getConfig(
        const std::shared_ptr<DeviceManager>& manager,
        const std::string& name) {
    static std::mutex config_mutex;
    std::lock_guard<std::mutex> lock(config_mutex);
    auto& devices = manager->config()->devices.value();

    if (!devices.count(name)) {
        devices.emplace(name, config::Device());
    }

    auto& device = devices.at(name);
    if (std::holds_alternative<config::Profile>(device)) {
        config::Device d;
        d.profiles["default"] = std::get<config::Profile>(device);
        d.default_profile = "default";
        device = std::move(d);
    }

    auto& conf = std::get<config::Device>(device);
    if (conf.profiles.empty()) {
        conf.profiles["default"] = {};
        conf.default_profile = "default";
    }

    return conf;
}
