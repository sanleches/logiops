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
#include <actions/gesture/NullGesture.h>
#include <Configuration.h>

using namespace logid::actions;

// IPC name used when this gesture is exported through D-Bus.
const char* NullGesture::interface_name = "None";

// Bind the no-op gesture to the config entry.
NullGesture::NullGesture(Device* device,
                         config::NoGesture& config,
                         const std::shared_ptr<ipcgull::node>& parent) :
        Gesture(device, parent, interface_name), _config(config) {
}

// Track the threshold state even though no action will be triggered.
void NullGesture::press(bool init_threshold) {
    _axis = init_threshold ? _config.threshold.value_or(
            defaults::gesture_threshold) : 0;
}

// No-op release, but keep the signature consistent with other gestures.
void NullGesture::release(bool primary) {
    // Do nothing
    (void) primary; // Suppress unused warning
}

// Accumulate movement so threshold comparisons still work.
void NullGesture::move(int16_t axis) {
    _axis += axis;
}

// Null gestures can be used in wheel-like paths.
bool NullGesture::wheelCompatibility() const {
    return true;
}

// Report whether enough movement has accumulated to count as active.
bool NullGesture::metThreshold() const {
    return _axis >= _config.threshold.value_or(defaults::gesture_threshold);
}
