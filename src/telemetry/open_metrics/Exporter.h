#pragma once
#include <string_view>
#include <functional>
namespace telemetry {

namespace open_metrics {

///Exports open metrics format
/**
 * @param string_view metric file content (without trailing # END)
 */
using Exporter = std::function<void(std::string_view)>;

///Exports metrics to a file, similar as logfile.
/**
 * @param pathname filename
 * @return exporter
 * 
 * @exception system_error - when file cannot be opened
 */
Exporter createFileExporter(std::string pathname);

}

}
