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
 * File: FeatureSet.cpp
 *
 * HID++ 2.0 feature-table wrapper. This module reads the device's feature
 * inventory so other wrappers can discover which feature IDs are present on the
 * current hardware.
 */

#include <backend/hidpp20/features/FeatureSet.h>

using namespace logid::backend::hidpp20;

[[maybe_unused]]
// Purpose: Bind the feature-table reader to the device.
// Inputs: HID++ device.
// Outputs: Feature table wrapper.
// Used by: feature discovery.
FeatureSet::FeatureSet(Device* device) : Feature(device, ID) {
}

// Purpose: Ask the device how many features it exposes.
// Inputs: None.
// Outputs: Feature count byte.
// Used by: feature table iteration.
uint8_t FeatureSet::getFeatureCount() {
    std::vector<uint8_t> params(0);
    auto response = callFunction(GetFeatureCount, params);
    return response[0];
}

// Purpose: Read one feature ID from the device's feature table.
// Inputs: Feature index.
// Outputs: Feature ID.
// Used by: feature table iteration.
uint16_t FeatureSet::getFeature(uint8_t feature_index) {
    std::vector<uint8_t> params(1);
    params[0] = feature_index;
    auto response = callFunction(GetFeature, params);

    uint16_t feature_id = (response[0] << 8);
    feature_id |= response[1];
    return feature_id;
}

// Purpose: Walk the full feature table and return advertised IDs.
// Inputs: None.
// Outputs: Map of indices to feature IDs.
// Used by: debugging and discovery helpers.
[[maybe_unused]]
std::map<uint8_t, uint16_t> FeatureSet::getFeatures() {
    uint8_t feature_count = getFeatureCount();
    std::map<uint8_t, uint16_t> features;
    for (uint8_t i = 0; i < feature_count; i++)
        features[i] = getFeature(i);
    return features;
}
