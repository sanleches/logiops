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
 * File: KeypressAction.cpp
 *
 * Action that synthesizes key presses on the daemon's virtual input device.
 * This module converts config-defined key names or numeric codes into cached
 * key codes, presses them on activation, and releases them when the action ends.
 */

#include <actions/KeypressAction.h>
#include <Device.h>
#include <InputDevice.h>
#include <backend/hidpp20/features/ReprogControls.h>
#include <util/log.h>

using namespace logid::actions;
using namespace logid::backend;

// Purpose: Name the IPC interface for this action.
// Inputs: None.
// Outputs: Static interface name string.
// Used by: action construction.
const char* KeypressAction::interface_name = "Keypress";

// Purpose: Bind the action to config and cache the target key codes.
// Inputs: Device, action config, and parent IPC node.
// Outputs: Ready-to-use action object with IPC methods.
// References: virtual input conversion and config parsing.
KeypressAction::KeypressAction(
        Device* device, config::KeypressAction& config,
        [[maybe_unused]] const std::shared_ptr<ipcgull::node>& parent) :
        Action(device, interface_name, {
                {
                        {"GetKeys", {this, &KeypressAction::getKeys, {"keys"}}},
                        {"SetKeys", {this, &KeypressAction::setKeys, {"keys"}}}
                },
                {},
                {}
        }), _config(config) {
    _setConfig();
}

// Purpose: Press all configured keys.
// Inputs: None.
// Outputs: Key-down events on the virtual input device.
// Used by: button press handling.
void KeypressAction::press() {
    std::shared_lock lock(_config_mutex);
    _pressed = true;
    for (auto& key: _keys)
        _device->virtualInput()->pressKey(key);
}

// Purpose: Release all configured keys.
// Inputs: None.
// Outputs: Key-up events on the virtual input device.
// Used by: button release handling.
void KeypressAction::release() {
    std::shared_lock lock(_config_mutex);
    _pressed = false;
    for (auto& key: _keys)
        _device->virtualInput()->releaseKey(key);
}

// Purpose: Convert config values into cached key codes.
// Inputs: Current action config.
// Outputs: Internal key list and registered virtual codes.
// Used by: constructor and `setKeys()`.
void KeypressAction::_setConfig() {
    _keys.clear();

    if (!_config.keys.has_value())
        return;

    auto& config = _config.keys.value();

    // A single string means one key name, so resolve it directly.
    if (std::holds_alternative<std::string>(config)) {
        const auto& key = std::get<std::string>(config);
        try {
            auto code = _device->virtualInput()->toKeyCode(key);
            _device->virtualInput()->registerKey(code);
            _keys.emplace_back(code);
        } catch (InputDevice::InvalidEventCode& e) {
            logPrintf(WARN, "Invalid keycode %s, skipping.", key.c_str());
        }
    // A single integer means one numeric key code.
    } else if (std::holds_alternative<uint>(_config.keys.value())) {
        const auto& key = std::get<uint>(config);
        _device->virtualInput()->registerKey(key);
        _keys.emplace_back(key);
    // A list allows mixed names and numeric codes.
    } else if (std::holds_alternative<
            std::list<std::variant<uint, std::string>>>(config)) {
        const auto& keys = std::get<
                std::list<std::variant<uint, std::string>>>(config);
        for (const auto& key: keys) {
            if (std::holds_alternative<std::string>(key)) {
                const auto& key_str = std::get<std::string>(key);
                try {
                    auto code = _device->virtualInput()->toKeyCode(key_str);
                    _device->virtualInput()->registerKey(code);
                    _keys.emplace_back(code);
                } catch (InputDevice::InvalidEventCode& e) {
                    logPrintf(WARN, "Invalid keycode %s, skipping.",
                              key_str.c_str());
                }
            } else if (std::holds_alternative<uint>(key)) {
                auto& code = std::get<uint>(key);
                _device->virtualInput()->registerKey(code);
                _keys.emplace_back(code);
            }
        }
    }
}

// Purpose: Mark the button as temporarily diverted.
// Inputs: None.
// Outputs: HID++ reprog flag bits.
// Used by: hardware remapping.
uint8_t KeypressAction::reprogFlags() const {
    return hidpp20::ReprogControls::TemporaryDiverted;
}

// Purpose: Return the configured key names.
// Inputs: None.
// Outputs: A list of key names.
// Used by: IPC `GetKeys`.
std::vector<std::string> KeypressAction::getKeys() const {
    std::shared_lock lock(_config_mutex);
    std::vector<std::string> ret;
    for (auto& x: _keys)
        ret.push_back(InputDevice::toKeyName(x));

    return ret;
}

// Purpose: Replace the configured keys and rebuild cached codes.
// Inputs: New key name list.
// Outputs: Updated action config and virtual device registrations.
// Used by: IPC `SetKeys`.
void KeypressAction::setKeys(const std::vector<std::string>& keys) {
    std::unique_lock lock(_config_mutex);
    if (_pressed)
        for (auto& key: _keys)
            _device->virtualInput()->releaseKey(key);
    _config.keys = std::list<std::variant<uint, std::string>>();
    auto& config = std::get<std::list<std::variant<uint, std::string>>>(
            _config.keys.value());
    for (auto& x: keys)
        config.emplace_back(x);
    _setConfig();
}
