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
 * File: backend/hidpp10/Device.cpp
 *
 * HID++ 1.0 register-access wrapper. This module constructs HID++ reports,
 * matches responses, and translates register errors into exceptions.
 */

#include <backend/hidpp10/Device.h>
#include <backend/Error.h>
#include <cassert>
#include <utility>

using namespace logid::backend;
using namespace logid::backend::hidpp10;

// Purpose: Build a register-access report with the correct short/long layout.
// Inputs: Device index, register identifiers, and parameter payload.
// Outputs: HID++ report ready for transport.
// Used by: register access helpers.
hidpp::Report setupRegReport(hidpp::DeviceIndex index,
                              uint8_t sub_id, uint8_t address,
                              const std::vector<uint8_t>& params) {
    hidpp::Report::Type type = params.size() <= hidpp::ShortParamLength ?
                               hidpp::Report::Type::Short : hidpp::Report::Type::Long;

    if (sub_id == SetRegisterLong) {
        // When setting a long register, the report must be long.
        type = hidpp::Report::Type::Long;
    }

    hidpp::Report request(type, index, sub_id, address);
    std::copy(params.begin(), params.end(), request.paramBegin());

    return request;
}

Device::Device(const std::string& path, hidpp::DeviceIndex index,
               const std::shared_ptr<raw::DeviceMonitor>& monitor, double timeout) :
        hidpp::Device(path, index, monitor, timeout) {
}

Device::Device(std::shared_ptr<raw::RawDevice> raw_dev, hidpp::DeviceIndex index,
               double timeout) : hidpp::Device(std::move(raw_dev), index, timeout) {
}

Device::Device(const std::shared_ptr<hidpp10::Receiver>& receiver,
               hidpp::DeviceIndex index,
               double timeout)
        : hidpp::Device(receiver, index, timeout) {
}

// Purpose: Clear one pending response slot.
// Inputs: None.
// Outputs: Slot ready for reuse.
// Used by: response matching.
void Device::ResponseSlot::reset() {
    response.reset();
    sub_id.reset();
}

// Purpose: Send a report and wait for a matching response.
// Inputs: Outgoing HID++ report.
// Outputs: Matched HID++ response or translated error.
// Used by: register access helpers.
hidpp::Report Device::sendReport(const hidpp::Report& report) {
    auto& response_slot = _responses[report.subId() % SubIDCount];

    std::unique_lock<std::mutex> lock(_response_mutex);
    _response_cv.wait(lock, [&response_slot]() {
        return !response_slot.sub_id.has_value();
    });
    response_slot.sub_id = report.subId();

    _sendReport(report);
    bool valid = _response_cv.wait_for(lock, io_timeout, [&response_slot]() {
        return response_slot.response.has_value();
    });

    if (!valid) {
        response_slot.reset();
        throw TimeoutError();
    }

    auto response = response_slot.response.value();
    response_slot.reset();

    if (std::holds_alternative<hidpp::Report>(response)) {
        return std::get<hidpp::Report>(response);
    } else { // if(std::holds_alternative<hidpp::Report::Hidpp10Error>(response))
        auto error = std::get<hidpp::Report::Hidpp10Error>(response);
        throw Error(error.error_code, error.device_index);
    }
}

// Purpose: Match an incoming report against a pending register response.
// Inputs: Incoming HID++ report.
// Outputs: True if the report completed a pending request.
// Used by: raw event handling.
bool Device::responseReport(const hidpp::Report& report) {
    std::lock_guard<std::mutex> lock(_response_mutex);
    uint8_t sub_id;

    bool is_error = false;
    hidpp::Report::Hidpp10Error hidpp10_error{};
    if (report.isError10(hidpp10_error)) {
        sub_id = hidpp10_error.sub_id;
        is_error = true;
    } else {
        sub_id = report.subId();
    }

    auto& response_slot = _responses[sub_id % SubIDCount];

    if (!response_slot.sub_id.has_value() || response_slot.sub_id.value() != sub_id)
        return false;

    if (is_error) {
        response_slot.response = hidpp10_error;
    } else {
        response_slot.response = report;
    }

    _response_cv.notify_all();
    return true;
}

// Purpose: Read a register through the HID++ 1.0 transport.
// Inputs: Register address, parameters, and desired report type.
// Outputs: Register payload bytes.
// Used by: feature wrappers.
std::vector<uint8_t> Device::getRegister(uint8_t address,
                                          const std::vector<uint8_t>& params,
                                          hidpp::Report::Type type) {
    assert(params.size() <= hidpp::LongParamLength);

    uint8_t sub_id = type == hidpp::Report::Type::Short ?
                     GetRegisterShort : GetRegisterLong;

    return accessRegister(sub_id, address, params);
}

// Purpose: Write a register through the HID++ 1.0 transport.
// Inputs: Register address, parameters, and desired report type.
// Outputs: Register payload bytes.
// Used by: feature wrappers.
std::vector<uint8_t> Device::setRegister(uint8_t address,
                                          const std::vector<uint8_t>& params,
                                          hidpp::Report::Type type) {
    assert(params.size() <= hidpp::LongParamLength);

    uint8_t sub_id = type == hidpp::Report::Type::Short ?
                     SetRegisterShort : SetRegisterLong;

    return accessRegister(sub_id, address, params);
}

// Purpose: Write a register without waiting for a response.
// Inputs: Register address, parameters, and desired report type.
// Outputs: No response expected.
// Used by: no-ACK feature calls.
void Device::setRegisterNoResponse(uint8_t address,
                                   const std::vector<uint8_t>& params,
                                   hidpp::Report::Type type) {
    assert(params.size() <= hidpp::LongParamLength);

    uint8_t sub_id = type == hidpp::Report::Type::Short ?
                     SetRegisterShort : SetRegisterLong;

    return accessRegisterNoResponse(sub_id, address, params);
}

// Purpose: Shared helper for synchronous register access.
// Inputs: Sub-ID, address, and parameters.
// Outputs: Response bytes.
// Used by: get/set register wrappers.
std::vector<uint8_t> Device::accessRegister(uint8_t sub_id, uint8_t address,
                                             const std::vector<uint8_t>& params) {
    auto response = sendReport(setupRegReport(deviceIndex(), sub_id, address, params));
    return {response.paramBegin(), response.paramEnd()};
}

// Purpose: Shared helper for asynchronous register access.
// Inputs: Sub-ID, address, and parameters.
// Outputs: Fire-and-forget report.
// Used by: set-register-no-response wrapper.
void Device::accessRegisterNoResponse(uint8_t sub_id, uint8_t address,
                                      const std::vector<uint8_t>& params) {
    sendReportNoACK(setupRegReport(deviceIndex(), sub_id, address, params));
}
