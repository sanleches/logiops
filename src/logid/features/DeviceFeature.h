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

#ifndef LOGID_FEATURES_DEVICEFEATURE_H
#define LOGID_FEATURES_DEVICEFEATURE_H

#include <memory>
#include <string>

namespace logid {
    class Device;
}

namespace logid::config {
    struct Profile;
}

namespace logid::features {
    // Base helper for device-level features that live across profiles.
    class UnsupportedFeature : public std::exception {
    public:
        UnsupportedFeature() = default;

        [[nodiscard]] const char* what() const noexcept override {
            return "Unsupported feature";
        }
    };

    template<typename T>
    // Small helper that exposes `T` through `shared_ptr` while keeping the
    // constructor protected. This lets feature code create managed instances
    // without exposing raw `new` or public constructors everywhere.
    class _featureWrapper : public T {
        friend class DeviceFeature;

    public:
        template<typename... Args>
        explicit _featureWrapper(Args... args) : T(std::forward<Args>(args)...) {}

        template<typename... Args>
        static std::shared_ptr<T> make(Args... args) {
            return std::make_shared<_featureWrapper>(std::forward<Args>(args)...);
        }
    };

    // Base class for per-device features such as DPI, remapping, and scroll mode.
    class DeviceFeature {
        std::weak_ptr<DeviceFeature> _self;
    public:
        // Apply the current profile's configuration to the device.
        virtual void configure() = 0;

        // Register runtime event handlers used while the device is active.
        virtual void listen() = 0;

        // Switch the feature to a different profile fragment.
        virtual void setProfile(config::Profile& profile) = 0;

        virtual ~DeviceFeature() = default;

        DeviceFeature(const DeviceFeature&) = delete;

        DeviceFeature(DeviceFeature&&) = delete;

    protected:
        // Store the owning device pointer.
        explicit DeviceFeature(Device* dev) : _device(dev) {}

        Device* _device;

        // Return a typed weak pointer to the current concrete feature instance.
        // Features use this inside async callbacks so they can stop safely if the
        // parent device has already been destroyed.
        template<typename T>
        [[nodiscard]] std::weak_ptr<T> self() const {
            return std::dynamic_pointer_cast<T>(_self.lock());
        }

    public:
        // Create a managed feature object and store a weak self-reference so
        // later callbacks can tell whether the feature is still alive.
        template<typename T, typename... Args>
        static std::shared_ptr<T> make(Args... args) {
            auto feature = _featureWrapper<T>::make(std::forward<Args>(args)...);
            feature->_self = feature;

            return feature;
        }
    };
}

#endif //LOGID_FEATURES_DEVICEFEATURE_H
