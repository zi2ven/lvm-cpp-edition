#include "module.h"

#include <vector>

#include "vm.h"
//
// Created by XiaoLi on 25-8-14.
//
namespace lvm
{
    Module::Module(const uint8_t* text, const uint64_t textLength, const uint8_t* rodata, const uint64_t rodataLength,
                   const uint8_t* data, const uint64_t dataLength, const uint64_t bssLength,
                   const uint64_t entryPoint): text(text), textLength(textLength), rodata(rodata),
                                               rodataLength(rodataLength), data(data), dataLength(dataLength),
                                               bssLength(bssLength), entryPoint(entryPoint)
    {
    }

    Module::~Module()
    {
        delete[] text;
        delete[] rodata;
        delete[] data;
    }

    uint8_t* Module::raw() const
    {
        std::vector<uint8_t> v;
        v.push_back('l');
        v.push_back('v');
        v.push_back('m');
        v.push_back('e');
        for (int i = 0; i < sizeof(LVM_VERSION); i++)v.push_back(LVM_VERSION >> (i * 8));
        for (int i = 0; i < sizeof(textLength); i++)v.push_back(textLength >> (i * 8));
        for (int i = 0; i < textLength; i++)v.push_back(text[i]);
        for (int i = 0; i < sizeof(rodataLength); i++)v.push_back(rodataLength >> (i * 8));
        for (int i = 0; i < rodataLength; i++)v.push_back(rodata[i]);
        for (int i = 0; i < sizeof(dataLength); i++)v.push_back(dataLength >> (i * 8));
        for (int i = 0; i < dataLength; i++)v.push_back(data[i]);
        for (int i = 0; i < sizeof(bssLength); i++)v.push_back(bssLength >> (i * 8));
        for (int i = 0; i < sizeof(entryPoint); i++)v.push_back(entryPoint >> (i * 8));
        auto* raw = new uint8_t[v.size()];
        for (int i = 0; i < v.size(); i++)raw[i] = v[i];
        return raw;
    }

    Module* Module::fromRaw(const uint8_t* raw)
    {
        uint64_t index = 0;
        if (raw[index++] != 'l' || raw[index++] != 'v' || raw[index++] != 'm' || raw[index++] != 'e')
        {
            return nullptr;
        }
        for (int i = 0; i < sizeof(LVM_VERSION); i++)
        {
            if (raw[index++] != (LVM_VERSION >> (i * 8)))
            {
                return nullptr;
            }
        }
        uint64_t textLength = 0;
        for (int i = 0; i < sizeof(textLength); i++)textLength |= raw[index++] << (i * 8);
        auto* text = new uint8_t[textLength]{};
        for (int i = 0; i < textLength; i++)text[i] = raw[index++];
        uint64_t rodataLength = 0;
        for (int i = 0; i < sizeof(rodataLength); i++)rodataLength |= raw[index++] << (i * 8);
        auto* rodata = new uint8_t[rodataLength]{};
        for (int i = 0; i < rodataLength; i++)rodata[i] = raw[index++];
        uint64_t dataLength = 0;
        for (int i = 0; i < sizeof(dataLength); i++)dataLength |= raw[index++] << (i * 8);
        auto* data = new uint8_t[dataLength]{};
        for (int i = 0; i < dataLength; i++)data[i] = raw[index++];
        uint64_t bssLength = 0;
        for (int i = 0; i < sizeof(bssLength); i++)bssLength |= raw[index++] << (i * 8);
        uint64_t entryPoint = 0;
        for (int i = 0; i < sizeof(entryPoint); i++)entryPoint |= raw[index++] << (i * 8);
        return new Module(text, textLength, rodata, rodataLength, data, dataLength, bssLength, entryPoint);
    }
}
