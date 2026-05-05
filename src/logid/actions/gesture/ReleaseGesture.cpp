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
#include <actions/gesture/ReleaseGesture.h>
#include <Configuration.h>

using namespace logid::actions;

// IPC name used when this gesture is exported through D-Bus.
const char* ReleaseGesture::interface_name = "OnRelease";

// Bind the release-triggered gesture to its config and optional action.
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

// Initialize the threshold counter at the start of a gesture sequence.
void ReleaseGesture::press(bool init_threshold) {
    std::shared_lock lock(_config_mutex);
    if (init_threshold) {
        _axis = (int32_t) (_config.threshold.value_or(defaults::gesture_threshold));
    } else {
        _axis = 0;
    }
}

// Fire the configured action only when the gesture met its threshold.
void ReleaseGesture::release(bool primary) {
    if (metThreshold() && primary) {
        if (_action) {
            _action->press();
            _action->release();
        }
    }
}

// Accumulate the movement used to decide when to trigger the action.
void ReleaseGesture::move(int16_t axis) {
    _axis += axis;
}

// Release gestures are not meant for wheel-style inputs.
bool ReleaseGesture::wheelCompatibility() const {
    return false;
}

// Report whether enough movement has accumulated.
bool ReleaseGesture::metThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _axis >= _config.threshold.value_or(defaults::gesture_threshold);
}


// Return the configured threshold value.
int ReleaseGesture::getThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _config.threshold.value_or(0);
}

// Update the threshold used to decide whether the action fires.
void ReleaseGesture::setThreshold(int threshold) {
    std::unique_lock lock(_config_mutex);
    if (threshold == 0)
        _config.threshold.reset();
    else
        _config.threshold = threshold;
}

// Replace the action executed when the gesture completes.
void ReleaseGesture::setAction(const std::string& type) {
    std::unique_lock lock(_config_mutex);
    _action.reset();
    _action = Action::makeAction(_device, type, _config.action, _node);
}
