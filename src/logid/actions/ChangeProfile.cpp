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
 * File: ChangeProfile.cpp
 *
 * Action that defers a device profile switch until release. The action keeps a
 * configurable target profile name and hands the actual profile change off to
 * the device manager worker queue so input handling stays responsive.
 */

#include <actions/ChangeProfile.h>
#include <backend/hidpp20/features/ReprogControls.h>
#include <Device.h>

using namespace logid;
using namespace logid::actions;

// Purpose: Name the IPC interface for this action.
// Inputs: None.
// Outputs: Static interface name string.
// Used by: action construction.
const char* ChangeProfile::interface_name = "ChangeProfile";

// Purpose: Bind the action to the profile config and expose its editable name.
// Inputs: Device, config, and parent IPC node.
// Outputs: Action object with IPC methods.
// Used by: profile switching buttons.
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

// Purpose: Ignore the press edge.
// Inputs: None.
// Outputs: No action until release.
// Used by: momentary profile switching buttons.
void ChangeProfile::press() {
}

// Purpose: Request the target profile on release.
// Inputs: None.
// Outputs: Deferred profile change on the worker queue.
// Used by: button release handling.
void ChangeProfile::release() {
    std::shared_lock lock(_config_mutex);
    if (_config.profile.has_value())
        _device->setProfileDelayed(_config.profile.value());
}

// Purpose: Mark the hardware button as temporarily diverted.
// Inputs: None.
// Outputs: Reprog flag bits.
// Used by: hardware remapping.
uint8_t ChangeProfile::reprogFlags() const {
    return backend::hidpp20::ReprogControls::TemporaryDiverted;
}

// Purpose: Return the configured target profile name.
// Inputs: None.
// Outputs: Profile name or empty string.
// Used by: IPC `GetProfile`.
std::string ChangeProfile::getProfile() {
    std::shared_lock lock(_config_mutex);
    if (_config.profile.has_value())
        return _config.profile.value();
    else
        return "";
}

// Purpose: Update the configured target profile name.
// Inputs: New profile name.
// Outputs: Config value updated or cleared.
// Used by: IPC `SetProfile`.
void ChangeProfile::setProfile(std::string profile) {
    std::unique_lock lock(_config_mutex);

    if (profile.empty())
        _config.profile->clear();
    else
        _config.profile = std::move(profile);
}
