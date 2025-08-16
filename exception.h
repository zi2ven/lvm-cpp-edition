//
// Created by XiaoLi on 25-8-16.
//

#ifndef EXCEPTION_H
#define EXCEPTION_H
#include <exception>
#include <string>

namespace lvm
{
    class VMException : std::exception
    {
    public:
        std::string message;

        explicit VMException(std::string message);
    };
}
#endif //EXCEPTION_H
