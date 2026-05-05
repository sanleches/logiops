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
 * File: IntervalGesture.cpp
 *
 * Gesture implementation that fires after a threshold and repeats on fixed
 * movement intervals while the gesture remains active.
 */

#include <actions/gesture/IntervalGesture.h>
#include <Configuration.h>
#include <util/log.h>

using namespace logid::actions;

// IPC name used when this gesture is exported through D-Bus.
const char* IntervalGesture::interface_name = "OnInterval";

// Purpose: Bind the interval gesture to its config and optional action.
// Inputs: Device, config, and IPC parent node.
// Outputs: Interval gesture interface.
// Used by: gesture action setup.
IntervalGesture::IntervalGesture(
        Device* device, config::IntervalGesture& config,
        const std::shared_ptr<ipcgull::node>& parent) :
        Gesture(device, parent, interface_name, {
                {
                        {"GetConfig", {this, &IntervalGesture::getConfig, {"interval", "threshold"}}},
                        {"SetInterval", {this, &IntervalGesture::setInterval, {"interval"}}},
                        {"SetThreshold", {this, &IntervalGesture::setThreshold, {"interval"}}},
                        {"SetAction", {this, &IntervalGesture::setAction, {"type"}}}
                },
                {},
                {}
        }),
        _axis(0), _interval_pass_count(0), _config(config) {
    if (config.action) {
        try {
            _action = Action::makeAction(device, config.action.value(), _node);
        } catch (InvalidAction& e) {
            logPrintf(WARN, "Mapping gesture to invalid action");
        }
    }
}

// Purpose: Initialize the threshold accumulation at the start of a gesture sequence.
// Inputs: Whether to seed from threshold.
// Outputs: Reset internal state.
// Used by: gesture begin handling.
void IntervalGesture::press(bool init_threshold) {
    std::shared_lock lock(_config_mutex);
    if (init_threshold) {
        _axis = (int32_t) _config.threshold.value_or(defaults::gesture_threshold);
    } else {
        _axis = 0;
    }
    _interval_pass_count = 0;
}

// Purpose: Interval gestures do not trigger on release.
// Inputs: Whether this is the primary release.
// Outputs: No-op.
// Used by: gesture end handling.
void IntervalGesture::release([[maybe_unused]] bool primary) {
}

// Purpose: Emit the action again each time movement crosses another interval boundary.
// Inputs: Movement delta.
// Outputs: Optional action trigger.
// Used by: gesture motion handling.
void IntervalGesture::move(int16_t axis) {
    std::shared_lock lock(_config_mutex);
    if (!_config.interval.has_value())
        return;

    const auto threshold =
            _config.threshold.value_or(defaults::gesture_threshold);
    _axis += axis;
    if (_axis < threshold)
        return;

    int32_t new_interval_count = (_axis - threshold) / _config.interval.value();
    if (new_interval_count > _interval_pass_count) {
        if (_action) {
            _action->press();
            _action->release();
        }
    }
    _interval_pass_count = new_interval_count;
}

// Purpose: Interval gestures can be used for wheel-style inputs.
// Inputs: None.
// Outputs: True.
// Used by: gesture compatibility checks.
bool IntervalGesture::wheelCompatibility() const {
    return true;
}

// Purpose: Report whether the gesture has crossed its threshold.
// Inputs: None.
// Outputs: Threshold met flag.
// Used by: gesture dispatch.
bool IntervalGesture::metThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _axis >= _config.threshold.value_or(defaults::gesture_threshold);
}

// Purpose: Return the configured interval and threshold.
// Inputs: None.
// Outputs: Interval and threshold tuple.
// Used by: IPC getters.
std::tuple<int, int> IntervalGesture::getConfig() const {
    std::shared_lock lock(_config_mutex);
    return {_config.interval.value_or(0), _config.threshold.value_or(0)};
}

// Purpose: Update the interval size between repeated triggers.
// Inputs: Interval value.
// Outputs: Config update.
// Used by: IPC setters.
void IntervalGesture::setInterval(int interval) {
    std::unique_lock lock(_config_mutex);
    if (interval == 0)
        _config.interval.reset();
    else
        _config.interval = interval;
}

// Purpose: Update the threshold used before interval triggering starts.
// Inputs: Threshold value.
// Outputs: Config update.
// Used by: IPC setters.
void IntervalGesture::setThreshold(int threshold) {
    std::unique_lock lock(_config_mutex);
    if (threshold == 0)
        _config.threshold.reset();
    else
        _config.threshold = threshold;
}

// Purpose: Replace the action executed at each interval boundary.
// Inputs: Action type name.
// Outputs: New action binding.
// Used by: IPC setters.
void IntervalGesture::setAction(const std::string& type) {
    std::unique_lock lock(_config_mutex);
    _action.reset();
    _action = Action::makeAction(_device, type, _config.action, _node);
}
