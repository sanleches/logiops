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
#include <actions/ChangeProfile.h>
#include <backend/hidpp20/features/ReprogControls.h>
#include <Device.h>

using namespace logid;
using namespace logid::actions;

// IPC name used when this action is exported through D-Bus.
const char* ChangeProfile::interface_name = "ChangeProfile";

// Bind the action to the profile config and expose its editable profile name.
ChangeProfile::ChangeProfile(Device* device, config::ChangeProfile& config,
                             [[maybe_unused]] const std::shared_ptr<ipcgull::node>& parent) :
        Action(device, interface_name, {
                {
                        {"GetProfile", {this, &ChangeProfile::getProfile, {"profile"}}},
                        {"SetProfile", {this, &ChangeProfile::setProfile, {"profile"}}}
                },
                {},
                {}
        }), _config(config) {
}

// This action does nothing on press so the profile only changes once the button is released.
void ChangeProfile::press() {
}

// Defer the profile switch to the device manager's worker queue.
void ChangeProfile::release() {
    std::shared_lock lock(_config_mutex);
    if (_config.profile.has_value())
        _device->setProfileDelayed(_config.profile.value());
}

// The profile-switch action temporarily diverts the hardware button.
uint8_t ChangeProfile::reprogFlags() const {
    return backend::hidpp20::ReprogControls::TemporaryDiverted;
}

// Return the configured target profile name.
std::string ChangeProfile::getProfile() {
    std::shared_lock lock(_config_mutex);
    if (_config.profile.has_value())
        return _config.profile.value();
    else
        return "";
}

// Update the configured target profile name, or clear it when given an empty string.
void ChangeProfile::setProfile(std::string profile) {
    std::unique_lock lock(_config_mutex);

    if (profile.empty())
        _config.profile->clear();
    else
        _config.profile = std::move(profile);
}
