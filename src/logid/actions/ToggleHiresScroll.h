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
#ifndef LOGID_ACTION_TOGGLEHIRESSCROLL_H
#define LOGID_ACTION_TOGGLEHIRESSCROLL_H

#include <actions/Action.h>
#include <features/HiresScroll.h>

namespace logid::actions {
    // Action that toggles high-resolution scrolling on the device.
    class ToggleHiresScroll : public Action {
    public:
        static const char* interface_name;

        // Bind the action to the optional HiResScroll feature.
        ToggleHiresScroll(Device* dev, const std::shared_ptr<ipcgull::node>& parent);

        ToggleHiresScroll(Device* device,
                          [[maybe_unused]] config::ToggleHiresScroll& action,
                          const std::shared_ptr<ipcgull::node>& parent) :
                ToggleHiresScroll(device, parent) {}

        // Press flips the HiRes scroll mode asynchronously.
        void press() final;

        // Release only clears the pressed state marker.
        void release() final;

        [[nodiscard]] uint8_t reprogFlags() const final;

    protected:
        std::shared_ptr<features::HiresScroll> _hires_scroll;
    };
}

#endif //LOGID_ACTION_TOGGLEHIRESSCROLL_H
