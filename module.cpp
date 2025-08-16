#include "module.h"
//
// Created by XiaoLi on 25-8-14.
//
namespace lvm
{
    Module::Module(const uint8_t* text, const uint8_t* rodata, const uint8_t* data, const uint64_t bssLength,
                   const uint64_t entryPoint): text(text), rodata(rodata), data(data), bssLength(bssLength),
                                               entryPoint(entryPoint)
    {
    }
}
