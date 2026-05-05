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
#ifndef LOGID_FEATURE_DPI_H
#define LOGID_FEATURE_DPI_H

#include <backend/hidpp20/features/AdjustableDPI.h>
#include <features/DeviceFeature.h>
#include <config/schema.h>
#include <ipcgull/interface.h>
#include <shared_mutex>

namespace logid::features {
    // Feature that manages per-sensor DPI settings and exposes them over IPC.
    // Some Logitech devices expose one DPI value per sensor, while others only
    // have a single shared DPI. This wrapper hides that difference from IPC.
    class DPI : public DeviceFeature {
    public:
        // Apply configured DPI values to the device.
        void configure() final;

        // No runtime event stream is needed for DPI changes.
        void listen() final;

        // Swap to a different profile's DPI settings.
        void setProfile(config::Profile& profile) final;

        // Read the current DPI for one sensor.
        uint16_t getDPI(uint8_t sensor = 0);

        // Set the DPI for one sensor, clamping to supported values.
        void setDPI(uint16_t dpi, uint8_t sensor = 0);

    protected:
        explicit DPI(Device* dev);

    private:
        void _fillDPILists(uint8_t sensor);

        class IPC : public ipcgull::interface {
        public:
            explicit IPC(DPI* parent);

            [[nodiscard]] uint8_t getSensors() const;

            [[nodiscard]] std::tuple<std::vector<uint16_t>, uint16_t, bool> getDPIs(
                    uint8_t sensor) const;

            [[nodiscard]] uint16_t getDPI(uint8_t sensor) const;

            void setDPI(uint16_t dpi, uint8_t sensor);

        private:
            DPI& _parent;
        };

        mutable std::shared_mutex _config_mutex;
        std::reference_wrapper<std::optional<config::DPI>> _config;
        std::shared_ptr<backend::hidpp20::AdjustableDPI> _adjustable_dpi;
        mutable std::shared_mutex _dpi_list_mutex;
        std::vector<backend::hidpp20::AdjustableDPI::SensorDPIList> _dpi_lists;

        std::shared_ptr<IPC> _ipc_interface;
    };
}

#endif //LOGID_FEATURE_DPI_H
