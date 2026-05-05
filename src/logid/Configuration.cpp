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
 * File: Configuration.cpp
 *
 * Runtime configuration loader and saver. This module reads the libconfig file
 * into the generated schema-backed runtime model, keeps defaults in place when
 * values are missing, and exposes the config save path over IPC.
 */

#include <Configuration.h>
#include <util/log.h>
#include <utility>
#include <filesystem>
#include <ipc_defs.h>

using namespace logid;
using namespace libconfig;
using namespace logid::config;

// Purpose: Load runtime configuration from disk.
// Inputs: Config file path.
// Outputs: Populated configuration state or parse/load exceptions.
// Used by: daemon startup.
Configuration::Configuration(std::string config_file) :
        _config_file(std::move(config_file)) {
    if (std::filesystem::exists(_config_file)) {
        try {
            _config.readFile(_config_file.c_str());
        } catch (const FileIOException& e) {
            logPrintf(ERROR, "I/O Error while reading %s: %s", _config_file.c_str(),
                  e.what());
            throw;
        } catch (const ParseException& e) {
            logPrintf(ERROR, "Parse error in %s, line %d: %s", e.getFile(),
                  e.getLine(), e.getError());
            throw;
        }

        // Copy the parsed libconfig tree into the generated runtime schema
        // object so runtime access works through the typed config model.
        Config::operator=(get<Config>(_config.getRoot()));
    } else {
        logPrintf(INFO, "Config file does not exist, using empty config.");
    }

    if (!devices.has_value())
        devices.emplace();
}

Configuration::Configuration() {
    devices.emplace();
}

// Purpose: Serialize the current runtime config back to disk.
// Inputs: None.
// Outputs: The config file is updated with runtime state.
// Used by: IPC `Save`.
void Configuration::save() {
    // libconfig writes from its own tree, so synchronize it from the schema object first.
    config::set(_config.getRoot(), *this);
    try {
        _config.writeFile(_config_file.c_str());
    } catch (const FileIOException& e) {
        logPrintf(ERROR, "I/O Error while writing %s: %s",
                  _config_file.c_str(), e.what());
        throw;
    } catch (const std::exception& e) {
        logPrintf(ERROR, "Error while writing %s: %s",
                  _config_file.c_str(), e.what());
        throw;
    }
}

// Purpose: Expose the config save action over IPC.
// Inputs: Configuration object.
// Outputs: `Save` method on the config interface.
// Used by: client persistence flows.
Configuration::IPC::IPC(Configuration* config) :
        ipcgull::interface(SERVICE_ROOT_NAME ".Config", {
                {"Save", {config, &Configuration::save}}
        }, {}, {}) {
}
