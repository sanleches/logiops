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
#ifndef LOGID_FEATURE_SMARTSHIFT_H
#define LOGID_FEATURE_SMARTSHIFT_H

#include <features/DeviceFeature.h>
#include <backend/hidpp20/features/SmartShift.h>
#include <ipcgull/interface.h>
#include <config/schema.h>
#include <shared_mutex>

namespace logid::features {
    // Feature that controls the scrolling resistance / freewheel behavior.
    // Logitech uses SmartShift to switch the wheel between ratcheted and free
    // spinning modes, sometimes with extra tuning for how quickly it disengages.
    class SmartShift : public DeviceFeature {
    public:
        // Apply SmartShift configuration from the current profile.
        void configure() final;

        // SmartShift does not need a runtime event handler.
        void listen() final;

        // Swap to a different profile's SmartShift settings.
        void setProfile(config::Profile& profile) final;

        typedef backend::hidpp20::SmartShift::Status Status;

        // Read the current device status.
        [[nodiscard]] Status getStatus() const;

        // Write one or more SmartShift fields back to the device.
        void setStatus(Status status);

        // Return the device's default SmartShift values.
        [[nodiscard]] const backend::hidpp20::SmartShift::Defaults& getDefaults() const;

        // Tell callers whether the device exposes the torque control field.
        [[nodiscard]] bool supportsTorque() const;

    protected:
        explicit SmartShift(Device* dev);

    private:
        mutable std::shared_mutex _config_mutex;
        std::reference_wrapper<std::optional<config::SmartShift>> _config;
        std::shared_ptr<backend::hidpp20::SmartShift> _smartshift;

        backend::hidpp20::SmartShift::Defaults _defaults{};
        bool _torque_support = false;

        class IPC : public ipcgull::interface {
        public:
            explicit IPC(SmartShift* parent);

            [[nodiscard]] std::tuple<uint8_t, uint8_t, uint8_t> getConfig() const;

            void setActive(bool active, bool clear);

            void setThreshold(uint8_t threshold, bool clear);

            void setTorque(uint8_t torque, bool clear);

        private:
            SmartShift& _parent;
        };

        std::shared_ptr<IPC> _ipc_interface;
    };
}

#endif //LOGID_FEATURE_SMARTSHIFT_H
