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
#ifndef LOGID_ACTION_GESTUREACTION_H
#define LOGID_ACTION_GESTUREACTION_H

#include <map>
#include <actions/Action.h>
#include <actions/gesture/Gesture.h>

namespace logid::actions {
    // Action that routes one hardware input into multiple directional gestures.
    // It keeps separate gesture objects for up/down/left/right and chooses which
    // one to fire based on the motion that actually happened.
    class GestureAction : public Action {
    public:
        static const char* interface_name;

        enum Direction {
            None,
            Up,
            Down,
            Left,
            Right
        };

        // Convert a config string like "left" or "up" into an enum value.
        // IPC uses text because it is easier for humans to read and edit.
        static Direction toDirection(std::string direction);

        // Convert an enum value back into the config string used by IPC.
        static std::string fromDirection(Direction direction);

        // Pick a dominant direction from raw X/Y movement.
        // This is how the action decides whether a gesture was mostly horizontal
        // or mostly vertical.
        static Direction toDirection(int32_t x, int32_t y);

        // Bind the action to the gesture config and create the gesture sub-tree.
        // The IPC node tree mirrors the gesture directions so each one can be edited.
        GestureAction(Device* dev, config::GestureAction& config,
                      const std::shared_ptr<ipcgull::node>& parent);

        // Press resets movement state and primes all gestures.
        // A press starts a fresh gesture sequence.
        void press() final;

        // Release selects which gesture should actually fire.
        // The action waits until release so it can decide which direction won.
        void release() final;

        // Feed raw movement into the active direction detectors.
        // Each directional gesture accumulates the movement it cares about.
        void move(int16_t x, int16_t y) final;

        uint8_t reprogFlags() const final;

        void setGesture(const std::string& direction,
                        const std::string& type);

    protected:
        int32_t _x{}, _y{};
        std::shared_ptr<ipcgull::node> _node;
        std::map<Direction, std::shared_ptr<Gesture>> _gestures;
        config::GestureAction& _config;
    };
}

#endif //LOGID_ACTION_GESTUREACTION_H
