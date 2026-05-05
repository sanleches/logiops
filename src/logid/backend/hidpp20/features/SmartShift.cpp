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
 * File: SmartShift.cpp
 *
 * HID++ 2.0 smart-shift protocol wrapper. This file exposes the base and V2
 * smart-shift feature variants, plus helpers for reading defaults and writing
 * status values.
 */

#include <backend/hidpp20/features/SmartShift.h>

using namespace logid::backend::hidpp20;

// Purpose: Bind the base smart-shift feature to the device.
// Inputs: HID++ device.
// Outputs: Base smart-shift wrapper.
// Used by: version selection.
SmartShift::SmartShift(Device* dev) : SmartShift(dev, ID) {
}

SmartShift::SmartShift(Device* dev, uint16_t feature_id) :
        Feature(dev, feature_id) {
}

// Purpose: Bind the V2 smart-shift feature to the device.
// Inputs: HID++ device.
// Outputs: V2 smart-shift wrapper.
// Used by: version selection.
SmartShiftV2::SmartShiftV2(Device* dev) : SmartShift(dev, ID) {
}

template<typename T>
// Purpose: Instantiate the requested smart-shift version if supported.
// Inputs: HID++ device.
// Outputs: SmartShift wrapper or null.
// Used by: `autoVersion()`.
std::shared_ptr<T> make_smartshift(Device* dev) {
    try {
        return std::make_shared<T>(dev);
    } catch (UnsupportedFeature& e) {
        return {};
    }
}

// Purpose: Prefer the newest supported smart-shift implementation.
// Inputs: HID++ device.
// Outputs: Best supported smart-shift wrapper.
// Used by: higher-level smart-shift feature wrapper.
std::shared_ptr<SmartShift> SmartShift::autoVersion(Device* dev) {
    if (auto v2 = make_smartshift<SmartShiftV2>(dev))
        return v2;

    return std::make_shared<SmartShift>(dev);
}

// Purpose: Read the current smart-shift state.
// Inputs: None.
// Outputs: Live state structure.
// Used by: configuration and IPC.
SmartShift::Status SmartShift::getStatus() {
    std::vector<uint8_t> params(0);

    auto response = callFunction(GetStatus, params);

    return {
            .active = static_cast<bool>(response[0] - 1),
            .autoDisengage = response[1],
            .torque = 0,
            .setActive = false,
            .setAutoDisengage = false,
            .setTorque = false
    };
}

// Purpose: Read the default smart-shift values.
// Inputs: None.
// Outputs: Default values structure.
// Used by: IPC and reset paths.
SmartShift::Defaults SmartShift::getDefaults() {
    std::vector<uint8_t> params(0);

    auto response = callFunction(GetStatus, params);

    return {
            .autoDisengage = response[2],
            .torque = 0,
            .maxForce = 0,
    };
}

// Purpose: Write the requested smart-shift state.
// Inputs: Status update.
// Outputs: Hardware state updated.
// Used by: feature wrappers and IPC.
void SmartShift::setStatus(Status status) {
    std::vector<uint8_t> params(3);
    if (status.setActive)
        params[0] = status.active + 1;
    if (status.setAutoDisengage)
        params[1] = status.autoDisengage;
    callFunction(SetStatus, params);
}

// Purpose: Read the V2 capability block for defaults.
// Inputs: None.
// Outputs: Default capability values.
// Used by: V2 configuration.
SmartShift::Defaults SmartShiftV2::getDefaults() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetCapabilities, params);

    return {
            .autoDisengage = response[1],
            .torque = response[2],
            .maxForce = response[3],
    };
}

// Purpose: Read the current V2 smart-shift state.
// Inputs: None.
// Outputs: Live V2 state structure.
// Used by: configuration and IPC.
SmartShift::Status SmartShiftV2::getStatus() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetStatus, params);

    return {
            .active = static_cast<bool>(response[0] - 1),
            .autoDisengage = response[1],
            .torque = response[2],
            .setActive = false, .setAutoDisengage = false, .setTorque = false,
    };
}

// Purpose: Write the requested V2 smart-shift state.
// Inputs: Status update.
// Outputs: Hardware state updated.
// Used by: feature wrappers and IPC.
void SmartShiftV2::setStatus(Status status) {
    std::vector<uint8_t> params(3);
    if (status.setActive)
        params[0] = status.active + 1;
    if (status.setAutoDisengage)
        params[1] = status.autoDisengage;
    if (status.setTorque)
        params[2] = status.torque;

    callFunction(SetStatus, params);
}

// Purpose: Report whether torque control is supported.
// Inputs: None.
// Outputs: Support flag.
// Used by: UI and IPC.
bool SmartShiftV2::supportsTorque() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetCapabilities, params);

    return static_cast<bool>(response[0] & 1);
}
