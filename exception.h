//
// Created by XiaoLi on 25-8-16.
//

#ifndef EXCEPTION_H
#define EXCEPTION_H
#include <exception>
#include <stdexcept>
#include <string>

namespace lvm
{
    class VMException final : std::runtime_error
    {
    public:
        explicit VMException(const std::string& message);
    };
}
#endif //EXCEPTION_H
