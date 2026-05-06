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
 * File: RemapButton.cpp
 *
 * Remappable button feature wrapper. This module discovers the device's
 * programmable controls, maps each one to an action wrapper, listens for
 * diverted button reports, and exposes the mapping tree over IPC.
 */

#include <features/RemapButton.h>
#include <actions/GestureAction.h>
#include <Device.h>
#include <sstream>
#include <util/log.h>
#include <ipc_defs.h>

using namespace logid::features;
using namespace logid::backend;
using namespace logid::actions;

// Purpose: Format debug output for control flags.
// Inputs: Reprog control bits.
// Outputs: Human-readable YES/empty strings.
// Used by: debug logging.
#define REPROG_FLAG(x) (control.second.flags & hidpp20::ReprogControls::x ? "YES" : "")
#define REPROG_FLAG_ADDITIONAL(x) (control.second.additionalFlags & \
    hidpp20::ReprogControls::x ? "YES" : "")

static constexpr auto hidpp20_reprog_rebind =
        (hidpp20::ReprogControls::ChangeTemporaryDivert |
         hidpp20::ReprogControls::ChangeRawXYDivert);

// Purpose: Discover remappable controls and build button wrappers.
// Inputs: Device.
// Outputs: Button mappings plus IPC tree.
// Used by: device feature setup.
RemapButton::RemapButton(Device* dev) : DeviceFeature(dev),
                                        _config(dev->activeProfile().buttons),
                                        _ipc_node(dev->ipcNode()->make_child("buttons")) {
    try {
        _reprog_controls = hidpp20::ReprogControls::autoVersion(
                &dev->hidpp20());
    } catch (hidpp20::UnsupportedFeature& e) {
        throw UnsupportedFeature();
    }

    _reprog_controls->initCidMap();

    auto& config = _config.get();

    if (!config.has_value())
        config = config::RemapButton();

    for (const auto& control: _reprog_controls->getControls()) {
        const auto i = _buttons.size();
        // Translate the action's divert bits back into HID++ reporting flags.
        Button::ConfigFunction func = [this, info = control.second](
                const std::shared_ptr<actions::Action>& action) {
            hidpp20::ReprogControls::ControlInfo report{};
            report.controlID = info.controlID;
            report.flags = hidpp20_reprog_rebind;

            if (action) {
                if ((action->reprogFlags() & hidpp20::ReprogControls::RawXYDiverted) &&
                    (!_reprog_controls->supportsRawXY() ||
                     !(info.additionalFlags & hidpp20::ReprogControls::RawXY)))
                    logPrintf(WARN, "%s: 'Cannot divert raw XY movements for CID 0x%02x",
                              _device->name().c_str(), info.controlID);

                report.flags |= action->reprogFlags();
            }
            _reprog_controls->setControlReporting(info.controlID, report);
        };
        _buttons.emplace(control.second.controlID,
                         Button::make(control.second, (int) i,
                                      _device, func, _ipc_node,
                                      config.value()[control.first]));
    }

    _ipc_interface = _device->ipcNode()->make_interface<IPC>(this);

    if (global_loglevel <= DEBUG) {
        // Print CIDs, originally by zv0n
        logPrintf(DEBUG, "%s:%d remappable buttons:",
                  dev->hidpp20().devicePath().c_str(),
                  dev->hidpp20().deviceIndex());
        logPrintf(DEBUG, "CID  | reprog? | fn key? | mouse key? | "
                         "gesture support?");
        for (const auto& control: _reprog_controls->getControls())
            logPrintf(DEBUG, "0x%02x | %-7s | %-7s | %-10s | %s",
                      control.first, REPROG_FLAG(TemporaryDivertable), REPROG_FLAG(FKey),
                      REPROG_FLAG(MouseButton), REPROG_FLAG_ADDITIONAL(RawXY));
    }
}

// Purpose: Re-apply each button's current action mapping.
// Inputs: None.
// Outputs: Button actions refreshed from config.
// Used by: device reconfiguration.
void RemapButton::configure() {
    for (const auto& button: _buttons)
        button.second->configure();
}

// Purpose: Register the HID++ event listener for diverted buttons and raw XY.
// Inputs: None.
// Outputs: Event handler attached or reused.
// Used by: feature lifecycle.
void RemapButton::listen() {
    if (_ev_handler.empty()) {
        _ev_handler = _device->hidpp20().addEventHandler(
                {[index = _reprog_controls->featureIndex()](
                        const hidpp::Report& report) -> bool {
                    if (report.feature() == index) {
                        switch (report.function()) {
                            case hidpp20::ReprogControls::DivertedButtonEvent:
                            case hidpp20::ReprogControls::DivertedRawXYEvent:
                                return true;
                            default:
                                return false;
                        }
                    }
                    return false;
                },
                 [self_weak = self<RemapButton>()](const hidpp::Report& report) -> void {
                    auto self = self_weak.lock();
                    if (!self)
                        return;

                     switch (report.function()) {
                         case hidpp20::ReprogControls::DivertedButtonEvent:
                             self->_buttonEvent(
                                     self->_reprog_controls->divertedButtonEvent(report));
                             break;
                         case hidpp20::ReprogControls::DivertedRawXYEvent: {
                             auto divertedXY = self->_reprog_controls->divertedRawXYEvent(report);
                             for (const auto& button: self->_buttons)
                                 if (button.second->pressed())
                                     button.second->move(divertedXY.x, divertedXY.y);
                             break;
                         }
                         default:
                             break;
                     }
                 }
                });
    }
}

// Purpose: Switch to a different profile and rebind each button config.
// Inputs: Profile reference.
// Outputs: Button mappings updated.
// Used by: profile switching.
void RemapButton::setProfile(config::Profile& profile) {
    std::lock_guard<std::mutex> lock(_button_lock);

    _config = profile.buttons;
    if (!_config.get().has_value())
        _config.get().emplace();
    auto& config = _config.get().value();

    for(auto& button : _buttons)
        button.second->setProfile(config[button.first]);
}

// Purpose: Update pressed-button state and forward transitions to actions.
// Inputs: New pressed-button set.
// Outputs: Button press/release callbacks.
// Used by: diverted-button event handling.
void RemapButton::_buttonEvent(const std::set<uint16_t>& new_state) {
    // Ensure I/O doesn't occur while updating button state
    std::lock_guard<std::mutex> lock(_button_lock);

    // Press all added buttons
    for (const auto& i: new_state) {
        auto old_i = _pressed_buttons.find(i);
        if (old_i != _pressed_buttons.end()) {
            _pressed_buttons.erase(old_i);
        } else {
            auto action = _buttons.find(i);
            if (action != _buttons.end())
                action->second->press();
        }
    }

    // Release all removed buttons
    for (auto& i: _pressed_buttons) {
        auto action = _buttons.find(i);
        if (action != _buttons.end())
            action->second->release();
    }

    _pressed_buttons = new_state;
}

namespace logid::features {
    class ButtonWrapper : public Button {
    public:
        template<typename... Args>
        explicit ButtonWrapper(Args&& ... args) : Button(std::forward<Args&&>(args)...) {
        }
    };
}

std::shared_ptr<Button> Button::make(
        Info info, int index, Device* device, ConfigFunction conf_func,
        const std::shared_ptr<ipcgull::node>& root, config::Button& config) {
    auto ret = std::make_shared<ButtonWrapper>(info, index, device, std::move(conf_func),
                                               root, config);
    ret->_self = ret;
    ret->_node->manage(ret);

    return ret;
}

Button::Button(Info info, int index,
               Device* device, ConfigFunction conf_func,
               const std::shared_ptr<ipcgull::node>& root,
               config::Button& config) :
        _node(root->make_child(std::to_string(index))),
        _device(device), _conf_func(std::move(conf_func)),
        _config(config),
        _info(info) {
    _makeConfig();

    _ipc_interface = _node->make_interface<IPC>(this, _info);
}

// Purpose: Build the action object for one button.
// Inputs: Button config.
// Outputs: Action object or null.
// Used by: button initialization.
void Button::_makeConfig() {
    auto& config = _config.get();
    if (config.action.has_value()) {
        try {
            _action = Action::makeAction(_device, config.action.value(), _node);
        } catch (std::exception& e) {
            logPrintf(WARN, "Error creating button action: %s", e.what());
        }
    }
}

// Purpose: Mark the button pressed and forward to the action.
// Inputs: None.
// Outputs: Action press notification.
// Used by: HID++ button events.
void Button::press() {
    std::shared_lock lock(_action_lock);
    _first_move = true;
    if (_action)
        _action->press();
}

// Forward the release event to the mapped action.
void Button::release() const {
    std::shared_lock lock(_action_lock);
    if (_action)
        _action->release();
}

// Forward the first movement event after press only after the button is held.
// This avoids sending movement during the same packet that reported the press.
void Button::move(int16_t x, int16_t y) {
    std::shared_lock lock(_action_lock);
    if (_action && !_first_move)
        _action->move(x, y);
    else if (_first_move)
        _first_move = false;
}

// Ask the mapped action whether it is currently active.
// Drag-style remaps use this to decide whether raw XY motion should keep flowing.
bool Button::pressed() const {
    std::shared_lock lock(_action_lock);
    if (_action)
        return _action->pressed();
    return false;
}

// Re-run the current action's hardware configuration callback.
void Button::configure() const {
    std::shared_lock lock(_action_lock);
    _conf_func(_action);
}

// Replace the button mapping using a different profile entry.
// The old action is discarded so the new profile starts with a clean slate.
void Button::setProfile(config::Button& config) {
    std::unique_lock lock(_action_lock);
    _config = config;
    _action.reset();
    _makeConfig();
}

std::shared_ptr<ipcgull::node> Button::node() const {
    return _node;
}

Button::IPC::IPC(Button* parent, const Info& info) :
        ipcgull::interface(SERVICE_ROOT_NAME ".Button", {
                {"SetAction", {this, &IPC::setAction, {"type"}}}
        }, {
                                   {"ControlID",      ipcgull::property<uint16_t>(
                                           ipcgull::property_readable, info.controlID)},
                                   {"TaskID",         ipcgull::property<uint16_t>(
                                           ipcgull::property_readable, info.taskID)},
                                   {"Remappable",     ipcgull::property<const bool>(
                                           ipcgull::property_readable,
                                           info.flags &
                                           hidpp20::ReprogControls::TemporaryDivertable)},
                                   {"GestureSupport", ipcgull::property<const bool>(
                                           ipcgull::property_readable,
                                           (info.additionalFlags & hidpp20::ReprogControls::RawXY)
                                   )}
                           }, {}), _button(*parent) {
}

// Replace the action mapping using a new type name from IPC.
// This is the entry point used when a user edits one button assignment.
void Button::IPC::setAction(const std::string& type) {
    if (!(_button._info.flags & hidpp20::ReprogControls::TemporaryDivertable))
        throw std::invalid_argument("Non-remappable");

    if (type == GestureAction::interface_name &&
        !(_button._info.additionalFlags & hidpp20::ReprogControls::RawXY))
        throw std::invalid_argument("No gesture support");

    {
        std::unique_lock lock(_button._action_lock);
        _button._action.reset();
        _button._action = Action::makeAction(
                _button._device, type,
                _button._config.get().action, _button._node);
    }
    _button.configure();
}

RemapButton::IPC::IPC(RemapButton* parent) :
        ipcgull::interface(SERVICE_ROOT_NAME ".Buttons", {
                {"Enumerate", {this, &IPC::enumerate, {"buttons"}}}
        }, {}, {}),
        _parent(*parent) {
}

std::vector<std::shared_ptr<Button>> RemapButton::IPC::enumerate() const {
    std::vector<std::shared_ptr<Button>> ret;
    for (auto& x: _parent._buttons)
        ret.push_back(x.second);
    return ret;
}
