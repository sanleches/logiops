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
#ifndef LOGID_ACTION_CYCLEDPI_H
#define LOGID_ACTION_CYCLEDPI_H

#include <actions/Action.h>
#include <features/DPI.h>

namespace logid::actions {
    // Action that steps through a list of DPI presets each time it is pressed.
    class CycleDPI : public Action {
    public:
        static const char* interface_name;

        // Bind the action to the DPI feature and the mutable cycle list.
        CycleDPI(Device* device, config::CycleDPI& setting,
                 const std::shared_ptr<ipcgull::node>& parent);

        // Press advances the active DPI to the next configured value.
        void press() final;

        void release() final;

        // Expose the configured DPI list to IPC clients.
        [[nodiscard]] std::vector<int> getDPIs() const;

        // Replace the configured DPI list and reset the current position.
        void setDPIs(const std::vector<int>& dpis);

        [[nodiscard]] uint8_t reprogFlags() const final;

    protected:
        std::mutex _dpi_mutex;
        config::CycleDPI& _config;
        std::shared_ptr<features::DPI> _dpi;
        std::list<int>::const_iterator _current_dpi;
    };
}

#endif //LOGID_ACTION_CYCLEDPI_H
