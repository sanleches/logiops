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
 * File: backend/hidpp20/Feature.cpp
 *
 * Base implementation for HID++ 2.0 feature wrappers. Features resolve their
 * runtime index through the ROOT feature, then delegate function calls back to
 * the owning device so higher-level modules can interact with hardware using a
 * small, typed abstraction instead of raw report assembly.
 */

#include <backend/hidpp20/Feature.h>
#include <backend/hidpp20/Device.h>
#include <backend/hidpp20/features/Root.h>

using namespace logid::backend::hidpp20;

// Purpose: Describe an unsupported HID++ 2.0 feature.
// Inputs: Stored feature ID.
// Outputs: Short `what()` text.
// Used by: feature construction and fallback handling.
const char* UnsupportedFeature::what() const noexcept {
    return "Unsupported feature";
}

// Purpose: Return the unsupported feature ID.
// Inputs: None.
// Outputs: Feature ID.
// Used by: error reporting and fallbacks.
uint16_t UnsupportedFeature::code() const noexcept {
    return _f_id;
}

// Purpose: Invoke a feature function and wait for its payload.
// Inputs: Function ID and parameter bytes.
// Outputs: Response payload bytes.
// References: owning device transport.
std::vector<uint8_t> Feature::callFunction(uint8_t function_id,
                                           std::vector<uint8_t>& params) {
    return _device->callFunction(_index, function_id, params);
}

// Purpose: Invoke a feature function without waiting for a payload.
// Inputs: Function ID and parameter bytes.
// Outputs: Raw report bytes written to the device.
// Used by: no-ACK feature paths.
void Feature::callFunctionNoResponse(uint8_t function_id,
                                      std::vector<uint8_t>& params) {
    _device->callFunctionNoResponse(_index, function_id, params);
}

// Purpose: Resolve a runtime feature index from a compile-time feature ID.
// Inputs: Owning device and compile-time feature ID.
// Outputs: Runtime feature index or `UnsupportedFeature`.
// References: `Root::GetFeature`.
Feature::Feature(Device* dev, uint16_t _id) : _device(dev) {
    _index = hidpp20::FeatureID::ROOT;

    if (_id) {
        std::vector<uint8_t> getFunc_req(2);
        getFunc_req[0] = (_id >> 8) & 0xff;
        getFunc_req[1] = _id & 0xff;

        try {
            auto getFunc_resp = this->callFunction(Root::GetFeature, getFunc_req);
            _index = getFunc_resp[0];
        } catch (Error& e) {
            if (e.code() == Error::InvalidFeatureIndex)
                throw UnsupportedFeature(_id);
            throw e;
        }

        // 0 if not found.
        if (!_index)
            throw UnsupportedFeature(_id);
    }
}

// Return the resolved feature index used in HID++ requests.
uint8_t Feature::featureIndex() const {
    return _index;
}
