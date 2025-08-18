//
// Created by XiaoLi on 25-8-14.
//
#include "memory.h"

#include <iostream>

#include "bytecode.h"
#include "exception.h"
#include "vm.h"

namespace lvm
{
    Memory::Memory() = default;

    void Memory::init(const uint8_t* text, const uint64_t textLength, const uint8_t* rodata,
                      const uint64_t rodataLength, const uint8_t* data, const uint64_t dataLength,
                      const uint64_t bssLength)
    {
        this->memoryPageTable = new MemoryPage****[PAGE_TABLE_SIZE]{};
        uint64_t address = 0;
        setMemoryPageIfAbsent(address, MemoryPage::MP_READ | MemoryPage::MP_WRITE | MemoryPage::MP_EXEC);
        MemoryPage* currentPage = getMemoryPageSafely(address);
        address += PAGE_SIZE;

        uint64_t offset = 0;
        for (uint64_t i = 0; i < textLength; ++i)
        {
            currentPage->setByte(nullptr, offset, text[i]);
            ++offset;
            if (offset == PAGE_SIZE)
            {
                currentPage->flags &= ~MemoryPage::MP_EXEC;
                setMemoryPageIfAbsent(address, MemoryPage::MP_READ | MemoryPage::MP_WRITE | MemoryPage::MP_EXEC);
                currentPage = getMemoryPageSafely(address);
                address += PAGE_SIZE;
                offset = 0;
            }
        }
        if (offset == 0)
        {
            currentPage->flags &= ~MemoryPage::MP_EXEC;
        }
        for (uint64_t i = 0; i < rodataLength; ++i)
        {
            currentPage->setByte(nullptr, offset, rodata[i]);
            ++offset;
            if (offset == PAGE_SIZE)
            {
                currentPage->flags &= ~MemoryPage::MP_WRITE;
                setMemoryPageIfAbsent(address, MemoryPage::MP_READ | MemoryPage::MP_WRITE);
                currentPage = getMemoryPageSafely(address);
                address += PAGE_SIZE;
                offset = 0;
            }
        }
        currentPage->flags |= MemoryPage::MP_WRITE;
        for (uint64_t i = 0; i < dataLength; ++i)
        {
            currentPage->setByte(nullptr, offset, data[i]);
            ++offset;
            if (offset == PAGE_SIZE)
            {
                setMemoryPageIfAbsent(address, MemoryPage::MP_READ | MemoryPage::MP_WRITE);
                currentPage = getMemoryPageSafely(address);
                address += PAGE_SIZE;
                offset = 0;
            }
        }
        uint64_t mapped = 0;
        while (mapped < bssLength)
        {
            mapped += PAGE_SIZE;
            setMemoryPageIfAbsent(address, MemoryPage::MP_READ | MemoryPage::MP_WRITE);
            address += PAGE_SIZE;
        }
        offset = (offset + bssLength) % PAGE_SIZE;
        auto* head = new FreeMemory(0, 0);
        head->next = new FreeMemory(address - PAGE_SIZE + offset, MAX_MEMORY_ADDRESS);
        this->freeMemoryList = head;
    }

    void Memory::destroy() const
    {
        for (uint64_t i = 0; i < PAGE_TABLE_SIZE; ++i)
        {
            if (memoryPageTable[i] == nullptr) continue;
            for (uint64_t j = 0; j < PAGE_TABLE_SIZE; ++j)
            {
                if (memoryPageTable[i][j] == nullptr) continue;
                for (uint64_t k = 0; k < PAGE_TABLE_SIZE; ++k)
                {
                    if (memoryPageTable[i][j][k] == nullptr) continue;
                    for (uint64_t l = 0; l < PAGE_TABLE_SIZE; ++l)
                    {
                        if (memoryPageTable[i][j][k][l] != nullptr)
                        {
                            memoryPageTable[i][j][k][l]->destroy();
                            delete memoryPageTable[i][j][k][l];
                        }
                    }
                    delete[] memoryPageTable[i][j][k];
                }
                delete[] memoryPageTable[i][j];
            }
            delete[] memoryPageTable[i];
        }
        delete[] memoryPageTable;
        delete freeMemoryList;
    }


    void Memory::lock()
    {
        this->_lock.lock();
    }

    void Memory::unlock()
    {
        this->_lock.unlock();
    }

    uint64_t Memory::allocateMemory(ThreadHandle* threadHandle, const uint64_t size)
    {
        std::lock_guard lock(_mutex);
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
                this->setLong(threadHandle, start, size);
                return start + 8;
            }
            freeMemory = freeMemory->next;
        }
        throw VMException("Out of memory");
    }

    uint64_t Memory::reallocateMemory(ThreadHandle* threadHandle, uint64_t address, uint64_t size)
    {
        std::lock_guard lock(_mutex);
        const uint64_t oldSize = this->getLong(threadHandle, address - 8);
        auto* bytes = new uint8_t[oldSize];
        for (uint64_t i = 0; i < oldSize; i++) bytes[i] = this->getByte(threadHandle, address + i);
        this->freeMemory(threadHandle, address);
        const uint64_t newAddress = this->allocateMemory(threadHandle, size);
        for (uint64_t i = 0; i < std::min(oldSize, size); i++) this->setByte(threadHandle, newAddress + i, bytes[i]);
        delete[] bytes;
        return newAddress;
    }

    void Memory::freeMemory(ThreadHandle* threadHandle, uint64_t address)
    {
        std::lock_guard lock(_mutex);
        address -= 8;
        const uint64_t size = this->getLong(threadHandle, address) + 8;
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
            pud = new MemoryPage***[PAGE_TABLE_SIZE]{};
            memoryPageTable[pgdOffset] = pud;
        }
        uint64_t pudOffset = (address >> 30) & 0x1ff;
        MemoryPage*** pmd = pud[pudOffset];
        if (pmd == nullptr)
        {
            pmd = new MemoryPage**[PAGE_TABLE_SIZE]{};
            pud[pudOffset] = pmd;
        }
        uint64_t pmdOffset = (address >> 21) & 0x1ff;
        MemoryPage** pte = pmd[pmdOffset];
        if (pte == nullptr)
        {
            pte = new MemoryPage*[PAGE_TABLE_SIZE]{};
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


    uint8_t Memory::getByte(ThreadHandle* threadHandle, uint64_t address)
    {
        return getMemoryPageSafely(address)->getByte(threadHandle, address & PAGE_OFFSET_MASK);
    }

    uint16_t Memory::getShort(ThreadHandle* threadHandle, uint16_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 1) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getShort(threadHandle, address & PAGE_OFFSET_MASK);
        }
        return static_cast<uint16_t>(getMemoryPageSafely(address)->getByte(threadHandle, address & PAGE_OFFSET_MASK) | (
            getMemoryPageSafely(address + 1)->getByte(threadHandle, 0) << 8));
    }

    uint32_t Memory::getInt(ThreadHandle* threadHandle, uint32_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getInt(threadHandle, address & PAGE_OFFSET_MASK);
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; i++)
        {
            value |= static_cast<uint32_t>(getMemoryPageSafely(address)->getByte(
                    threadHandle, address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return value;
    }

    uint64_t Memory::getLong(ThreadHandle* threadHandle, uint64_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 7) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getLong(threadHandle, address & PAGE_OFFSET_MASK);
        }
        uint64_t value = 0;
        for (uint64_t i = 0; i < 8; i++)
        {
            value |= static_cast<uint64_t>(getMemoryPageSafely(address)->getByte(
                    threadHandle, address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return value;
    }

    float Memory::getFloat(ThreadHandle* threadHandle, uint64_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getFloat(threadHandle, address & PAGE_OFFSET_MASK);
        }
        uint32_t value = 0;
        for (uint64_t i = 0; i < 4; i++)
        {
            value |= static_cast<uint32_t>(getMemoryPageSafely(address)->getByte(
                    threadHandle, address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return std::bit_cast<float>(value);
    }

    double Memory::getDouble(ThreadHandle* threadHandle, uint64_t address)
    {
        if (((address & PAGE_OFFSET_MASK) + 7) < PAGE_SIZE)
        {
            return getMemoryPageSafely(address)->getDouble(threadHandle, address & PAGE_OFFSET_MASK);
        }
        uint64_t value = 0;
        for (uint64_t i = 0; i < 8; i++)
        {
            value |= static_cast<uint64_t>(getMemoryPageSafely(address)->getByte(
                    threadHandle, address & PAGE_OFFSET_MASK))
                << (i * 8);
            ++address;
        }
        return std::bit_cast<double>(value);
    }

    void Memory::setByte(ThreadHandle* threadHandle, uint64_t address, uint8_t value)
    {
        getMemoryPageSafely(address)->setByte(threadHandle, address & PAGE_OFFSET_MASK, value);
    }

    void Memory::setShort(ThreadHandle* threadHandle, uint64_t address, uint16_t value)
    {
        if (((address & PAGE_OFFSET_MASK) + 1) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setShort(threadHandle, address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            getMemoryPageSafely(address)->setByte(threadHandle, address & PAGE_OFFSET_MASK,
                                                  static_cast<uint8_t>(value & 0xFF));
            getMemoryPageSafely(address + 1)->setByte(threadHandle, 0, static_cast<uint8_t>(value >> 8));
        }
    }

    void Memory::setInt(ThreadHandle* threadHandle, uint64_t address, uint32_t value)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setInt(threadHandle, address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            for (uint64_t i = 0; i < 4; i++)
            {
                getMemoryPageSafely(address)->setByte(threadHandle,
                                                      (address & PAGE_OFFSET_MASK),
                                                      static_cast<uint8_t>(value >> (i * 8)));
                ++address;
            }
        }
    }

    void Memory::setLong(ThreadHandle* threadHandle, uint64_t address, uint64_t value)
    {
        if (((address & PAGE_OFFSET_MASK) + 7) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setLong(threadHandle, address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            for (uint64_t i = 0; i < 8; ++i)
            {
                getMemoryPageSafely(address)->setByte(threadHandle,
                                                      address & PAGE_OFFSET_MASK,
                                                      static_cast<uint8_t>(value >> (i * 8)));
                ++address;
            }
        }
    }

    void Memory::setFloat(ThreadHandle* threadHandle, uint64_t address, float value)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setFloat(threadHandle, address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            auto bits = std::bit_cast<uint32_t>(value);
            for (uint64_t i = 0; i < 4; ++i)
            {
                getMemoryPageSafely(address)->setByte(threadHandle, address & PAGE_OFFSET_MASK,
                                                      static_cast<uint8_t>(bits & 0xFF));
                bits >>= 8;
                ++address;
            }
        }
    }

    void Memory::setDouble(ThreadHandle* threadHandle, uint64_t address, const double value)
    {
        if (((address & PAGE_OFFSET_MASK) + 3) < PAGE_SIZE)
        {
            getMemoryPageSafely(address)->setDouble(threadHandle, address & PAGE_OFFSET_MASK, value);
        }
        else
        {
            auto bits = std::bit_cast<uint64_t>(value);
            for (auto i = 0; i < 8; ++i)
            {
                getMemoryPageSafely(address)->setByte(threadHandle, address & PAGE_OFFSET_MASK,
                                                      static_cast<uint8_t>(bits & 0xFF));
                bits >>= 8;
                ++address;
            }
        }
    }

    MemoryPage::MemoryPage(const uint64_t start, const uint32_t flags): flags(flags), _start(start)
    {
    }

    uint64_t MemoryPage::start() const
    {
        return _start;
    }


    void MemoryPage::initialize()
    {
        std::lock_guard lock(_mutex);
        if ((this->flags & MP_PRESENT) != 0) return;
        this->data = new uint8_t[Memory::PAGE_SIZE]{};
        this->flags |= MP_PRESENT;
    }

    void MemoryPage::retain()
    {
        std::lock_guard lock(_mutex);
        ++this->referenceCount;
    }

    void MemoryPage::release()
    {
        std::lock_guard lock(_mutex);
        --this->referenceCount;
        if (this->referenceCount == 0) this->destroy();
    }

    void MemoryPage::destroy()
    {
        std::lock_guard lock(_mutex);
        delete[] this->data;
        this->flags &= ~MP_PRESENT;
    }

    uint8_t MemoryPage::getByte(ThreadHandle* threadHandle, uint64_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable(threadHandle))
            return this->data[offset];
        return 0;
    }

    uint16_t MemoryPage::getShort(ThreadHandle* threadHandle, uint64_t address)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable(threadHandle))
            return *reinterpret_cast<uint16_t*>(&this->data[address]);
        return 0;
    }

    uint32_t MemoryPage::getInt(ThreadHandle* threadHandle, uint64_t address)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable(threadHandle))
            return *reinterpret_cast<uint32_t*>(&this->data[address]);
        return 0;
    }

    uint64_t MemoryPage::getLong(ThreadHandle* threadHandle, uint64_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable(threadHandle))
            return *reinterpret_cast<uint64_t*>(&this->data[offset]);
        return 0;
    }

    float MemoryPage::getFloat(ThreadHandle* threadHandle, uint64_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable(threadHandle))
            return *reinterpret_cast<float*>(&this->data[offset]);
        return 0;
    }

    double MemoryPage::getDouble(ThreadHandle* threadHandle, uint64_t offset)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkReadable(threadHandle))
            return *reinterpret_cast<double*>(&this->data[offset]);
        return 0;
    }

    void MemoryPage::setByte(ThreadHandle* threadHandle, uint64_t offset, uint8_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable(threadHandle))
            this->data[offset] = value;
    }

    void MemoryPage::setShort(ThreadHandle* threadHandle, uint64_t offset, uint16_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable(threadHandle))
            *reinterpret_cast<uint16_t*>(&this->data[offset]) = value;
    }

    void MemoryPage::setInt(ThreadHandle* threadHandle, uint64_t offset, uint32_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable(threadHandle))
            *reinterpret_cast<uint32_t*>(&this->data[offset]) = value;
    }

    void MemoryPage::setLong(ThreadHandle* threadHandle, uint64_t offset, uint64_t value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable(threadHandle))
            *reinterpret_cast<uint64_t*>(&this->data[offset]) = value;
    }

    void MemoryPage::setFloat(ThreadHandle* threadHandle, uint64_t offset, float value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable(threadHandle))
            *reinterpret_cast<float*>(&this->data[offset]) = value;
    }

    void MemoryPage::setDouble(ThreadHandle* threadHandle, uint64_t offset, double value)
    {
        if ((this->flags & MP_PRESENT) == 0) initialize();
        if (this->checkWritable(threadHandle))
            *reinterpret_cast<double*>(&this->data[offset]) = value;
    }

    bool MemoryPage::checkReadable(ThreadHandle* threadHandle) const
    {
        bool readable = (this->flags & MP_READ) != 0;
        if (!readable)
        {
            // if (threadHandle != nullptr)
            // {
            // ExecutionUnit* executionUnit = threadHandle->executionUnit;
            // executionUnit->registers[bytecode::FLAGS_REGISTER] = executionUnit->registers[bytecode::FLAGS_REGISTER]
            // | bytecode::PAGE_NOT_READABLE;
            // }
            // else
            // {
            throw VMException("Attempt to read from a non-readable memory page");
            // }
        }
        return readable;
    }

    bool MemoryPage::checkWritable(ThreadHandle* threadHandle) const
    {
        bool writable = (this->flags & MP_WRITE) != 0;
        if (!writable)
        {
            // if (threadHandle != nullptr)
            // {
            // ExecutionUnit* executionUnit = threadHandle->executionUnit;
            // executionUnit->registers[bytecode::FLAGS_REGISTER] = executionUnit->registers[bytecode::FLAGS_REGISTER]
            // | bytecode::PAGE_NOT_WRITABLE;
            // }
            // else
            // {
            throw VMException("Attempt to write to a non-writable memory page");
            // }
        }
        return writable;
    }

    bool MemoryPage::checkExecutable(ThreadHandle* threadHandle) const
    {
        bool executable = (this->flags & MP_EXEC) != 0;
        if (!executable)
        {
            // if (threadHandle != nullptr)
            // {
            // ExecutionUnit* executionUnit = threadHandle->executionUnit;
            // executionUnit->registers[bytecode::FLAGS_REGISTER] = executionUnit->registers[bytecode::FLAGS_REGISTER]
            // | bytecode::PAGE_NOT_EXECUTABLE;
            // }
            // else
            // {
            throw VMException("Attempt to execute from a non-executable memory page");
            // }
        }
        return executable;
    }

    FreeMemory::FreeMemory(uint64_t start, uint64_t end): start(start), end(end)
    {
    }

    FreeMemory::~FreeMemory()
    {
        delete this->next;
    }
}
