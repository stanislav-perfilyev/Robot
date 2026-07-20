#include "robot_teacher/curl_utils.hpp"

namespace robot_teacher {

std::size_t curlWriteCallback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
{
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace robot_teacher
