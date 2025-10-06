//
// Created by XiaoLi on 25-8-16.
//

#include "exception.h"

#include <utility>

namespace lvm
{
    VMException::VMException(const std::string& message): std::runtime_error(message)
    {
    }
}
