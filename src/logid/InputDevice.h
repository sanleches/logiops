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

#ifndef LOGID_INPUTDEVICE_H
#define LOGID_INPUTDEVICE_H

#include <memory>
#include <string>
#include <mutex>

extern "C"
{
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
}

namespace logid {
    // Wraps libevdev/uinput so actions can synthesize keyboard and relative
    // events on a single shared virtual device.
    class InputDevice {
    public:
        // Thrown when the code/name lookup does not map to a known input event.
        class InvalidEventCode : public std::exception {
        public:
            explicit InvalidEventCode(const std::string& name);

            explicit InvalidEventCode(uint code);

            const char* what() const noexcept override;

        private:
            const std::string _what;
        };

        // Create the virtual input device and pre-register common keyboard keys.
        // The daemon uses this to emit events that look like real keyboard/mouse input.
        explicit InputDevice(const char* name);

        ~InputDevice();

        // Register additional key or axis codes on demand.
        // libevdev needs the event type/code enabled before the kernel will accept it.
        void registerKey(uint code);

        void registerAxis(uint axis);

        void moveAxis(uint axis, int movement);

        void pressKey(uint code);

        void releaseKey(uint code);

        static std::string toKeyName(uint code);

        static uint toKeyCode(const std::string& name);

        static std::string toAxisName(uint code);

        static uint toAxisCode(const std::string& name);

        // Map high-resolution wheel axes back to their low-resolution equivalents when possible.
        // This helps gestures fall back cleanly on systems that only understand low-res wheels.
        static int getLowResAxis(uint axis_code);

    private:
        void _sendEvent(uint type, uint code, int value);

        void _enableEvent(uint type, uint name);

        static std::string _toEventName(uint type, uint code);

        static uint _toEventCode(uint type, const std::string& name);

        bool registered_keys[KEY_CNT]{};
        bool registered_axis[REL_CNT]{};
        libevdev* device;
        libevdev_uinput* ui_device{};

        std::mutex _input_mutex;
    };
}

#endif //LOGID_INPUTDEVICE_H
