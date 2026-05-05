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
#ifndef LOGID_ACTION_KEYPRESS_H
#define LOGID_ACTION_KEYPRESS_H

#include <vector>
#include <actions/Action.h>

namespace logid::actions {
    // Action that emits one or more virtual key presses through the daemon's input device.
    class KeypressAction : public Action {
    public:
        static const char* interface_name;

        // Bind the action to the virtual input device and config entry.
        KeypressAction(Device* dev,
                       config::KeypressAction& config,
                       const std::shared_ptr<ipcgull::node>& parent);

        // Press sends the configured key sequence down.
        void press() final;

        // Release sends the matching key releases.
        void release() final;

        // Return the configured key list as readable names.
        [[nodiscard]] std::vector<std::string> getKeys() const;

        // Replace the configured key list from readable names.
        void setKeys(const std::vector<std::string>& keys);

        [[nodiscard]] uint8_t reprogFlags() const final;

    protected:
        config::KeypressAction& _config;
        std::list<uint> _keys;

        void _setConfig();
    };
}

#endif //LOGID_ACTION_KEYPRESS_H
