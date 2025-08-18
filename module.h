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
        const uint64_t textLength;
        const uint8_t* rodata;
        const uint64_t rodataLength;
        const uint8_t* data;
        const uint64_t dataLength;
        const uint64_t bssLength;
        const uint64_t entryPoint;

        Module(const uint8_t* text, uint64_t textLength, const uint8_t* rodata, uint64_t rodataLength,
               const uint8_t* data, uint64_t dataLength, uint64_t bssLength, uint64_t entryPoint);
        ~Module();
        [[nodiscard]] uint8_t* raw() const;
        static Module* fromRaw(const uint8_t* raw);
    };
}
#endif //MODULE_H
