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
 * File: ReprogControls.cpp
 *
 * HID++ 2.0 reprogrammable-controls wrapper. This module discovers the control
 * table, caches control metadata, and translates HID++ button/raw-XY diversion
 * reports into the daemon's action mapping model.
 */

#include <backend/hidpp20/features/ReprogControls.h>
#include <backend/hidpp20/Error.h>
#include <backend/hidpp20/Device.h>
#include <cassert>

using namespace logid::backend::hidpp20;

// Purpose: Define the versioned ReprogControls constructors.
// Inputs: Device and optional feature ID.
// Outputs: Version-specific wrappers.
// Used by: `autoVersion()` selection.
#define DEFINE_REPROG(T, Base) \
    T::T(Device* dev, uint16_t _id) : Base(dev, _id) { } \
    T::T(Device* dev) : T(dev, ID) { }

DEFINE_REPROG(ReprogControls, Feature)

DEFINE_REPROG(ReprogControlsV2, ReprogControls)

DEFINE_REPROG(ReprogControlsV2_2, ReprogControlsV2)

DEFINE_REPROG(ReprogControlsV3, ReprogControlsV2_2)

DEFINE_REPROG(ReprogControlsV4, ReprogControlsV3)

template<typename T>
    // Purpose: Try to create a remapping backend version.
    // Inputs: Device and desired wrapper type.
    // Outputs: Wrapper instance or null.
    // Used by: version selection.
    std::shared_ptr<T> make_reprog(Device* dev) {
    try {
        return std::make_shared<T>(dev);
    } catch (UnsupportedFeature& e) {
        return {};
    }
}

// Purpose: Prefer the newest supported remapping implementation.
// Inputs: HID++ device.
// Outputs: The best supported remapping wrapper.
// Used by: remap button setup.
std::shared_ptr<ReprogControls> ReprogControls::autoVersion(Device* dev) {
    if (auto v4 = make_reprog<ReprogControlsV4>(dev)) {
        return v4;
    } else if (auto v3 = make_reprog<ReprogControlsV3>(dev)) {
        return v3;
    } else if (auto v2_2 = make_reprog<ReprogControlsV2_2>(dev)) {
        return v2_2;
    } else if (auto v2 = make_reprog<ReprogControlsV2>(dev)) {
        return v2;
    }

    return std::make_shared<ReprogControls>(dev);
}

// Purpose: Read how many remappable controls the device exposes.
// Inputs: None.
// Outputs: Control count.
// Used by: control table initialization.
uint8_t ReprogControls::getControlCount() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetControlCount, params);
    return response[0];
}

// Purpose: Read metadata for one control entry.
// Inputs: Control table index.
// Outputs: Parsed control info.
// Used by: control table initialization.
ReprogControls::ControlInfo ReprogControls::getControlInfo(uint8_t index) {
    std::vector<uint8_t> params(1);
    ControlInfo info{};
    params[0] = index;
    auto response = callFunction(GetControlInfo, params);

    info.controlID = response[1];
    info.controlID |= response[0] << 8;
    info.taskID = response[3];
    info.taskID |= response[2] << 8;
    info.flags = response[4];
    info.position = response[5];
    info.group = response[6];
    info.groupMask = response[7];
    info.additionalFlags = response[8];
    return info;
}

// Purpose: Populate the cached control-ID map on first use.
// Inputs: None.
// Outputs: Cached control metadata.
// Used by: lookup helpers.
void ReprogControls::initCidMap() {
    std::unique_lock<std::mutex> lock(_cids_populating);
    if (_cids_initialized)
        return;
    uint8_t controls = getControlCount();
    for (uint8_t i = 0; i < controls; i++) {
        auto info = getControlInfo(i);
        _cids.emplace(info.controlID, info);
    }
    _cids_initialized = true;
}

// Purpose: Return the cached control table.
// Inputs: None.
// Outputs: Control metadata map.
// Used by: button setup.
const std::map<uint16_t, ReprogControls::ControlInfo>&
ReprogControls::getControls() const {
    return _cids;
}

// Purpose: Look up control metadata by control ID.
// Inputs: Control ID.
// Outputs: Parsed control info or exception.
// Used by: remap button logic.
ReprogControls::ControlInfo ReprogControls::getControlIdInfo(uint16_t cid) {
    if (!_cids_initialized)
        initCidMap();

    auto it = _cids.find(cid);
    if (it == _cids.end())
        throw Error(Error::InvalidArgument, _device->deviceIndex());
    else
        return it->second;
}

// Purpose: Read reporting flags through the emulated pre-V4 path.
// Inputs: Control ID.
// Outputs: Compatibility reporting info.
// Used by: remap button setup.
[[maybe_unused]] ReprogControls::ControlInfo ReprogControls::getControlReporting(uint16_t cid) {
    // Emulate this function, only Reprog controls v4 supports this.
    auto info = getControlIdInfo(cid);

    ControlInfo report{};
    report.controlID = cid;
    report.flags = 0;
    if (info.flags & TemporaryDivertable)
        report.flags |= TemporaryDiverted;
    if (info.flags & PersistentlyDivertable)
        report.flags |= PersistentlyDiverted;
    if (info.additionalFlags & RawXY)
        report.flags |= RawXYDiverted;

    return report;
}

// Purpose: Ignore reporting writes on pre-V4 firmware.
// Inputs: Control ID and reporting info.
// Outputs: No-op.
// Used by: compatibility implementation.
void ReprogControls::setControlReporting(uint16_t cid, ControlInfo info) {
    // This function does not exist pre-v4 and cannot be emulated, ignore.
    (void) cid;
    (void) info; // Suppress unused warnings
}

// Purpose: Decode the list of diverted button control IDs.
// Inputs: One HID++ report.
// Outputs: Set of diverted control IDs.
// Used by: remap button event handling.
std::set<uint16_t> ReprogControls::divertedButtonEvent(
        const hidpp::Report& report) {
    assert(report.function() == DivertedButtonEvent);
    std::set<uint16_t> buttons;
    uint8_t cids = std::distance(report.paramBegin(), report.paramEnd()) / 2;
    for (uint8_t i = 0; i < cids; i++) {
        uint16_t cid = report.paramBegin()[2 * i + 1];
        cid |= report.paramBegin()[2 * i] << 8;
        if (cid)
            buttons.insert(cid);
        else
            break;
    }
    return buttons;
}

// Purpose: Decode the diverted raw-XY event payload.
// Inputs: One HID++ report.
// Outputs: Raw XY movement delta.
// Used by: remap button event handling.
ReprogControls::Move ReprogControls::divertedRawXYEvent(const hidpp::Report
                                                        & report) {
    assert(report.function() == DivertedRawXYEvent);
    Move move{};
    move.x = (int16_t) ((report.paramBegin()[0] << 8) | report.paramBegin()[1]);
    move.y = (int16_t) ((report.paramBegin()[2] << 8) | report.paramBegin()[3]);
    return move;
}

// Purpose: Read reporting flags on V4 firmware.
// Inputs: Control ID.
// Outputs: Live reporting info.
// Used by: remap button setup.
ReprogControls::ControlInfo ReprogControlsV4::getControlReporting(uint16_t cid) {
    std::vector<uint8_t> params(2);
    ControlInfo info{};
    params[0] = (cid >> 8) & 0xff;
    params[1] = cid & 0xff;
    auto response = callFunction(GetControlReporting, params);

    info.controlID = response[1];
    info.controlID |= response[0] << 8;
    info.flags = response[2];
    return info;
}

// Purpose: Write reporting flags on V4 firmware.
// Inputs: Control ID and reporting info.
// Outputs: Hardware reporting state updated.
// Used by: remap button setup.
void ReprogControlsV4::setControlReporting(uint16_t cid, ControlInfo info) {
    std::vector<uint8_t> params(5);
    params[0] = (cid >> 8) & 0xff;
    params[1] = cid & 0xff;
    params[2] = info.flags;
    params[3] = (info.controlID >> 8) & 0xff;
    params[4] = info.controlID & 0xff;
    callFunction(SetControlReporting, params);
}
