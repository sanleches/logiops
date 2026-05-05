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

#ifndef LOGID_RECEIVER_H
#define LOGID_RECEIVER_H

#include <string>
#include <Device.h>
#include <backend/hidpp10/ReceiverMonitor.h>

namespace logid {
    class ReceiverNickname {
    public:
    // Reserve a short numeric identity for the lifetime of a receiver node so
    // its IPC path stays compact and stable while the receiver is alive.
    explicit ReceiverNickname(const std::shared_ptr<DeviceManager>& manager);

        ReceiverNickname() = delete;

        ReceiverNickname(const ReceiverNickname&) = delete;

        ~ReceiverNickname();

        operator std::string() const;

    private:
        const int _nickname;
        const std::weak_ptr<DeviceManager> _manager;
    };

    // Wraps a Logitech receiver and manages the wireless devices discovered
    // through that receiver. It acts as the bridge between pair slots and
    // logical `Device` objects.
    class Receiver : public backend::hidpp10::ReceiverMonitor,
                     public ipcgull::object {
    public:
        typedef std::map<backend::hidpp::DeviceIndex, std::shared_ptr<Device>>
                DeviceList;

        ~Receiver() noexcept override;

        // Construct a receiver and attach it to the monitor and IPC tree.
        static std::shared_ptr<Receiver> make(
                const std::string& path,
                const std::shared_ptr<DeviceManager>& manager);

        [[nodiscard]] const std::string& path() const;

        std::shared_ptr<backend::hidpp10::Receiver> rawReceiver();

        [[nodiscard]] const DeviceList& devices() const;

        [[nodiscard]] std::vector<std::tuple<int, uint16_t, std::string, uint32_t>>
        pairedDevices() const;

        // Start or stop the receiver pairing flow.
        void startPair(uint8_t timeout);

        void stopPair();

        void unpair(int device);

    protected:
        Receiver(const std::string& path,
                 const std::shared_ptr<DeviceManager>& manager);

        void addDevice(backend::hidpp::DeviceConnectionEvent event) override;

        void removeDevice(backend::hidpp::DeviceIndex index) override;

        void pairReady(const backend::hidpp10::DeviceDiscoveryEvent& event,
                       const std::string& passkey) override;

    private:
        std::mutex _devices_change;
        DeviceList _devices;
        std::string _path;
        std::weak_ptr<DeviceManager> _manager;

        const ReceiverNickname _nickname;
        std::shared_ptr<ipcgull::node> _ipc_node;

        class IPC : public ipcgull::interface {
        public:
            // IPC facade for pairing, unpairing, and receiver status.
            explicit IPC(Receiver* receiver);
        };

        std::shared_ptr<ipcgull::interface> _ipc_interface;
    };
}

#endif //LOGID_RECEIVER_H
