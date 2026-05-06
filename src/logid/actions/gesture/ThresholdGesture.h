/*
 * Copyright 2019-2023 PixlOne, michtere
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
#ifndef LOGID_ACTION_THRESHOLDGESTURE_H
#define LOGID_ACTION_THRESHOLDGESTURE_H

#include <actions/gesture/Gesture.h>

namespace logid::actions {
    // Gesture that fires its action once when movement crosses a threshold.
    class ThresholdGesture : public Gesture {
    public:
        static const char* interface_name;

        // Bind the gesture to the threshold-action config.
        ThresholdGesture(Device* device, config::ThresholdGesture& config,
                         const std::shared_ptr<ipcgull::node>& parent);

        // Start tracking movement for a new gesture sequence.
        void press(bool init_threshold) final;

        // Reset the one-shot state after release.
        void release(bool primary) final;

        // Fire the action when the threshold is reached.
        void move(int16_t axis) final;

        [[nodiscard]] bool metThreshold() const final;

        [[nodiscard]] bool wheelCompatibility() const final;

        [[nodiscard]] int getThreshold() const;

        void setThreshold(int threshold);

        void setAction(const std::string& type);

    protected:
        int32_t _axis{};
        std::shared_ptr<actions::Action> _action;
        config::ThresholdGesture& _config;

    private:
        bool _executed = false;
    };
}

#endif //LOGID_ACTION_THRESHOLDGESTURE_H
