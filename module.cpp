#include "module.h"
//
// Created by XiaoLi on 25-8-14.
//
namespace lvm
{
    Module::Module(const uint8_t* text, const uint64_t textLength, const uint8_t* rodata, const uint64_t rodataLength,
                   const uint8_t* data, const uint64_t dataLength, const uint64_t bssLength,
                   const uint64_t entryPoint): text(text), textLength(textLength), rodata(rodata),
                                               rodataLength(rodataLength), data(data), dataLength(dataLength),
                                               bssLength(bssLength), entryPoint(0)
    {
    }
}
