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
#ifndef LOGID_CHANGEPROFILE_H
#define LOGID_CHANGEPROFILE_H

#include <actions/Action.h>

namespace logid::actions {
    // Action that switches the active device profile when released.
    class ChangeProfile : public Action {
    public:
        static const char* interface_name;

        // Bind the action to the target device profile config.
        ChangeProfile(Device* device, config::ChangeProfile& setting,
                      const std::shared_ptr<ipcgull::node>& parent);

        // Press is intentionally a no-op.
        void press() final;

        // Release schedules the profile switch.
        void release() final;

        [[nodiscard]] uint8_t reprogFlags() const final;

        // Read the configured profile name.
        std::string getProfile();

        // Update the target profile name.
        void setProfile(std::string profile);

    private:
        config::ChangeProfile& _config;
    };
}


#endif //LOGID_CHANGEPROFILE_H
