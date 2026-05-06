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
/*
 * File: DPI.cpp
 *
 * DPI feature wrapper. This module binds the profile's DPI settings to the
 * hardware adjustable-DPI feature, keeps per-sensor DPI lists cached, and
 * exposes the live values over IPC.
 */

#include <features/DPI.h>
#include <Device.h>
#include <algorithm>
#include <cmath>
#include <ipc_defs.h>

using namespace logid::features;
using namespace logid::backend;

// Purpose: Snap a requested DPI to the closest supported value.
// Inputs: Supported DPI list and requested value.
// Outputs: Supported DPI value or fallback nearest value.
// Used by: DPI writes and profile configuration.
uint16_t getClosestDPI(const hidpp20::AdjustableDPI::SensorDPIList& dpi_list,
                       uint16_t dpi) {
    if (dpi_list.isRange) {
        const uint16_t min = *std::min_element(dpi_list.dpis.begin(), dpi_list.dpis.end());
        const uint16_t max = *std::max_element(dpi_list.dpis.begin(), dpi_list.dpis.end());
        if (!((dpi - min) % dpi_list.dpiStep) && dpi >= min && dpi <= max)
            return dpi;
        else if (dpi > max)
            return max;
        else if (dpi < min)
            return min;
        else
            return (uint16_t) (min + round((double) (dpi - min) / dpi_list.dpiStep) *
                                     dpi_list.dpiStep);
    } else {
        if (std::find(dpi_list.dpis.begin(), dpi_list.dpis.end(), dpi)
            != dpi_list.dpis.end())
            return dpi;
        else {
            auto it = std::min_element(dpi_list.dpis.begin(), dpi_list.dpis.end(),
                                       [dpi](uint16_t a, uint16_t b) {
                                           return (dpi - a) < (dpi - b);
                                       });
            if (it == dpi_list.dpis.end())
                return 0;
            else
                return *it;
        }
    }
}

// Purpose: Bind the profile DPI config to the adjustable-DPI feature.
// Inputs: Device.
// Outputs: Feature wrapper plus IPC interface.
// Used by: device feature setup.
DPI::DPI(Device* device) : DeviceFeature(device), _config(device->activeProfile().dpi) {
    try {
        _adjustable_dpi = std::make_shared<hidpp20::AdjustableDPI>
                (&device->hidpp20());
    } catch (hidpp20::UnsupportedFeature& e) {
        throw UnsupportedFeature();
    }

    _ipc_interface = _device->ipcNode()->make_interface<IPC>(this);
}

// Purpose: Apply the configured DPI values to each sensor.
// Inputs: None.
// Outputs: Hardware DPI state updated.
// Used by: device reconfiguration.
void DPI::configure() {
    std::shared_lock lock(_config_mutex);

    if (_config.get().has_value()) {
        const auto& config = _config.get().value();
        if (std::holds_alternative<int>(config)) {
            const auto& dpi = std::get<int>(config);
            _fillDPILists(0);
            std::shared_lock dpi_lock(_dpi_list_mutex);
            if (dpi != 0) {
                _adjustable_dpi->setSensorDPI(0, getClosestDPI(_dpi_lists.at(0), dpi));
            }
        } else {
            const auto& dpis = std::get<std::list<int>>(config);
            int i = 0;
            _fillDPILists(dpis.size() - 1);
            std::shared_lock dpi_lock(_dpi_list_mutex);
            for (const auto& dpi: dpis) {
                if (dpi != 0) {
                    _adjustable_dpi->setSensorDPI(i, getClosestDPI(_dpi_lists.at(i), dpi));
                    ++i;
                }
            }
        }
    }
}

// Purpose: No event subscription is required for DPI.
// Inputs: None.
// Outputs: No-op.
// Used by: feature lifecycle.
void DPI::listen() {
}

// Purpose: Rebind the feature to a new profile's DPI config.
// Inputs: Profile reference.
// Outputs: Config binding updated.
// Used by: profile switching.
void DPI::setProfile(config::Profile& profile) {
    std::unique_lock lock(_config_mutex);
    _config = profile.dpi;
}

// Purpose: Read the current live DPI.
// Inputs: Sensor index.
// Outputs: Live DPI value from hardware.
// Used by: IPC and fallback reads.
uint16_t DPI::getDPI(uint8_t sensor) {
    return _adjustable_dpi->getSensorDPI(sensor);
}

// Purpose: Clamp and write a requested DPI value.
// Inputs: DPI value and sensor index.
// Outputs: Hardware sensor DPI updated.
// Used by: IPC `SetDPI` and action helpers.
void DPI::setDPI(uint16_t dpi, uint8_t sensor) {
    if (dpi == 0)
        return;
    _fillDPILists(sensor);
    std::shared_lock lock(_dpi_list_mutex);
    auto dpi_list = _dpi_lists.at(sensor);
    _adjustable_dpi->setSensorDPI(sensor, getClosestDPI(dpi_list, dpi));
}

// Purpose: Ensure the cached DPI list exists up to one sensor.
// Inputs: Sensor index.
// Outputs: DPI list cache filled through that sensor.
// Used by: configure and IPC reads.
void DPI::_fillDPILists(uint8_t sensor) {
    bool needs_fill;
    {
        std::shared_lock lock(_dpi_list_mutex);
        needs_fill = _dpi_lists.size() <= sensor;
    }
    if (needs_fill) {
        std::unique_lock lock(_dpi_list_mutex);
        for (std::size_t i = _dpi_lists.size(); i <= sensor; i++) {
            _dpi_lists.push_back(_adjustable_dpi->getSensorDPIList(i));
        }
    }
}

DPI::IPC::IPC(DPI* parent) : ipcgull::interface(
        SERVICE_ROOT_NAME ".DPI", {
                {"GetSensors", {this, &IPC::getSensors, {"sensors"}}},
                {"GetDPIs", {this, &IPC::getDPIs, {"sensor"}, {"dpis", "dpiStep", "range"}}},
                {"GetDPI", {this, &IPC::getDPI, {"sensor"}, {"dpi"}}},
                {"SetDPI", {this, &IPC::setDPI, {"dpi", "sensor"}}}
        }, {}, {}), _parent(*parent) {
}

// Purpose: Return how many sensors are known.
// Inputs: None.
// Outputs: Sensor count.
// Used by: IPC `GetSensors`.
uint8_t DPI::IPC::getSensors() const {
    return _parent._dpi_lists.size();
}

// Purpose: Return the supported DPI list for one sensor.
// Inputs: Sensor index.
// Outputs: Supported values, step, and range flag.
// Used by: IPC `GetDPIs`.
std::tuple<std::vector<uint16_t>, uint16_t, bool> DPI::IPC::getDPIs(uint8_t sensor) const {
    _parent._fillDPILists(sensor);
    std::shared_lock lock(_parent._dpi_list_mutex);
    auto& dpi_list = _parent._dpi_lists.at(sensor);
    return {dpi_list.dpis, dpi_list.dpiStep, dpi_list.isRange};
}

// Purpose: Return the configured DPI for one sensor.
// Inputs: Sensor index.
// Outputs: DPI value from config or hardware.
// Used by: IPC `GetDPI`.
uint16_t DPI::IPC::getDPI(uint8_t sensor) const {
    std::shared_lock lock(_parent._config_mutex);
    auto& config = _parent._config.get();

    if (!config.has_value())
        return _parent.getDPI(sensor);

    if (std::holds_alternative<int>(config.value())) {
        if (sensor == 0)
            return std::get<int>(config.value());
        else
            return _parent.getDPI(sensor);
    }

    const auto& list = std::get<std::list<int>>(config.value());

    if (list.size() > sensor) {
        auto it = list.begin();
        std::advance(it, sensor);
        return *it;
    } else {
        return _parent.getDPI(sensor);
    }
}

// Purpose: Persist a new DPI value and apply it immediately.
// Inputs: DPI value and sensor index.
// Outputs: Config updated and hardware written.
// Used by: IPC `SetDPI`.
void DPI::IPC::setDPI(uint16_t dpi, uint8_t sensor) {
    std::unique_lock lock(_parent._config_mutex);
    auto& config = _parent._config.get();

    if (!config.has_value())
        config.emplace(std::list<int>());

    if (std::holds_alternative<int>(config.value())) {
        if (sensor == 0) {
            config.value() = dpi;
        } else {
            auto list = std::list<int>(sensor + 1, 0);
            *list.rbegin() = dpi;
            *list.begin() = dpi;
            config.value() = list;
        }
    } else {
        auto& list = std::get<std::list<int>>(config.value());

        while (list.size() <= sensor) {
            list.emplace_back(0);
        }

        if (list.size() == (size_t) (sensor + 1)) {
            *list.rbegin() = dpi;
        } else {
            auto it = list.begin();
            std::advance(it, sensor);
            *it = dpi;
        }
    }

    _parent.setDPI(dpi, sensor);
}
