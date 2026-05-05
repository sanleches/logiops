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
#ifndef LOGID_ACTION_CHANGEDPI_H
#define LOGID_ACTION_CHANGEDPI_H

#include <actions/Action.h>
#include <features/DPI.h>

namespace logid::actions {
    // Action that changes the mouse DPI by a configured amount when pressed.
    class ChangeDPI : public Action {
    public:
        static const char* interface_name;

        // Bind the action to the DPI feature and the mutable config entry.
        ChangeDPI(Device* device, config::ChangeDPI& setting,
                  const std::shared_ptr<ipcgull::node>& parent);

        // Press triggers the configured DPI change; release only clears pressed state.
        void press() final;

        void release() final;

        // Expose the current increment and sensor selection over IPC.
        [[nodiscard]] std::tuple<int16_t, uint16_t> getConfig() const;

        // Update the amount that will be added to the current DPI.
        void setChange(int16_t change);

        // Select the sensor to modify, or reset back to the default sensor.
        void setSensor(uint8_t sensor, bool reset);

        [[nodiscard]] uint8_t reprogFlags() const final;

    protected:
        config::ChangeDPI& _config;
        std::shared_ptr<features::DPI> _dpi;
    };
}

#endif //LOGID_ACTION_CHANGEDPI_H
