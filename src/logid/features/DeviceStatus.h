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
#ifndef LOGID_FEATURE_DEVICESTATUS_H
#define LOGID_FEATURE_DEVICESTATUS_H


#include <features/DeviceFeature.h>
#include <Device.h>
#include <backend/hidpp20/features/WirelessDeviceStatus.h>

namespace logid::features {
    // Feature wrapper that watches the device status broadcast stream.
    // It does not change settings directly; it only reacts when the firmware
    // says the device needs attention after sleep or reconnect.
    class DeviceStatus : public DeviceFeature {
    public:
        // No extra configuration is needed for this feature.
        void configure() final;

        // Install the wireless status event listener.
        void listen() final;

        // Device status does not track profile-specific settings.
        void setProfile(config::Profile& profile) final;

    protected:
        // Bind the status watcher to a device.
        explicit DeviceStatus(Device* dev);

    private:
        EventHandlerLock<backend::hidpp::Device> _ev_handler;
        std::shared_ptr<backend::hidpp20::WirelessDeviceStatus> _wireless_device_status;
    };
}

#endif //LOGID_FEATURE_DEVICESTATUS_H
