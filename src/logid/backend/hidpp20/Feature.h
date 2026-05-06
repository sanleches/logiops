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

#ifndef LOGID_BACKEND_HIDPP20_FEATURE_H
#define LOGID_BACKEND_HIDPP20_FEATURE_H

#include <cstdint>
#include <exception>
#include <vector>

namespace logid::backend::hidpp20 {
    class Device;

    // Thrown when the device does not support a requested HID++ 2.0 feature.
    // This is how feature wrappers say "the hardware doesn't have that capability".
    class UnsupportedFeature : public std::exception {
    public:
        explicit UnsupportedFeature(uint16_t ID) : _f_id(ID) {}

        [[nodiscard]] const char* what() const noexcept override;

        [[nodiscard]] uint16_t code() const noexcept;

    private:
        uint16_t _f_id;
    };

    // Base class for HID++ 2.0 features bound to a device instance.
    // A concrete feature resolves its runtime index from the ROOT feature once,
    // then uses that index for all future calls.
    class Feature {
    public:
        static const uint16_t ID;

        // Return the feature's compile-time ID.
        // This is the stable feature ID from the Logitech HID++ tables.
        virtual uint16_t getID() = 0;

        // Return the runtime feature index used in actual HID++ requests.
        [[nodiscard]] uint8_t featureIndex() const;

        virtual ~Feature() = default;

    protected:
        explicit Feature(Device* dev, uint16_t _id);

        // Call the feature function and wait for a response.
        // This delegates to the parent device so response matching stays centralized.
        std::vector<uint8_t> callFunction(uint8_t function_id, std::vector<uint8_t>& params);

        // Call the feature function without waiting for a response.
        // Use this when the device is expected to drop the link or not acknowledge.
        void callFunctionNoResponse(uint8_t function_id, std::vector<uint8_t>& params);

        Device* const _device;
        uint8_t _index;
    };
}

#endif //LOGID_BACKEND_HIDPP20_FEATURE_H
