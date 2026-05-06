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
 * File: InputDevice.cpp
 *
 * Virtual input device wrapper. This file owns the synthetic uinput device the
 * daemon writes into, plus the code that expands the supported event set as new
 * keys or axes are needed by actions and features.
 */

#include <InputDevice.h>
#include <system_error>
#include <mutex>

extern "C"
{
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
}

using namespace logid;

// Purpose: Build a readable exception for invalid event names or codes.
// Inputs: A name or numeric code.
// Outputs: Exception text describing the missing event.
// Used by: event-name/code conversion helpers.
InputDevice::InvalidEventCode::InvalidEventCode(const std::string& name) :
        _what("Invalid event code " + name) {
}

InputDevice::InvalidEventCode::InvalidEventCode(uint code) :
        _what("Invalid event code " + std::to_string(code)) {
}

const char* InputDevice::InvalidEventCode::what() const noexcept {
    return _what.c_str();
}

// Purpose: Create the virtual uinput device and seed common capabilities.
// Inputs: Device name.
// Outputs: A live uinput-backed virtual device.
// Used by: the daemon's synthetic input path.
InputDevice::InputDevice(const char* name) {
    device = libevdev_new();
    libevdev_set_name(device, name);

    libevdev_enable_event_type(device, EV_KEY);
    for (unsigned int i = 0; i < KEY_CNT; i++) {
        // Pre-enable the common keyboard range so synthesized input works out of
        // the box. Most actions only need standard keyboard codes, so this
        // covers the common case without extra device rebuilds.
        if (i < 128) {
            registered_keys[i] = true;
            libevdev_enable_event_code(device, EV_KEY, i, nullptr);
        } else {
            registered_keys[i] = false;
        }
    }

    for (bool& axis: registered_axis)
        axis = false;

    libevdev_enable_event_type(device, EV_REL);

    int err = libevdev_uinput_create_from_device(device,
                                                 LIBEVDEV_UINPUT_OPEN_MANAGED, &ui_device);

    if (err != 0) {
        libevdev_free(device);
        throw std::system_error(-err, std::generic_category());
    }
}

InputDevice::~InputDevice() {
    libevdev_uinput_destroy(ui_device);
    libevdev_free(device);
}

// Purpose: Export a key code before it is used.
// Inputs: Numeric key code.
// Outputs: The virtual device supports that key.
// Used by: keypress actions.
void InputDevice::registerKey(uint code) {
    // TODO: Maybe print error message, if wrong code is passed?
    if (code >= KEY_CNT || registered_keys[code]) {
        return;
    }

    _enableEvent(EV_KEY, code);

    registered_keys[code] = true;
}

// Purpose: Export a relative axis before it is used.
// Inputs: Numeric axis code.
// Outputs: The virtual device supports that axis.
// Used by: scroll and motion actions.
void InputDevice::registerAxis(uint axis) {
    // TODO: Maybe print error message, if wrong code is passed?
    if (axis >= REL_CNT || registered_axis[axis]) {
        return;
    }

    _enableEvent(EV_REL, axis);

    registered_axis[axis] = true;
}

// Purpose: Send one relative motion event.
// Inputs: Axis code and movement value.
// Outputs: Kernel receives the relative movement.
// Used by: motion and scroll actions.
void InputDevice::moveAxis(uint axis, int movement) {
    _sendEvent(EV_REL, axis, movement);
}

// Purpose: Emit a key press event.
// Inputs: Numeric key code.
// Outputs: Kernel receives a pressed key state.
// Used by: keypress actions.
void InputDevice::pressKey(uint code) {
    _sendEvent(EV_KEY, code, 1);
}

// Purpose: Emit a key release event.
// Inputs: Numeric key code.
// Outputs: Kernel receives a released key state.
// Used by: keypress actions.
void InputDevice::releaseKey(uint code) {
    _sendEvent(EV_KEY, code, 0);
}

// Purpose: Translate a numeric key code to a libevdev name.
// Inputs: Numeric key code.
// Outputs: Event name string.
// Used by: IPC and diagnostics.
std::string InputDevice::toKeyName(uint code) {
    return _toEventName(EV_KEY, code);
}

// Purpose: Translate a key name to a numeric code.
// Inputs: Event name string.
// Outputs: Numeric key code.
// Used by: action configuration parsing.
uint InputDevice::toKeyCode(const std::string& name) {
    return _toEventCode(EV_KEY, name);
}

// Purpose: Translate a numeric axis code to a libevdev name.
// Inputs: Numeric axis code.
// Outputs: Event name string.
// Used by: IPC and diagnostics.
std::string InputDevice::toAxisName(uint code) {
    return _toEventName(EV_REL, code);
}

// Purpose: Translate an axis name to a numeric code.
// Inputs: Event name string.
// Outputs: Numeric axis code.
// Used by: action configuration parsing.
uint InputDevice::toAxisCode(const std::string& name) {
    return _toEventCode(EV_REL, name);
}

// Purpose: Map a hi-res wheel axis to its low-res equivalent.
// Inputs: Relative axis code.
// Outputs: Matching low-res axis or `-1`.
// Used by: scroll feature handling.
int InputDevice::getLowResAxis(const uint axis_code) {
    /* Some systems don't have these hi-res axes */
#ifdef REL_WHEEL_HI_RES
    if (axis_code == REL_WHEEL_HI_RES)
        return REL_WHEEL;
#endif
#ifdef REL_HWHEEL_HI_RES
    if (axis_code == REL_HWHEEL_HI_RES)
        return REL_HWHEEL;
#endif

    return -1;
}

// Purpose: Resolve a libevdev event name from a numeric code.
// Inputs: Event type and code.
// Outputs: Event name string or exception.
// Used by: public name helpers.
std::string InputDevice::_toEventName(uint type, uint code) {
    const char* ret = libevdev_event_code_get_name(type, code);

    if (!ret)
        throw InvalidEventCode(code);

    return {ret};
}

// Purpose: Resolve a numeric event code from a libevdev name.
// Inputs: Event type and name.
// Outputs: Numeric event code or exception.
// Used by: public code helpers.
uint InputDevice::_toEventCode(uint type, const std::string& name) {
    int code = libevdev_event_code_from_name(type, name.c_str());

    if (code == -1)
        throw InvalidEventCode(name);

    return code;
}

// Purpose: Recreate the uinput device after adding a new event type/code.
// Inputs: Event type and code.
// Outputs: The virtual device now supports that event.
// Used by: key and axis registration.
void InputDevice::_enableEvent(const uint type, const uint code) {
    std::unique_lock lock(_input_mutex);
    libevdev_uinput_destroy(ui_device);

    libevdev_enable_event_code(device, type, code, nullptr);

    int err = libevdev_uinput_create_from_device(device,
                                                 LIBEVDEV_UINPUT_OPEN_MANAGED, &ui_device);

    if (err != 0) {
        libevdev_free(device);
        device = nullptr;
        ui_device = nullptr;
        throw std::system_error(-err, std::generic_category());
    }
}

// Purpose: Emit one complete input frame.
// Inputs: Event type, code, and value.
// Outputs: The kernel receives the event plus SYN_REPORT.
// Used by: key and axis send helpers.
void InputDevice::_sendEvent(uint type, uint code, int value) {
    std::unique_lock lock(_input_mutex);
    libevdev_uinput_write_event(ui_device, type, code, value);
    libevdev_uinput_write_event(ui_device, EV_SYN, SYN_REPORT, 0);
}
