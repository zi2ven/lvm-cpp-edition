//
// Created by XiaoLi on 25-8-14.
//

#ifndef BYTECODE_H
#define BYTECODE_H
#include <cstdint>

namespace lvm
{
    namespace bytecode
    {
        constexpr uint64_t REGISTER_COUNT = 42;
        constexpr uint8_t RETURN_VALUE_REGISTER = 36;
        constexpr uint8_t BP_REGISTER= 37;
        constexpr uint8_t SP_REGISTER = 38;
        constexpr uint8_t PC_REGISTER= 39;
        constexpr uint8_t FLAGS_REGISTER = 40;
        constexpr uint8_t IDTR_REGISTER = 41;

        constexpr uint64_t NOP = 0;
    }
}
#endif //BYTECODE_H
