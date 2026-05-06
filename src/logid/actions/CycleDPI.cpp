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
 * File: CycleDPI.cpp
 *
 * Action that cycles through a configured list of DPI values. It keeps the
 * current position in the list, applies the next value asynchronously, and
 * exposes the list over IPC so clients can edit it at runtime.
 */

#include <actions/CycleDPI.h>
#include <Device.h>
#include <backend/hidpp20/features/ReprogControls.h>
#include <util/task.h>
#include <util/log.h>

using namespace logid::actions;

// Purpose: Name the IPC interface for this action.
// Inputs: None.
// Outputs: Static interface name string.
// Used by: action construction.
const char* CycleDPI::interface_name = "CycleDPI";

// Purpose: Bind the action to the DPI feature and expose the cycle list.
// Inputs: Device, config, and parent IPC node.
// Outputs: Action object with IPC methods.
// Used by: DPI cycling buttons.
CycleDPI::CycleDPI(Device* device, config::CycleDPI& config,
                   [[maybe_unused]] const std::shared_ptr<ipcgull::node>& parent) :
        Action(device, interface_name, {
                {
                        {"GetDPIs", {this, &CycleDPI::getDPIs, {"dpis"}}},
                        {"SetDPIs", {this, &CycleDPI::setDPIs, {"dpis"}}}
                },
                {},
                {}
        }),
        _config(config) {
    _dpi = _device->getFeature<features::DPI>("dpi");
    if (!_dpi)
        logPrintf(WARN, "%s:%d: DPI feature not found, cannot use "
                        "CycleDPI action.",
                  _device->hidpp20().devicePath().c_str(),
                  _device->hidpp20().deviceIndex());

    if (_config.dpis.has_value()) {
        _current_dpi = _config.dpis.value().begin();
    }
}

// Purpose: Return the configured DPI list.
// Inputs: None.
// Outputs: Current DPI values.
// Used by: IPC `GetDPIs`.
std::vector<int> CycleDPI::getDPIs() const {
    std::shared_lock lock(_config_mutex);
    auto dpis = _config.dpis.value_or(std::list<int>());
    return {dpis.begin(), dpis.end()};
}

// Purpose: Replace the DPI list and restart from the first value.
// Inputs: New DPI list.
// Outputs: Config list updated and iterator reset.
// Used by: IPC `SetDPIs`.
void CycleDPI::setDPIs(const std::vector<int>& dpis) {
    std::unique_lock lock(_config_mutex);
    std::lock_guard dpi_lock(_dpi_mutex);
    _config.dpis.emplace(dpis.begin(), dpis.end());
    _current_dpi = _config.dpis->cbegin();
}

// Purpose: Advance to the next DPI and apply it asynchronously.
// Inputs: None.
// Outputs: Deferred DPI update on the worker queue.
// Used by: button press handling.
void CycleDPI::press() {
    _pressed = true;
    std::shared_lock lock(_config_mutex);
    std::lock_guard dpi_lock(_dpi_mutex);
    if (_dpi && _config.dpis.has_value() && _config.dpis.value().empty()) {
        ++_current_dpi;
        if (_current_dpi == _config.dpis.value().end())
            _current_dpi = _config.dpis.value().begin();

        run_task([self_weak = self<CycleDPI>(), dpi = *_current_dpi] {
            if (auto self = self_weak.lock()) {
                try {
                    self->_dpi->setDPI(dpi, self->_config.sensor.value_or(0));
                } catch (backend::hidpp20::Error& e) {
                    if (e.code() == backend::hidpp20::Error::InvalidArgument)
                        logPrintf(WARN, "%s:%d: Could not set DPI to %d for "
                                        "sensor %d",
                                        self->_device->hidpp20().devicePath().c_str(),
                                        self->_device->hidpp20().deviceIndex(), dpi,
                                        self->_config.sensor.value_or(0));
                    else
                        throw e;
                }
            }
        });
    }
}

// Purpose: Clear the pressed marker.
// Inputs: None.
// Outputs: Button state no longer active.
// Used by: button release handling.
void CycleDPI::release() {
    _pressed = false;
}

// Purpose: Mark the button as temporarily diverted.
// Inputs: None.
// Outputs: Reprog flag bits.
// Used by: hardware remapping.
uint8_t CycleDPI::reprogFlags() const {
    return backend::hidpp20::ReprogControls::TemporaryDiverted;
}
