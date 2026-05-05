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
#include <backend/hidpp20/features/ReprogControls.h>
#include <backend/hidpp20/Error.h>
#include <backend/hidpp20/Device.h>
#include <cassert>

using namespace logid::backend::hidpp20;

// Define all the ReprogControls versions
#define DEFINE_REPROG(T, Base) \
    T::T(Device* dev, uint16_t _id) : Base(dev, _id) { } \
    T::T(Device* dev) : T(dev, ID) { }

DEFINE_REPROG(ReprogControls, Feature)

DEFINE_REPROG(ReprogControlsV2, ReprogControls)

DEFINE_REPROG(ReprogControlsV2_2, ReprogControlsV2)

DEFINE_REPROG(ReprogControlsV3, ReprogControlsV2_2)

DEFINE_REPROG(ReprogControlsV4, ReprogControlsV3)

template<typename T>
// Try to create a remapping backend version if the device supports it.
std::shared_ptr<T> make_reprog(Device* dev) {
    try {
        return std::make_shared<T>(dev);
    } catch (UnsupportedFeature& e) {
        return {};
    }
}

// Prefer the newest supported remapping implementation.
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

// Read how many remappable controls the device exposes.
uint8_t ReprogControls::getControlCount() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetControlCount, params);
    return response[0];
}

// Read metadata for one control entry.
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

// Populate the cached control-ID map on first use.
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

// Return the cached control table.
const std::map<uint16_t, ReprogControls::ControlInfo>&
ReprogControls::getControls() const {
    return _cids;
}

// Look up control metadata by control ID.
ReprogControls::ControlInfo ReprogControls::getControlIdInfo(uint16_t cid) {
    if (!_cids_initialized)
        initCidMap();

    auto it = _cids.find(cid);
    if (it == _cids.end())
        throw Error(Error::InvalidArgument, _device->deviceIndex());
    else
        return it->second;
}

// Read reporting flags through the emulated pre-V4 path.
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

// Pre-V4 firmware cannot write reporting flags, so this is a no-op.
void ReprogControls::setControlReporting(uint16_t cid, ControlInfo info) {
    // This function does not exist pre-v4 and cannot be emulated, ignore.
    (void) cid;
    (void) info; // Suppress unused warnings
}

// Decode the list of diverted button control IDs.
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

// Decode the diverted raw-XY event payload.
ReprogControls::Move ReprogControls::divertedRawXYEvent(const hidpp::Report
                                                        & report) {
    assert(report.function() == DivertedRawXYEvent);
    Move move{};
    move.x = (int16_t) ((report.paramBegin()[0] << 8) | report.paramBegin()[1]);
    move.y = (int16_t) ((report.paramBegin()[2] << 8) | report.paramBegin()[3]);
    return move;
}

// Read reporting flags on V4 firmware.
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

// Write reporting flags on V4 firmware.
void ReprogControlsV4::setControlReporting(uint16_t cid, ControlInfo info) {
    std::vector<uint8_t> params(5);
    params[0] = (cid >> 8) & 0xff;
    params[1] = cid & 0xff;
    params[2] = info.flags;
    params[3] = (info.controlID >> 8) & 0xff;
    params[4] = info.controlID & 0xff;
    callFunction(SetControlReporting, params);
}
