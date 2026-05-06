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
#ifndef LOGID_ACTION_CHANGEHOSTACTION_H
#define LOGID_ACTION_CHANGEHOSTACTION_H

#include <actions/Action.h>
#include <backend/hidpp20/features/ChangeHost.h>

namespace logid::actions {
    // Action that switches the receiver host slot when activated.
    class ChangeHostAction : public Action {
    public:
        static const char* interface_name;

        // Bind the action to the host-switch feature and config entry.
        ChangeHostAction(Device* device, config::ChangeHost& config,
                         const std::shared_ptr<ipcgull::node>& parent);

        // Press does nothing; the change is applied on release.
        void press() final;

        // Release selects the configured host slot.
        void release() final;

        // Read the configured host selector in human-readable form.
        [[nodiscard]] std::string getHost() const;

        // Accept a host selector such as next, prev, or a numeric index.
        void setHost(std::string host);

        [[nodiscard]] uint8_t reprogFlags() const final;

    protected:
        std::shared_ptr<backend::hidpp20::ChangeHost> _change_host;
        config::ChangeHost& _config;
    };
}

#endif //LOGID_ACTION_CHANGEHOSTACTION_H
