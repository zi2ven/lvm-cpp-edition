//
// Created by XiaoLi on 25-8-14.
//

#ifndef MODULE_H
#define MODULE_H
#include <cstdint>

namespace lvm
{
    class Module
    {
    public:
        const uint8_t* text;
        const uint8_t* rodata;
        const uint8_t* data;
        const uint64_t bssLength;
        const uint64_t entryPoint;

        Module(const uint8_t* text, const uint8_t* rodata, const uint8_t* data, uint64_t bssLength,uint64_t entryPoint);
    };
}
#endif //MODULE_H
