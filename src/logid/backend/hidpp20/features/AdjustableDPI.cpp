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
 * File: AdjustableDPI.cpp
 *
 * HID++ 2.0 adjustable-DPI wrapper. This module exposes the sensor count,
 * supported DPI lists, current values, and DPI writes for devices that support
 * per-sensor DPI control.
 */

#include <backend/hidpp20/features/AdjustableDPI.h>

using namespace logid::backend::hidpp20;

// Purpose: Bind the adjustable-DPI feature to the device.
// Inputs: HID++ device.
// Outputs: Adjustable-DPI wrapper.
// Used by: DPI feature wrapper.
AdjustableDPI::AdjustableDPI(Device* dev) : Feature(dev, ID) {
}

// Purpose: Return the number of sensors that report DPI values.
// Inputs: None.
// Outputs: Sensor count.
// Used by: DPI feature setup.
uint8_t AdjustableDPI::getSensorCount() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetSensorCount, params);
    return response[0];
}

// Purpose: Parse the device's supported DPI list for one sensor.
// Inputs: Sensor index.
// Outputs: Supported DPI list/range description.
// Used by: DPI normalization.
AdjustableDPI::SensorDPIList AdjustableDPI::getSensorDPIList(uint8_t sensor) {
    SensorDPIList dpi_list{};
    std::vector<uint8_t> params(1);
    params[0] = sensor;
    auto response = callFunction(GetSensorDPIList, params);

    dpi_list.dpiStep = false;
    for (std::size_t i = 1; i < response.size(); i += 2) {
        uint16_t dpi = response[i + 1];
        dpi |= (response[i] << 8);
        if (!dpi)
            break;
        if (dpi >= 0xe000) {
            dpi_list.isRange = true;
            dpi_list.dpiStep = dpi - 0xe000;
        } else {
            dpi_list.dpis.push_back(dpi);
        }
    }

    return dpi_list;
}

// Purpose: Read the sensor's factory-default DPI.
// Inputs: Sensor index.
// Outputs: Default DPI value.
// Used by: DPI feature logic.
uint16_t AdjustableDPI::getDefaultSensorDPI(uint8_t sensor) {
    std::vector<uint8_t> params(1);
    params[0] = sensor;
    auto response = callFunction(GetSensorDPI, params);

    uint16_t default_dpi = response[4];
    default_dpi |= (response[3] << 8);

    return default_dpi;
}

// Purpose: Read the sensor's current DPI.
// Inputs: Sensor index.
// Outputs: Live DPI value.
// Used by: DPI feature logic and IPC.
uint16_t AdjustableDPI::getSensorDPI(uint8_t sensor) {
    std::vector<uint8_t> params(1);
    params[0] = sensor;
    auto response = callFunction(GetSensorDPI, params);

    uint16_t dpi = response[2];
    dpi |= (response[1] << 8);

    return dpi;
}

// Purpose: Write a new DPI value for one sensor.
// Inputs: Sensor index and DPI value.
// Outputs: Hardware DPI updated.
// Used by: DPI setters.
void AdjustableDPI::setSensorDPI(uint8_t sensor, uint16_t dpi) {
    std::vector<uint8_t> params(3);
    params[0] = sensor;
    params[1] = (dpi >> 8);
    params[2] = (dpi & 0xFF);
    callFunction(SetSensorDPI, params);
}
