#pragma once

#include <cstddef>
#include <string>

namespace robot_teacher {

/**
 * libcurl CURLOPT_WRITEFUNCTION callback that appends received response
 * bytes onto a std::string passed via CURLOPT_WRITEDATA.
 *
 * Shared by hearing_node (Whisper STT) and dialog_node (Claude/OpenAI
 * chat completions) — both previously carried an identical private copy.
 */
[[nodiscard]] std::size_t curlWriteCallback(
    char* ptr, std::size_t size, std::size_t nmemb, void* userdata);

}  // namespace robot_teacher
