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
 * File: Error.cpp
 *
 * HID++ 1.0 register error wrapper. This module stores raw register error
 * codes and exposes them as daemon-readable exceptions.
 */

#include <backend/hidpp10/Error.h>
#include <cassert>

using namespace logid::backend;
using namespace logid::backend::hidpp10;

// Purpose: Record a HID++ 1.0 register error for later inspection.
// Inputs: Error code and device index.
// Outputs: Stored exception state.
// Used by: register access paths.
Error::Error(uint8_t code, hidpp::DeviceIndex index) : _code(code), _index(index) {
    assert(code != Success);
}

// Purpose: Return a descriptive message for the stored error code.
// Inputs: None.
// Outputs: Static error string.
// Used by: exception reporting.
const char* Error::what() const noexcept {
    switch (_code) {
        case Success:
            return "Success";
        case InvalidSubID:
            return "Invalid sub ID";
        case InvalidAddress:
            return "Invalid address";
        case InvalidValue:
            return "Invalid value";
        case ConnectFail:
            return "Connection failure";
        case TooManyDevices:
            return "Too many devices";
        case AlreadyExists:
            return "Already exists";
        case Busy:
            return "Busy";
        case UnknownDevice:
            return "Unknown device";
        case ResourceError:
            return "Resource error";
        case RequestUnavailable:
            return "Request unavailable";
        case InvalidParameterValue:
            return "Invalid parameter value";
        case WrongPINCode:
            return "Wrong PIN code";
        default:
            return "Unknown error code";
    }
}

// Purpose: Return the raw HID++ 1.0 error code.
// Inputs: None.
// Outputs: Error code byte.
// Used by: error handling.
uint8_t Error::code() const noexcept {
    return _code;
}

// Purpose: Return the device index that generated the error.
// Inputs: None.
// Outputs: Device index.
// Used by: error handling.
hidpp::DeviceIndex Error::deviceIndex() const noexcept {
    return _index;
}
