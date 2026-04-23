#ifndef ASR_UTILS_HUMAN_READABLE_BYTES_H
#define ASR_UTILS_HUMAN_READABLE_BYTES_H

#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace asr::utils {

[[nodiscard]] inline std::string human_readable_bytes(std::size_t bytes) {
    static constexpr std::array units{ "B", "KiB", "MiB", "GiB" };

    auto value = static_cast<double>(bytes);
    std::size_t unit_index = 0;

    while (value >= 1024.0 && unit_index + 1 < units.size()) {
        value /= 1024.0;
        ++unit_index;
    }

    std::ostringstream output;
    if (unit_index == 0) {
        output << bytes << ' ' << units[unit_index];
    } else {
        output << std::fixed << std::setprecision(2)
               << value << ' ' << units[unit_index]
               << " (" << bytes << " bytes)";
    }

    return output.str();
}

} // namespace asr::utils

#endif // ASR_UTILS_HUMAN_READABLE_BYTES_H
