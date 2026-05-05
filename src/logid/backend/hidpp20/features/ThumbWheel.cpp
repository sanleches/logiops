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
 * File: ThumbWheel.cpp
 *
 * HID++ 2.0 thumb-wheel protocol wrapper. This module exposes the wheel
 * capability block, current status, and event decoding helpers used by the
 * thumb-wheel feature wrapper.
 */

#include <backend/hidpp20/features/ThumbWheel.h>
#include <cassert>

using namespace logid::backend::hidpp20;

// Purpose: Bind the thumb-wheel feature to the device.
// Inputs: HID++ device.
// Outputs: Thumb-wheel wrapper.
// Used by: higher-level thumb-wheel feature wrapper.
ThumbWheel::ThumbWheel(Device* dev) : Feature(dev, ID) {
}

// Purpose: Read the wheel's resolution and feature flags.
// Inputs: None.
// Outputs: Capability information.
// Used by: thumb-wheel configuration.
ThumbWheel::ThumbwheelInfo ThumbWheel::getInfo() {
    std::vector<uint8_t> params(0), response;
    ThumbwheelInfo info{};
    response = callFunction(GetInfo, params);

    info.nativeRes = response[1];
    info.nativeRes |= (response[0] << 8);
    info.divertedRes = response[3];
    info.divertedRes |= (response[2] << 8);
    info.defaultDirection = response[4] ? 1 : -1; /* 1 increment to the right */
    info.capabilities = response[5];
    info.timeElapsed = response[7];
    info.timeElapsed |= response[6] << 8;

    return info;
}

// Purpose: Read the current divert/touch/proxy state.
// Inputs: None.
// Outputs: Status structure.
// Used by: thumb-wheel configuration.
ThumbWheel::ThumbwheelStatus ThumbWheel::getStatus() {
    std::vector<uint8_t> params(0), response;
    ThumbwheelStatus status{};
    response = callFunction(GetStatus, params);

    status.diverted = response[0];
    status.inverted = response[1] & 1;
    status.touch = response[1] & (1 << 1);
    status.proxy = response[1] & (1 << 2);

    return status;
}

// Purpose: Enable divert mode and optionally invert the wheel.
// Inputs: Divert and invert flags.
// Outputs: Status structure from hardware.
// Used by: thumb-wheel configuration.
ThumbWheel::ThumbwheelStatus ThumbWheel::setStatus(bool divert, bool invert) {
    std::vector<uint8_t> params(2), response;
    ThumbwheelStatus status{};
    params[0] = divert;
    params[1] = invert;

    response = callFunction(SetReporting, params);
    status.diverted = response[0];
    status.inverted = response[1] & 1;

    return status;
}

// Purpose: Decode one thumb-wheel report into a typed event.
// Inputs: One HID++ report.
// Outputs: Parsed thumb-wheel event.
// Used by: wheel event handling.
ThumbWheel::ThumbwheelEvent ThumbWheel::thumbwheelEvent(const hidpp::Report& report) {
    assert(report.function() == Event);
    ThumbwheelEvent event{};
    event.rotation = (int16_t) ((report.paramBegin()[0] << 8) | report.paramBegin()[1]);
    event.timestamp = report.paramBegin()[3];
    event.timestamp |= report.paramBegin()[2] << 8;
    event.rotationStatus = static_cast<RotationStatus>(report.paramBegin()[4]);
    event.flags = report.paramBegin()[5];
    return event;
}
