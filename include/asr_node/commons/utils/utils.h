#ifndef _UTILS_H_
#define _UTILS_H_
#include <cstdint>
#include <filesystem>
namespace asr::utils
{
	enum class AudioType: std::uint8_t
	{
		kUnknown,
		kRawInputAudio,
		kSpeechStart,
		kSpeechStop,
	};
}
#endif
