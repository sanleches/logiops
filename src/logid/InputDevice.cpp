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

#include <InputDevice.h>
#include <system_error>
#include <mutex>

extern "C"
{
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
}

using namespace logid;

// Build a descriptive exception message for invalid event names or codes.
InputDevice::InvalidEventCode::InvalidEventCode(const std::string& name) :
        _what("Invalid event code " + name) {
}

InputDevice::InvalidEventCode::InvalidEventCode(uint code) :
        _what("Invalid event code " + std::to_string(code)) {
}

const char* InputDevice::InvalidEventCode::what() const noexcept {
    return _what.c_str();
}

// Create the virtual uinput device and seed it with the common keyboard capabilities.
// The initial device only knows a small set of keys; more are added lazily.
InputDevice::InputDevice(const char* name) {
    device = libevdev_new();
    libevdev_set_name(device, name);

    libevdev_enable_event_type(device, EV_KEY);
    for (unsigned int i = 0; i < KEY_CNT; i++) {
        // Pre-enable the common keyboard range so synthesized input works out of the box.
        // Most actions only need standard keyboard codes, so this covers the common case.
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

// Make sure a key code is exported before clients try to emit it.
// This may rebuild the uinput device because libevdev does not let us add codes
// to an already-created virtual device in place.
void InputDevice::registerKey(uint code) {
    // TODO: Maybe print error message, if wrong code is passed?
    if (code >= KEY_CNT || registered_keys[code]) {
        return;
    }

    _enableEvent(EV_KEY, code);

    registered_keys[code] = true;
}

// Make sure a relative axis is exported before clients try to emit it.
// Like keys, axes have to be declared before the kernel will accept events.
void InputDevice::registerAxis(uint axis) {
    // TODO: Maybe print error message, if wrong code is passed?
    if (axis >= REL_CNT || registered_axis[axis]) {
        return;
    }

    _enableEvent(EV_REL, axis);

    registered_axis[axis] = true;
}

// Send a motion event for the requested relative axis.
void InputDevice::moveAxis(uint axis, int movement) {
    _sendEvent(EV_REL, axis, movement);
}

// Emit a press or release event for a key.
void InputDevice::pressKey(uint code) {
    _sendEvent(EV_KEY, code, 1);
}

void InputDevice::releaseKey(uint code) {
    _sendEvent(EV_KEY, code, 0);
}

// Translate a numeric code into its libevdev name.
std::string InputDevice::toKeyName(uint code) {
    return _toEventName(EV_KEY, code);
}

// Translate an input name back into its numeric key code.
uint InputDevice::toKeyCode(const std::string& name) {
    return _toEventCode(EV_KEY, name);
}

std::string InputDevice::toAxisName(uint code) {
    return _toEventName(EV_REL, code);
}

uint InputDevice::toAxisCode(const std::string& name) {
    return _toEventCode(EV_REL, name);
}

/* Returns -1 if axis_code is not hi-res */
// Return the low-resolution wheel axis for a high-resolution wheel code when available.
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

// Resolve a libevdev event name from a numeric code.
std::string InputDevice::_toEventName(uint type, uint code) {
    const char* ret = libevdev_event_code_get_name(type, code);

    if (!ret)
        throw InvalidEventCode(code);

    return {ret};
}

// Resolve a numeric event code from a libevdev name.
uint InputDevice::_toEventCode(uint type, const std::string& name) {
    int code = libevdev_event_code_from_name(type, name.c_str());

    if (code == -1)
        throw InvalidEventCode(name);

    return code;
}

// Recreate the uinput device after adding support for a new event type/code.
// This is required because a uinput device's supported codes are fixed when it is created.
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

// Emit a single input event followed by SYN_REPORT so the kernel sees a complete frame.
// The SYN_REPORT is what tells the kernel "this input packet is done".
void InputDevice::_sendEvent(uint type, uint code, int value) {
    std::unique_lock lock(_input_mutex);
    libevdev_uinput_write_event(ui_device, type, code, value);
    libevdev_uinput_write_event(ui_device, EV_SYN, SYN_REPORT, 0);
}
