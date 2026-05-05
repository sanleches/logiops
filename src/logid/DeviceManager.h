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

#ifndef LOGID_DEVICEMANAGER_H
#define LOGID_DEVICEMANAGER_H

#include <backend/raw/DeviceMonitor.h>
#include <Device.h>
#include <Receiver.h>
#include <ipcgull/node.h>
#include <ipcgull/interface.h>

namespace logid {
    class InputDevice;

    // Owns discovery and lifetime management for all devices and receivers.
    // It also owns the IPC roots that let clients inspect and control them.
    class DeviceManager : public backend::raw::DeviceMonitor {
    public:

        [[nodiscard]] std::shared_ptr<Configuration> config() const;

        [[nodiscard]] std::shared_ptr<InputDevice> virtualInput() const;

        [[nodiscard]] std::shared_ptr<const ipcgull::node> devicesNode() const;

        [[nodiscard]] std::shared_ptr<const ipcgull::node>
        receiversNode() const;

        // Publish a device created from a receiver into the IPC tree so clients
        // can see it exactly like a directly attached device.
        void addExternalDevice(const std::shared_ptr<Device>& d);

        // Remove a receiver-backed device from IPC when the receiver disconnects.
        void removeExternalDevice(const std::shared_ptr<Device>& d);

        std::mutex& mutex() const;

    protected:
        DeviceManager(std::shared_ptr<Configuration> config,
                      std::shared_ptr<InputDevice> virtual_input,
                      std::shared_ptr<ipcgull::server> server);

        void addDevice(std::string path) final;

        void removeDevice(std::string path) final;

    private:
        class DevicesIPC : public ipcgull::interface {
        public:
            // IPC surface for listing devices and broadcasting add/remove events.
            explicit DevicesIPC(DeviceManager* manager);

            void deviceAdded(const std::shared_ptr<Device>& d);

            void deviceRemoved(const std::shared_ptr<Device>& d);
        };

        [[nodiscard]]
        std::vector<std::shared_ptr<Device>> listDevices() const;

        class ReceiversIPC : public ipcgull::interface {
        public:
            // IPC surface for listing receivers and broadcasting add/remove events.
            explicit ReceiversIPC(DeviceManager* manager);

            void receiverAdded(const std::shared_ptr<Receiver>& r);

            void receiverRemoved(const std::shared_ptr<Receiver>& r);
        };

        [[nodiscard]]
        std::vector<std::shared_ptr<Receiver>> listReceivers() const;

        std::shared_ptr<ipcgull::server> _server;
        std::shared_ptr<Configuration> _config;
        std::shared_ptr<InputDevice> _virtual_input;

        std::shared_ptr<ipcgull::node> _root_node;

        std::shared_ptr<ipcgull::node> _device_node;
        std::shared_ptr<ipcgull::node> _receiver_node;

        std::shared_ptr<Configuration::IPC> _ipc_config;
        std::shared_ptr<DevicesIPC> _ipc_devices;
        std::shared_ptr<ReceiversIPC> _ipc_receivers;

        std::map<std::string, std::shared_ptr<Device>> _devices;
        std::map<std::string, std::shared_ptr<Receiver>> _receivers;

        mutable std::mutex _map_lock;

        friend class DeviceNickname;

        friend class ReceiverNickname;

        [[nodiscard]] int newDeviceNickname();

        [[nodiscard]] int newReceiverNickname();

        std::mutex _nick_lock;
        std::set<int> _device_nicknames;
        std::set<int> _receiver_nicknames;
    };

}

#endif //LOGID_DEVICEMANAGER_H
