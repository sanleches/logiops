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
 * File: NullGesture.cpp
 *
 * Gesture placeholder that keeps threshold tracking and IPC plumbing without
 * firing an action.
 */

#include <actions/gesture/NullGesture.h>
#include <Configuration.h>

using namespace logid::actions;

// IPC name used when this gesture is exported through D-Bus.
const char* NullGesture::interface_name = "None";

// Purpose: Bind the no-op gesture to the config entry.
// Inputs: Device, config, and IPC parent node.
// Outputs: No-op gesture interface.
// Used by: default gesture mappings.
NullGesture::NullGesture(Device* device,
                         config::NoGesture& config,
                         const std::shared_ptr<ipcgull::node>& parent) :
        Gesture(device, parent, interface_name), _config(config) {
}

// Purpose: Track the threshold state even though no action will be triggered.
// Inputs: Whether to seed from threshold.
// Outputs: Internal state reset.
// Used by: gesture begin handling.
void NullGesture::press(bool init_threshold) {
    _axis = init_threshold ? _config.threshold.value_or(
            defaults::gesture_threshold) : 0;
}

// Purpose: No-op release, but keep the signature consistent with other gestures.
// Inputs: Whether this is the primary release.
// Outputs: No-op.
// Used by: gesture end handling.
void NullGesture::release(bool primary) {
    // Do nothing
    (void) primary; // Suppress unused warning
}

// Purpose: Accumulate movement so threshold comparisons still work.
// Inputs: Movement delta.
// Outputs: Updated movement accumulation.
// Used by: gesture motion handling.
void NullGesture::move(int16_t axis) {
    _axis += axis;
}

// Purpose: Null gestures can be used in wheel-like paths.
// Inputs: None.
// Outputs: True.
// Used by: gesture compatibility checks.
bool NullGesture::wheelCompatibility() const {
    return true;
}

// Purpose: Report whether enough movement has accumulated to count as active.
// Inputs: None.
// Outputs: Threshold met flag.
// Used by: gesture dispatch.
bool NullGesture::metThreshold() const {
    return _axis >= _config.threshold.value_or(defaults::gesture_threshold);
}
