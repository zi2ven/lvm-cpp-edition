//
// Created by XiaoLi on 25-8-14.
//
#include "memory.h"

#include "exception.h"

namespace lvm
{
    Memory::Memory() = default;

    void Memory::init(const uint8_t* text, const uint8_t* rodata, const uint8_t* data, const uint64_t bssLength)
    {
    }

    uint64_t Memory::allocateMemory(const uint64_t size)
    {
        _mutex.lock();
        const uint64_t length = size + 8;
        FreeMemory* freeMemory = this->freeMemoryList;
        while (freeMemory != nullptr)
        {
            if (freeMemory->end - freeMemory->start >= length)
            {
                const uint64_t start = freeMemory->start;
                freeMemory->start += length;
                uint64_t address = start;
                uint64_t mapped = 0;
                while (mapped < length)
                {
                    setMemoryPageIfAbsent(address & ~PAGE_OFFSET_MASK, MemoryPage::MP_READ | MemoryPage::MP_WRITE);
                    const uint64_t tmp = PAGE_SIZE - (address & PAGE_OFFSET_MASK);
                    mapped += tmp;
                    address += tmp;
                }
                this->setLong(start, size);
                _mutex.unlock();
                return start + 8;
            }
        }
        _mutex.unlock();
        throw VMException("Out of memory");
    }

    uint64_t Memory::reallocateMemory(uint64_t address, uint64_t size)
    {
        const uint64_t oldSize = this->getLong(address - 8);
        auto* bytes = new uint8_t[size];
        for (uint64_t i = 0; i < oldSize; i++) bytes[i] = this->getByte(address + i);
        this->freeMemory(address);
        const uint64_t newAddress = this->allocateMemory(size);
        for (uint64_t i = 0; i < std::min(oldSize, size); i++) this->setByte(newAddress + i, bytes[i]);
        delete[] bytes;
        return newAddress;
    }

    void Memory::freeMemory(uint64_t address)
    {
        address -= 8;
        const uint64_t size = this->getLong(address) + 8;
        FreeMemory* freeMemory = this->freeMemoryList;
        while (freeMemory->next != nullptr)
        {
            if (freeMemory->end == address)
            {
                freeMemory->end += size;
                break;
            }
            if (freeMemory->start == address + size)
            {
                freeMemory->start -= size;
                break;
            }
            if (freeMemory->end < address && freeMemory->next->start > address + size)
            {
                FreeMemory* next = freeMemory->next;
                freeMemory->next = new FreeMemory(address, address + size);
                freeMemory->next->next = next;
                break;
            }
            freeMemory = freeMemory->next;
        }
        if (freeMemory->end < address && freeMemory->next == nullptr)
        {
            freeMemory->next = new FreeMemory(address, address + size);
        }
        uint64_t released = 0;
        while (released < size)
        {
            releaseMemoryPage(address & ~PAGE_OFFSET_MASK);
            uint64_t tmp = PAGE_SIZE - (address & PAGE_OFFSET_MASK);
            released += tmp;
            address += tmp;
        }
    }

    MemoryPage* Memory::getMemoryPageSafely(uint64_t address)
    {
        MemoryPage* memoryPage = this->getMemoryPage(address);
        if (memoryPage == nullptr) throw VMException("Illegal address");
        return memoryPage;
    }

    MemoryPage* Memory::getMemoryPage(uint64_t address)
    {
        uint64_t pgdOffset = (address >> 39) & 0x1ff;
        MemoryPage**** pud = this->memoryPageTable[pgdOffset];
        if (pud == nullptr) return nullptr;
        const uint64_t pudOffset = (address >> 30) & 0x1ff;
        MemoryPage*** pmd = pud[pudOffset];
        if (pmd == nullptr) return nullptr;
        const uint64_t pmdOffset = (address >> 21) & 0x1ff;
        MemoryPage** pte = pmd[pmdOffset];
        if (pte == nullptr) return nullptr;
        const uint64_t pteOffset = (address >> 12) & 0x1ff;
        return pte[pteOffset];
    }

    void Memory::releaseMemoryPage(uint64_t address)
    {
        if ((address & PAGE_OFFSET_MASK) != 0)
        {
            throw VMException("Invalid address");
        }
        MemoryPage* memoryPage = getMemoryPage(address);
        memoryPage->release();
        if (memoryPage->referenceCount == 0) resetMemoryPageIfExist(address);
    }

    bool Memory::setMemoryPageIfAbsent(uint64_t address, uint32_t flags)
    {
        if ((address & PAGE_OFFSET_MASK) != 0)
        {
            throw VMException("Invalid address");
        }
        uint64_t pgdOffset = (address >> 39) & 0x1ff;
        MemoryPage**** pud = this->memoryPageTable[pgdOffset];
        if (pud == nullptr)
        {
            pud = new MemoryPage***[PAGE_TABLE_SIZE];
            memoryPageTable[pgdOffset] = pud;
        }
        uint64_t pudOffset = (address >> 30) & 0x1ff;
        MemoryPage*** pmd = pud[pudOffset];
        if (pmd == nullptr)
        {
            pmd = new MemoryPage**[PAGE_TABLE_SIZE];
            pud[pudOffset] = pmd;
        }
        uint64_t pmdOffset = (address >> 21) & 0x1ff;
        MemoryPage** pte = pmd[pmdOffset];
        if (pte == nullptr)
        {
            pte = new MemoryPage*[PAGE_TABLE_SIZE];
            pmd[pmdOffset] = pte;
        }
        uint64_t pteOffset = (address >> 12) & 0x1ff;
        MemoryPage* page = pte[pteOffset];
        const bool ret = page != nullptr;
        if (ret)
        {
            page->flags |= flags;
        }
        else
        {
            page = new MemoryPage(address & ~PAGE_OFFSET_MASK, flags);
            pte[pteOffset] = page;
        }
        page->retain();
        return ret;
    }

    void Memory::resetMemoryPageIfExist(uint64_t address)
    {
        uint64_t pgdOffset = (address >> 39) & 0x1ff;
        MemoryPage**** pud = this->memoryPageTable[pgdOffset];
        if (pud == nullptr) return;
        const uint64_t pudOffset = (address >> 30) & 0x1ff;
        MemoryPage*** pmd = pud[pudOffset];
        if (pmd == nullptr) return;
        const uint64_t pmdOffset = (address >> 21) & 0x1ff;
        MemoryPage** pte = pmd[pmdOffset];
        if (pte == nullptr) return;
        const uint64_t pteOffset = (address >> 12) & 0x1ff;
        delete pte[pteOffset];
        pte[pteOffset] = nullptr;
    }


    uint8_t Memory::getByte(uint64_t address)
    {
        return getMemoryPageSafely(address)->getByte(address & PAGE_OFFSET_MASK);
    }

    uint16_t Memory::getShort(uint16_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 1) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getShort(address & PAGE_OFFSET_MASK);
        }
        return static_cast<uint16_t>(getMemoryPageSafely(address)->getByte(address & PAGE_OFFSET_MASK) | (
            getMemoryPageSafely(address + 1)->getByte(0) << 8));
    }

    uint32_t Memory::getInt(uint32_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getInt(address & PAGE_OFFSET_MASK);
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; i++)
        {
            value |= static_cast<uint32_t>(getMemoryPageSafely(address)->getByte(address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return value;
    }

    uint64_t Memory::getLong(uint64_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 7) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getLong(address & PAGE_OFFSET_MASK);
        }
        uint64_t value = 0;
        for (uint64_t i = 0; i < 8; i++)
        {
            value |= static_cast<uint64_t>(getMemoryPageSafely(address)->getByte(address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return value;
    }

    float Memory::getFloat(uint64_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getFloat(address & PAGE_OFFSET_MASK);
        }
        uint32_t value = 0;
        for (uint64_t i = 0; i < 4; i++)
        {
            value |= static_cast<uint32_t>(getMemoryPageSafely(address)->getByte(address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return std::bit_cast<float>(value);
    }

    double Memory::getDouble(uint64_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 7) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getDouble(address & PAGE_OFFSET_MASK);
        }
        uint64_t value = 0;
        for (uint64_t i = 0; i < 8; i++)
        {
            value |= static_cast<uint64_t>(getMemoryPageSafely(address)->getByte(address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return std::bit_cast<double>(value);
    }

    void Memory::setByte(uint64_t address, uint8_t value)
    {
        getMemoryPageSafely(address)->setByte(address & PAGE_OFFSET_MASK, value);
    }

    void Memory::setShort(uint64_t address, uint16_t value)
    {
        if (((address & PAGE_OFFSET_MASK) + 1) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setShort(address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            getMemoryPageSafely(address)->setByte(address & PAGE_OFFSET_MASK, static_cast<uint8_t>(value & 0xFF));
            getMemoryPageSafely(address + 1)->setByte(0, static_cast<uint8_t>(value >> 8));
        }
    }

    void Memory::setInt(uint64_t address, uint32_t value)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setInt(address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            for (uint64_t i = 0; i < 4; i++)
            {
                getMemoryPageSafely(address)->setByte((address & PAGE_OFFSET_MASK),
                                                      static_cast<uint8_t>(value >> (i * 8)));
                ++address;
            }
        }
    }

    void Memory::setLong(uint64_t address, uint64_t value)
    {
        if (((address & PAGE_OFFSET_MASK) + 7) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setLong(address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            for (uint64_t i = 0; i < 8; ++i)
            {
                getMemoryPageSafely(address)->setByte(address & PAGE_OFFSET_MASK,
                                                      static_cast<uint8_t>(value >> (i * 8)));
                ++address;
            }
        }
    }

    void Memory::setFloat(uint64_t address, float value)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setFloat(address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            auto bits = std::bit_cast<uint32_t>(value);
            for (uint64_t i = 0; i < 4; ++i)
            {
                getMemoryPageSafely(address)->setByte(address & PAGE_OFFSET_MASK, static_cast<uint8_t>(bits & 0xFF));
                bits >>= 8;
                ++address;
            }
        }
    }

    void Memory::setDouble(uint64_t address, const double value)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setDouble(address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            auto bits = std::bit_cast<uint64_t>(value);
            for (auto i = 0; i < 8; ++i)
            {
                getMemoryPageSafely(address)->setByte(address & PAGE_OFFSET_MASK, static_cast<uint8_t>(bits & 0xFF));
                bits >>= 8;
                ++address;
            }
        }
    }

    MemoryPage::MemoryPage(const uint64_t start, const uint32_t flags): flags(flags), start(start)
    {
    }

    void MemoryPage::initialize()
    {
        this->_mutex.lock();
        if ((this->flags & MP_PRESENT) != 0) return;
        this->data = new uint8_t[Memory::PAGE_SIZE];
        this->flags |= MP_PRESENT;
        this->_mutex.unlock();
    }

    void MemoryPage::retain()
    {
        this->_mutex.lock();
        ++this->referenceCount;
        this->_mutex.unlock();
    }

    void MemoryPage::release()
    {
        this->_mutex.lock();
        --this->referenceCount;
        if (this->referenceCount == 0) this->destroy();
        this->_mutex.unlock();
    }

    void MemoryPage::destroy()
    {
        delete[] this->data;
        this->flags &= ~MP_PRESENT;
    }

    uint8_t MemoryPage::getByte(uint8_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable())
            return this->data[offset];
        return 0;
    }

    uint16_t MemoryPage::getShort(uint16_t address)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable())
            return *reinterpret_cast<uint16_t*>(&this->data[address]);
        return 0;
    }

    uint32_t MemoryPage::getInt(uint32_t address)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable())
            return *reinterpret_cast<uint32_t*>(&this->data[address]);
        return 0;
    }

    uint64_t MemoryPage::getLong(uint64_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable())
            return *reinterpret_cast<uint64_t*>(&this->data[offset]);
        return 0;
    }

    float MemoryPage::getFloat(uint64_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable())
            return *reinterpret_cast<float*>(&this->data[offset]);
        return 0;
    }

    double MemoryPage::getDouble(uint64_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable())
            return *reinterpret_cast<double*>(&this->data[offset]);
        return 0;
    }

    void MemoryPage::setByte(uint8_t offset, uint8_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable())
            this->data[offset] = value;
    }

    void MemoryPage::setShort(uint16_t offset, uint16_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable())
            *reinterpret_cast<uint16_t*>(&this->data[offset]) = value;
    }

    void MemoryPage::setInt(uint32_t offset, uint32_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable())
            *reinterpret_cast<uint32_t*>(&this->data[offset]) = value;
    }

    void MemoryPage::setLong(uint64_t offset, uint64_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable())
            *reinterpret_cast<uint64_t*>(&this->data[offset]) = value;
    }

    void MemoryPage::setFloat(uint64_t offset, float value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable())
            *reinterpret_cast<float*>(&this->data[offset]) = value;
    }

    void MemoryPage::setDouble(uint64_t offset, double value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable())
            *reinterpret_cast<double*>(&this->data[offset]) = value;
    }

    bool MemoryPage::checkReadable() const
    {
        bool readable = (this->flags & MP_READ) != 0;
        if (!readable)
        {
            throw VMException("Attempt to read from a non-readable memory page");
        }
        return readable;
    }

    bool MemoryPage::checkWritable() const
    {
        bool writable = (this->flags & MP_WRITE) != 0;
        if (!writable)
        {
            throw VMException("Attempt to write to a non-writable memory page");
        }
        return writable;
    }

    bool MemoryPage::checkExecutable() const
    {
        bool executable = (this->flags & MP_EXEC) != 0;
        if (!executable)
        {
            throw VMException("Attempt to execute from a non-executable memory page");
        }
        return executable;
    }

    FreeMemory::FreeMemory(uint64_t start, uint64_t end): start(start), end(end)
    {
    }
}
