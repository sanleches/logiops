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
 * File: AxisGesture.cpp
 *
 * Gesture implementation that turns movement into virtual axis events.
 */

#include <cmath>
#include <actions/gesture/AxisGesture.h>
#include <Device.h>
#include <InputDevice.h>
#include <util/log.h>

using namespace logid::actions;

// IPC name used when this gesture is exported through D-Bus.
const char* AxisGesture::interface_name = "Axis";

// Purpose: Bind the gesture to axis movement and the virtual input device.
// Inputs: Device, config, and IPC parent node.
// Outputs: Axis gesture interface.
// Used by: gesture action setup.
AxisGesture::AxisGesture(Device* device, config::AxisGesture& config,
                         const std::shared_ptr<ipcgull::node>& parent) :
        Gesture(device, parent, interface_name, {
            {
                    {"GetConfig", {this, &AxisGesture::getConfig, {"axis", "multiplier", "threshold"}}},
                    {"SetThreshold", {this, &AxisGesture::setThreshold, {"threshold"}}},
                    {"SetMultiplier", {this, &AxisGesture::setMultiplier, {"multiplier"}}},
                    {"SetAxis", {this, &AxisGesture::setAxis, {"axis"}}}
            },
            {},
            {}
        }), _multiplier(1), _config(config) {
    if (_config.axis.has_value()) {
        if (std::holds_alternative<uint>(_config.axis.value())) {
            _input_axis = std::get<uint>(_config.axis.value());
        } else {
            const auto& axis = std::get<std::string>(_config.axis.value());
            try {
                _input_axis = _device->virtualInput()->toAxisCode(axis);
            } catch (InputDevice::InvalidEventCode& e) {
                logPrintf(WARN, "Invalid axis %s.");
            }
        }

    }

    if (_input_axis.has_value())
        _device->virtualInput()->registerAxis(_input_axis.value());
}

// Purpose: Initialize threshold and internal accumulation when the gesture starts.
// Inputs: Whether to seed from threshold.
// Outputs: Reset internal state.
// Used by: gesture begin handling.
void AxisGesture::press(bool init_threshold) {
    std::shared_lock lock(_config_mutex);
    if (init_threshold) {
        _axis = (int32_t) (_config.threshold.value_or(defaults::gesture_threshold));
    } else {
        _axis = 0;
    }
    _axis_remainder = 0;
    _hires_remainder = 0;
}

// Purpose: Axis gestures do not fire on release.
// Inputs: Whether this is the primary release.
// Outputs: No-op.
// Used by: gesture end handling.
void AxisGesture::release(bool primary) {
    // Do nothing
    (void) primary; // Suppress unused warning
}

// Purpose: Convert movement into virtual axis events once the threshold is passed.
// Inputs: Movement delta.
// Outputs: Virtual axis events.
// Used by: gesture motion handling.
void AxisGesture::move(int16_t axis) {
    std::shared_lock lock(_config_mutex);
    if (!_input_axis.has_value())
        return;

    const auto threshold = _config.threshold.value_or(
            defaults::gesture_threshold);
    int32_t new_axis = _axis + axis;
    int low_res_axis = InputDevice::getLowResAxis(axis);
    int hires_remainder = _hires_remainder;

    if (new_axis > threshold) {
        double move = axis;
        if (_axis < threshold)
            move = new_axis - threshold;
        bool negative_multiplier = _config.axis_multiplier.value_or(1) < 0;
        if (negative_multiplier)
            move *= -_config.axis_multiplier.value_or(1);
        else
            move *= _config.axis_multiplier.value_or(1);
        // Scale the movement using the wheel's high-resolution multiplier.
        move *= _multiplier;

        double move_floor = floor(move);
        _axis_remainder = move - move_floor;
        if (_axis_remainder >= 1) {
            double int_remainder = floor(_axis_remainder);
            move_floor += int_remainder;
            _axis_remainder -= int_remainder;
        }

        if (negative_multiplier)
            move_floor = -move_floor;

        if (low_res_axis != -1) {
            int lowres_movement, hires_movement = (int) move_floor;
            _device->virtualInput()->moveAxis(_input_axis.value(), hires_movement);
            hires_remainder += hires_movement;
            if (abs(hires_remainder) >= 60) {
                lowres_movement = hires_remainder / 120;
                if (lowres_movement == 0)
                    lowres_movement = hires_remainder > 0 ? 1 : -1;
                hires_remainder -= lowres_movement * 120;
                _device->virtualInput()->moveAxis(low_res_axis, lowres_movement);
            }

            _hires_remainder = hires_remainder;
        } else {
            _device->virtualInput()->moveAxis(_input_axis.value(), (int) move_floor);
        }
    }
    _axis = new_axis;
}

// Purpose: Report whether the accumulated movement has crossed the configured threshold.
// Inputs: None.
// Outputs: Threshold met flag.
// Used by: gesture dispatch.
bool AxisGesture::metThreshold() const {
    std::shared_lock lock(_config_mutex);
    return _axis >= _config.threshold.value_or(defaults::gesture_threshold);
}

// Purpose: Axis gestures are compatible with wheel-style inputs.
// Inputs: None.
// Outputs: True.
// Used by: gesture compatibility checks.
bool AxisGesture::wheelCompatibility() const {
    return true;
}

// Purpose: Update the multiplier used for high-resolution wheels.
// Inputs: Multiplier value.
// Outputs: Internal scaling update.
// Used by: IPC setters.
void AxisGesture::setHiresMultiplier(double multiplier) {
    _hires_multiplier = multiplier;
    if (_input_axis.has_value()) {
        if (InputDevice::getLowResAxis(_input_axis.value()) != -1)
            _multiplier = _config.axis_multiplier.value_or(1) * multiplier;
    }
}

// Purpose: Return the current axis, multiplier, and threshold settings.
// Inputs: None.
// Outputs: Axis config tuple.
// Used by: IPC getters.
std::tuple<std::string, double, int> AxisGesture::getConfig() const {
    std::shared_lock lock(_config_mutex);
    std::string axis;
    if (_config.axis.has_value()) {
        if (std::holds_alternative<std::string>(_config.axis.value())) {
            axis = std::get<std::string>(_config.axis.value());
        } else {
            axis = _device->virtualInput()->toAxisName(std::get<uint>(_config.axis.value()));
        }
    }

    return {axis, _config.axis_multiplier.value_or(1), _config.threshold.value_or(0)};
}

// Purpose: Replace the target axis and register it on the virtual input device.
// Inputs: Axis name.
// Outputs: Config and registration update.
// Used by: IPC setters.
void AxisGesture::setAxis(const std::string& axis) {
    std::unique_lock lock(_config_mutex);
    if (axis.empty()) {
        _config.axis.reset();
        _input_axis.reset();
    } else {
        _input_axis = _device->virtualInput()->toAxisCode(axis);
        _config.axis = axis;
        _device->virtualInput()->registerAxis(_input_axis.value());
    }
    setHiresMultiplier(_hires_multiplier);
}

// Purpose: Replace the scale factor applied to outgoing movement.
// Inputs: Multiplier value.
// Outputs: Config update.
// Used by: IPC setters.
void AxisGesture::setMultiplier(double multiplier) {
    std::unique_lock lock(_config_mutex);
    _config.axis_multiplier = multiplier;
    _multiplier = multiplier;
    setHiresMultiplier(_hires_multiplier);
}

// Purpose: Replace the threshold used before movement is emitted.
// Inputs: Threshold value.
// Outputs: Config update.
// Used by: IPC setters.
void AxisGesture::setThreshold(int threshold) {
    std::unique_lock lock(_config_mutex);
    if (threshold == 0)
        _config.threshold.reset();
    else
        _config.threshold = threshold;
}
