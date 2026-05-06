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
 * File: ReleaseGesture.cpp
 *
 * Gesture implementation that fires once on release if the movement threshold
 * has been met.
 */

#include <actions/gesture/ReleaseGesture.h>
#include <Configuration.h>

using namespace logid::actions;

// IPC name used when this gesture is exported through D-Bus.
const char* ReleaseGesture::interface_name = "OnRelease";

// Purpose: Bind the release-triggered gesture to its config and optional action.
// Inputs: Device, config, and IPC parent node.
// Outputs: Release gesture interface.
// Used by: gesture action setup.
ReleaseGesture::ReleaseGesture(Device* device, config::ReleaseGesture& config,
                               const std::shared_ptr<ipcgull::node>& parent) :
        Gesture(device, parent, interface_name, {
                {
                        {"GetThreshold", {this, &ReleaseGesture::getThreshold, {"threshold"}}},
                        {"SetThreshold", {this, &ReleaseGesture::setThreshold, {"threshold"}}},
                        {"SetAction", {this, &ReleaseGesture::setAction, {"type"}}}
                },
                {},
                {}
        }), _config(config) {
    if (_config.action.has_value())
        _action = Action::makeAction(device, _config.action.value(), _node);
}

// Purpose: Initialize the threshold counter at the start of a gesture sequence.
// Inputs: Whether to seed from threshold.
// Outputs: Reset internal state.
// Used by: gesture begin handling.
void ReleaseGesture::press(bool init_threshold) {
    std::shared_lock lock(_config_mutex);
    if (init_threshold) {
        _axis = (int32_t) (_config.threshold.value_or(defaults::gesture_threshold));
    } else {
        _axis = 0;
    }
}

// Purpose: Fire the configured action only when the gesture met its threshold.
// Inputs: Whether this is the primary release.
// Outputs: Optional action trigger.
// Used by: gesture end handling.
void ReleaseGesture::release(bool primary) {
    if (metThreshold() && primary) {
        if (_action) {
            _action->press();
            _action->release();
        }
    }
}

// Purpose: Accumulate the movement used to decide when to trigger the action.
// Inputs: Movement delta.
// Outputs: Updated movement accumulation.
// Used by: gesture motion handling.
void ReleaseGesture::move(int16_t axis) {
    _axis += axis;
}

// Purpose: Release gestures are not meant for wheel-style inputs.
// Inputs: None.
// Outputs: False.
// Used by: gesture compatibility checks.
bool ReleaseGesture::wheelCompatibility() const {
    return false;
}

// Purpose: Report whether enough movement has accumulated.
// Inputs: None.
// Outputs: Threshold met flag.
// Used by: gesture dispatch.
bool ReleaseGesture::metThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _axis >= _config.threshold.value_or(defaults::gesture_threshold);
}


// Purpose: Return the configured threshold value.
// Inputs: None.
// Outputs: Threshold value or zero.
// Used by: IPC getters.
int ReleaseGesture::getThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _config.threshold.value_or(0);
}

// Purpose: Update the threshold used to decide whether the action fires.
// Inputs: Threshold value.
// Outputs: Config update.
// Used by: IPC setters.
void ReleaseGesture::setThreshold(int threshold) {
    std::unique_lock lock(_config_mutex);
    if (threshold == 0)
        _config.threshold.reset();
    else
        _config.threshold = threshold;
}

// Purpose: Replace the action executed when the gesture completes.
// Inputs: Action type name.
// Outputs: New action binding.
// Used by: IPC setters.
void ReleaseGesture::setAction(const std::string& type) {
    std::unique_lock lock(_config_mutex);
    _action.reset();
    _action = Action::makeAction(_device, type, _config.action, _node);
}
