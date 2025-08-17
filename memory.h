//
// Created by XiaoLi on 25-8-14.
//

#ifndef MEMORY_H
#define MEMORY_H
#include <cstdint>
#include <mutex>

namespace lvm
{
    class Memory;
    class MemoryPage;
    class FreeMemory;

    class Memory
    {
    public:
        static constexpr uint64_t MAX_MEMORY_ADDRESS = 0x0000ffffffffffffL;
        static constexpr uint64_t PAGE_TABLE_SIZE = 512;
        static constexpr uint64_t PAGE_SIZE = 4096;
        static constexpr uint64_t PAGE_OFFSET_MASK = 0xFFF;
        MemoryPage***** memoryPageTable = nullptr;
        FreeMemory* freeMemoryList = nullptr;

        Memory();
        void init(const uint8_t* text, uint64_t textLength, const uint8_t* rodata, uint64_t rodataLength,
                  const uint8_t* data, uint64_t dataLength, uint64_t bssLength);
        void destroy();
        void lock();
        void unlock();
        uint64_t allocateMemory(uint64_t size);
        uint64_t reallocateMemory(uint64_t address, uint64_t size);
        void freeMemory(uint64_t address);
        uint8_t getByte(uint64_t address);
        uint16_t getShort(uint16_t address);
        uint32_t getInt(uint32_t address);
        uint64_t getLong(uint64_t address);
        float getFloat(uint64_t address);
        double getDouble(uint64_t address);
        void setByte(uint64_t address, uint8_t value);
        void setShort(uint64_t address, uint16_t value);
        void setInt(uint64_t address, uint32_t value);
        void setLong(uint64_t address, uint64_t value);
        void setFloat(uint64_t address, float value);
        void setDouble(uint64_t address, double value);

    private:
        std::mutex _mutex;
        std::mutex _lock;

        MemoryPage* getMemoryPageSafely(uint64_t address);
        MemoryPage* getMemoryPage(uint64_t address);
        void releaseMemoryPage(uint64_t address);
        bool setMemoryPageIfAbsent(uint64_t address, uint32_t flags);
        void resetMemoryPageIfExist(uint64_t address);
    };

    class MemoryPage
    {
    public:
        static constexpr uint32_t MP_READ = 1;
        static constexpr uint32_t MP_WRITE = 1 << 1;
        static constexpr uint32_t MP_EXEC = 1 << 2;
        static constexpr uint32_t MP_PRESENT = 1 << 3;

        uint32_t flags;
        uint64_t referenceCount = 0;

        MemoryPage(uint64_t start, uint32_t flags);
        void initialize();
        void retain();
        void release();

        uint8_t getByte(uint8_t offset);
        uint16_t getShort(uint16_t offset);
        uint32_t getInt(uint32_t offset);
        uint64_t getLong(uint64_t offset);
        float getFloat(uint64_t offset);
        double getDouble(uint64_t offset);
        void setByte(uint8_t offset, uint8_t value);
        void setShort(uint16_t offset, uint16_t value);
        void setInt(uint32_t offset, uint32_t value);
        void setLong(uint64_t offset, uint64_t value);
        void setFloat(uint64_t offset, float value);
        void setDouble(uint64_t offset, double value);
        [[nodiscard]] bool checkReadable() const;
        [[nodiscard]] bool checkWritable() const;
        [[nodiscard]] bool checkExecutable() const;

    private:
        uint64_t start;
        uint8_t* data = nullptr;
        std::mutex _mutex;

        void destroy();
    };

    class FreeMemory
    {
    public:
        uint64_t start;
        uint64_t end;
        FreeMemory* next = nullptr;

        FreeMemory(uint64_t start, uint64_t end);
        ~FreeMemory();
    };
}

#endif //MEMORY_H
