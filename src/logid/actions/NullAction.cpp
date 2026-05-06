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
 * File: NullAction.cpp
 *
 * Explicit no-op action used for unassigned buttons. The action preserves the
 * remapping pipeline and button state tracking while intentionally doing no
 * input synthesis.
 */

#include <actions/NullAction.h>
#include <backend/hidpp20/features/ReprogControls.h>

using namespace logid::actions;

// Purpose: Name the IPC interface for the null action.
// Inputs: None.
// Outputs: Static interface name string.
// Used by: action construction.
const char* NullAction::interface_name = "None";

// Purpose: Create the no-op action wrapper.
// Inputs: Device and parent IPC node.
// Outputs: Action object that performs no output.
// Used by: unassigned button mappings.
NullAction::NullAction(
        Device* device,
        [[maybe_unused]] const std::shared_ptr<ipcgull::node>& parent) :
        Action(device, interface_name) {
}

// Purpose: Mark the action as pressed.
// Inputs: None.
// Outputs: Internal pressed state set.
// Used by: button state tracking.
void NullAction::press() {
    _pressed = true;
}

// Purpose: Clear the pressed state.
// Inputs: None.
// Outputs: Internal pressed state cleared.
// Used by: button state tracking.
void NullAction::release() {
    _pressed = false;
}

// Purpose: Mark the button as temporarily diverted.
// Inputs: None.
// Outputs: Reprog flag bits.
// Used by: hardware remapping.
uint8_t NullAction::reprogFlags() const {
    return backend::hidpp20::ReprogControls::TemporaryDiverted;
}
