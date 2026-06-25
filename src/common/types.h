#pragma once
#include <chrono>
#include <string>
#include "seqsvr.pb.h"

namespace seqsvr {

template<typename T>
class StatusOr {
public:
    StatusOr(T value) : value_(std::move(value)), ok_(true) {}
    StatusOr(ErrorCode err, std::string msg = "")
        : error_code_(err), error_msg_(std::move(msg)), ok_(false) {}

    bool ok() const { return ok_; }
    const T& value() const { return value_; }
    ErrorCode error_code() const { return error_code_; }
    const std::string& error_msg() const { return error_msg_; }

private:
    T         value_{};
    ErrorCode error_code_{ErrorCode::OK};
    std::string error_msg_;
    bool      ok_{false};
};

inline int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace seqsvr
