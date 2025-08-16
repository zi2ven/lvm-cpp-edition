//
// Created by XiaoLi on 25-8-16.
//

#include "exception.h"

#include <utility>

namespace lvm
{
    VMException::VMException(std::string message): message(std::move(message))
    {
    }
}
