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
 * File: HiresScroll.cpp
 *
 * HID++ 2.0 hi-res scroll protocol wrapper. This module exposes wheel mode,
 * capability, and event decoding helpers used by the higher-level scroll
 * feature wrapper.
 */

#include <backend/hidpp20/features/HiresScroll.h>
#include <cassert>

using namespace logid::backend::hidpp20;

// Purpose: Bind the hi-res scroll feature to the device.
// Inputs: HID++ device.
// Outputs: Hi-res scroll wrapper.
// Used by: higher-level scroll feature wrapper.
HiresScroll::HiresScroll(Device* device) : Feature(device, ID) {
}

// Purpose: Read the capability block.
// Inputs: None.
// Outputs: Capabilities structure.
// Used by: scroll configuration.
HiresScroll::Capabilities HiresScroll::getCapabilities() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetCapabilities, params);

    Capabilities capabilities{};
    capabilities.multiplier = response[0];
    capabilities.flags = response[1];
    return capabilities;
}

// Purpose: Read the current mode bits.
// Inputs: None.
// Outputs: Hardware mode value.
// Used by: scroll configuration.
uint8_t HiresScroll::getMode() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetMode, params);
    return response[0];
}

// Purpose: Write new mode bits.
// Inputs: Mode value.
// Outputs: Hardware mode updated.
// Used by: scroll configuration.
void HiresScroll::setMode(uint8_t mode) {
    std::vector<uint8_t> params(1);
    params[0] = mode;
    callFunction(SetMode, params);
}

[[maybe_unused]] bool HiresScroll::getRatchetState() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetRatchetState, params);
    return params[0];
}

// Purpose: Decode a wheel movement event.
// Inputs: One HID++ report.
// Outputs: Wheel status structure.
// Used by: scroll event handling.
HiresScroll::WheelStatus HiresScroll::wheelMovementEvent(const hidpp::Report& report) {
    assert(report.function() == WheelMovement);
    WheelStatus status{};
    status.hiRes = report.paramBegin()[0] & 1 << 4;
    status.periods = report.paramBegin()[0] & 0x0F;
    status.deltaV = (int16_t) (report.paramBegin()[1] << 8 | report.paramBegin()[2]);
    return status;
}

[[maybe_unused]]
// Purpose: Decode a ratchet switch event.
// Inputs: One HID++ report.
// Outputs: Ratchet state enum.
// Used by: scroll event handling.
HiresScroll::RatchetState HiresScroll::ratchetSwitchEvent(const hidpp::Report& report) {
    assert(report.function() == RatchetSwitch);
    // Possible bad cast
    return static_cast<RatchetState>(report.paramBegin()[0]);
}
