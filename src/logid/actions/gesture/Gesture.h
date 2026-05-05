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
#ifndef LOGID_ACTION_GESTURE_H
#define LOGID_ACTION_GESTURE_H

#include <utility>
#include <actions/Action.h>

namespace logid::actions {
    // Thrown when the requested gesture type is unknown or invalid.
    class InvalidGesture : public std::exception {
    public:
        explicit InvalidGesture(std::string what = "") : _what(std::move(what)) {
        }

        [[nodiscard]] const char* what() const noexcept override {
            return _what.c_str();
        }

    private:
        std::string _what;
    };

    // Base class for directional gestures used by GestureAction and wheel features.
    // Each concrete gesture decides when movement has crossed a threshold and
    // whether it can be used by wheel-style inputs.
    class Gesture : public ipcgull::interface {
    public:
        // Press begins a gesture sequence.
        // Some gestures use this to initialize thresholds or reset counters.
        virtual void press(bool init_threshold) = 0;

        // Release finalizes a gesture sequence.
        // The `primary` flag tells the gesture whether it won the direction race.
        virtual void release(bool primary) = 0;

        // Feed movement into the gesture.
        // Gestures use this to decide when enough movement has been collected.
        virtual void move(int16_t axis) = 0;

        [[nodiscard]] virtual bool wheelCompatibility() const = 0;

        [[nodiscard]] virtual bool metThreshold() const = 0;

        virtual ~Gesture() = default;

        // Build the correct concrete gesture from a config variant.
        static std::shared_ptr<Gesture> makeGesture(Device* device,
                                                    config::Gesture& gesture,
                                                    const std::shared_ptr<ipcgull::node>& parent);

        // Build a gesture when only the type name is known.
        static std::shared_ptr<Gesture> makeGesture(
                Device* device, const std::string& type,
                config::Gesture& gesture,
                const std::shared_ptr<ipcgull::node>& parent);

    protected:
        Gesture(Device* device,
                std::shared_ptr<ipcgull::node> parent,
                const std::string& name, tables t = {});

        mutable std::shared_mutex _config_mutex;

        const std::shared_ptr<ipcgull::node> _node;
        Device* _device;
    };
}

#endif //LOGID_ACTION_GESTURE_H
