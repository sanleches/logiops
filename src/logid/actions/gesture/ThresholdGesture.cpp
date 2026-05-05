/*
 * Copyright 2019-2023 PixlOne, michtere
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
 * File: ThresholdGesture.cpp
 *
 * Gesture implementation that fires once movement crosses a configured
 * threshold and then executes one action.
 */

#include <actions/gesture/ThresholdGesture.h>
#include <Configuration.h>
#include <util/log.h>

using namespace logid::actions;

// IPC name used when this gesture is exported through D-Bus.
const char* ThresholdGesture::interface_name = "OnRelease";

// Purpose: Bind the threshold gesture to its config and optional action.
// Inputs: Device, config, and IPC parent node.
// Outputs: Threshold gesture interface.
// Used by: gesture action setup.
ThresholdGesture::ThresholdGesture(
        Device* device, config::ThresholdGesture& config,
        const std::shared_ptr<ipcgull::node>& parent) :
        Gesture(device, parent, interface_name, {
                {
                        {"GetThreshold", {this, &ThresholdGesture::getThreshold, {"threshold"}}},
                        {"SetThreshold", {this, &ThresholdGesture::setThreshold, {"threshold"}}},
                        {"SetAction", {this, &ThresholdGesture::setAction, {"type"}}}
                },
                {},
                {}
        }), _config(config) {
    if (config.action) {
        try {
            _action = Action::makeAction(device, config.action.value(), _node);
        } catch (InvalidAction& e) {
            logPrintf(WARN, "Mapping gesture to invalid action");
        }
    }
}

// Purpose: Initialize movement tracking for a new gesture sequence.
// Inputs: Whether to seed from threshold.
// Outputs: Reset internal state.
// Used by: gesture begin handling.
void ThresholdGesture::press(bool init_threshold) {
    std::shared_lock lock(_config_mutex);
    _axis = init_threshold ? (int32_t) _config.threshold.value_or(defaults::gesture_threshold) : 0;
    this->_executed = false;
}

// Purpose: Clear the one-shot execution state after the gesture ends.
// Inputs: Whether this is the primary release.
// Outputs: Reset execution guard.
// Used by: gesture end handling.
void ThresholdGesture::release([[maybe_unused]] bool primary) {
    this->_executed = false;
}

// Purpose: Accumulate movement and fire the action once the threshold is crossed.
// Inputs: Movement delta.
// Outputs: Optional action trigger.
// Used by: gesture motion handling.
void ThresholdGesture::move(int16_t axis) {
    _axis += axis;

    if (!this->_executed && metThreshold()) {
        if (_action) {
            _action->press();
            _action->release();
        }
        this->_executed = true;
    }
}

// Purpose: Report whether enough movement has accumulated.
// Inputs: None.
// Outputs: Threshold met flag.
// Used by: gesture dispatch.
bool ThresholdGesture::metThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _axis >= _config.threshold.value_or(defaults::gesture_threshold);
}

// Purpose: Report that threshold gestures are not designed for wheel-style inputs.
// Inputs: None.
// Outputs: False.
// Used by: gesture compatibility checks.
bool ThresholdGesture::wheelCompatibility() const {
    return false;
}

// Purpose: Return the configured threshold value.
// Inputs: None.
// Outputs: Threshold value or zero.
// Used by: IPC getters.
int ThresholdGesture::getThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _config.threshold.value_or(0);
}

// Purpose: Update the threshold used for action firing.
// Inputs: Threshold value.
// Outputs: Config update.
// Used by: IPC setters.
void ThresholdGesture::setThreshold(int threshold) {
    std::unique_lock lock(_config_mutex);
    if (threshold == 0)
        _config.threshold.reset();
    else
        _config.threshold = threshold;
}

// Purpose: Replace the action executed when the threshold is first crossed.
// Inputs: Action type name.
// Outputs: New action binding.
// Used by: IPC setters.
void ThresholdGesture::setAction(const std::string& type) {
    std::unique_lock lock(_config_mutex);
    _action.reset();
    _action = Action::makeAction(_device, type, _config.action, _node);
}
