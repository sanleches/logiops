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
 * File: ToggleSmartShift.cpp
 *
 * Action that toggles the SmartShift hardware feature on press. The action
 * resolves the feature once, then flips its active state asynchronously when
 * triggered.
 */

#include <actions/ToggleSmartShift.h>
#include <Device.h>
#include <backend/hidpp20/features/ReprogControls.h>
#include <util/task.h>
#include <util/log.h>

using namespace logid::actions;
using namespace logid::backend;

// Purpose: Name the IPC interface for this action.
// Inputs: None.
// Outputs: Static interface name string.
// Used by: action construction.
const char* ToggleSmartShift::interface_name = "ToggleSmartShift";

// Purpose: Bind the action to the SmartShift feature if available.
// Inputs: Device and parent IPC node.
// Outputs: Action object ready to toggle the feature.
// Used by: SmartShift toggle buttons.
ToggleSmartShift::ToggleSmartShift(
        Device* dev,
        [[maybe_unused]] const std::shared_ptr<ipcgull::node>& parent) :
        Action(dev, interface_name) {
    _smartshift = _device->getFeature<features::SmartShift>("smartshift");
    if (!_smartshift)
        logPrintf(WARN, "%s:%d: SmartShift feature not found, cannot use "
                        "ToggleSmartShift action.",
                  _device->hidpp20().devicePath().c_str(),
                  _device->hidpp20().deviceIndex());
}

// Purpose: Toggle SmartShift on press.
// Inputs: None.
// Outputs: Deferred SmartShift state update.
// Used by: button press handling.
void ToggleSmartShift::press() {
    _pressed = true;
    if (_smartshift) {
        run_task([self_weak = self<ToggleSmartShift>()]() {
            if (auto self = self_weak.lock()) {
                auto status = self->_smartshift->getStatus();
                status.setActive = true;
                status.active = !status.active;
                self->_smartshift->setStatus(status);
            }
        });
    }
}

// Purpose: Clear the pressed marker.
// Inputs: None.
// Outputs: Button state no longer active.
// Used by: button release handling.
void ToggleSmartShift::release() {
    _pressed = false;
}

// Purpose: Mark the button as temporarily diverted.
// Inputs: None.
// Outputs: Reprog flag bits.
// Used by: hardware remapping.
uint8_t ToggleSmartShift::reprogFlags() const {
    return hidpp20::ReprogControls::TemporaryDiverted;
}
