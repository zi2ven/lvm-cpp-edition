//
// Created by XiaoLi on 25-8-14.
//

#ifndef VM_H
#define VM_H
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "memory.h"
#include "module.h"

namespace lvm
{
    constexpr uint64_t DEFAULT_STACK_SIZE = 4 * 1024 * 1024;
    constexpr uint64_t LVM_VERSION = 0;

    class VirtualMachine;
    class ThreadHandle;
    class ExecutionUnit;
    class FileHandle;
    class Memory;
    class MemoryPage;
    class FreeMemory;

    class VirtualMachine
    {
    public:
        const uint64_t stackSize;
        Memory* memory;
        std::map<uint64_t, ThreadHandle*> threadID2Handle;
        uint64_t entryPoint = 0;

        explicit VirtualMachine(uint64_t stackSize);
        int init(const Module* module);
        void destroy();
        int run();
        uint64_t createThread(ThreadHandle* threadHandle, uint64_t entryPoint);
        uint64_t open(const char* path, uint32_t flags, uint32_t mode);
        uint64_t close(uint64_t fd);
        uint32_t read(uint64_t fd, uint8_t* buffer, uint32_t count);
        uint32_t write(uint64_t fd, const uint8_t* buffer, uint32_t count);
        void exit(uint64_t status);

    private:
        bool running = false;
        std::map<uint64_t, FileHandle*> fd2FileHandle;
        uint64_t lastFd = 0;
        uint64_t lastThreadID = 0;
        std::recursive_mutex _mutex;

        ExecutionUnit* createExecutionUnit(ThreadHandle* threadHandle, uint64_t entryPoint);
        void destroyThread(const ThreadHandle* threadHandle);
        uint64_t getThreadID();
        uint64_t getFd();
    };

    class ThreadHandle
    {
    public:
        const uint64_t threadID;
        ExecutionUnit* executionUnit;
        std::thread* _thread = nullptr;
        ThreadHandle(uint64_t threadID, ExecutionUnit* executionUnit);
        ~ThreadHandle();
        void start();

    private:
        std::mutex _mutex;
    };

    class ExecutionUnit
    {
    public:
        uint64_t* registers = nullptr;

        explicit ExecutionUnit(VirtualMachine* virtualMachine);
        void init(uint64_t stackBase, uint64_t entryPoint);
        void setThreadHandle(ThreadHandle* threadHandle);
        void execute();
        void interrupt(uint8_t interruptNumber);
        void destroy();

    private:
        VirtualMachine* virtualMachine;
        ThreadHandle* threadHandle = nullptr;
        std::mutex _mutex;
    };

    class FileHandle
    {
    public:
        static constexpr uint32_t FH_READ = 1;
        static constexpr uint32_t FH_WRITE = 1 << 1;

        FileHandle(std::string path, uint32_t flags, uint32_t mode, std::istream* inputStream,
                   std::ostream* outputStream);
        FileHandle(std::string path, uint32_t flags, uint32_t mode);
        ~FileHandle();
        uint32_t read(uint8_t* buffer, uint32_t count) const;
        uint32_t write(const uint8_t* buffer, uint32_t count);

    private:
        static constexpr uint32_t FH_PREOPEN = 1 << 2;
        const std::string path;
        const uint32_t flags;
        const uint32_t mode;
        std::istream* inputStream;
        std::ostream* outputStream;
    };

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
        uint64_t allocateMemory(ThreadHandle* threadHandle, uint64_t size);
        uint64_t reallocateMemory(ThreadHandle* threadHandle, uint64_t address, uint64_t size);
        void freeMemory(ThreadHandle* threadHandle, uint64_t address);
        uint8_t getByte(ThreadHandle* threadHandle, uint64_t address);
        uint16_t getShort(ThreadHandle* threadHandle, uint16_t address);
        uint32_t getInt(ThreadHandle* threadHandle, uint32_t address);
        uint64_t getLong(ThreadHandle* threadHandle, uint64_t address);
        float getFloat(ThreadHandle* threadHandle, uint64_t address);
        double getDouble(ThreadHandle* threadHandle, uint64_t address);
        void setByte(ThreadHandle* threadHandle, uint64_t address, uint8_t value);
        void setShort(ThreadHandle* threadHandle, uint64_t address, uint16_t value);
        void setInt(ThreadHandle* threadHandle, uint64_t address, uint32_t value);
        void setLong(ThreadHandle* threadHandle, uint64_t address, uint64_t value);
        void setFloat(ThreadHandle* threadHandle, uint64_t address, float value);
        void setDouble(ThreadHandle* threadHandle, uint64_t address, double value);

    private:
        std::recursive_mutex _mutex;
        std::recursive_mutex _lock;

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
        uint64_t start() const;
        void initialize();
        void retain();
        void release();
        void destroy();

        uint8_t getByte(ThreadHandle* threadHandle, uint64_t offset);
        uint16_t getShort(ThreadHandle* threadHandle, uint64_t address);
        uint32_t getInt(ThreadHandle* threadHandle, uint64_t address);
        uint64_t getLong(ThreadHandle* threadHandle, uint64_t offset);
        float getFloat(ThreadHandle* threadHandle, uint64_t offset);
        double getDouble(ThreadHandle* threadHandle, uint64_t offset);
        void setByte(ThreadHandle* threadHandle, uint64_t offset, uint8_t value);
        void setShort(ThreadHandle* threadHandle, uint64_t offset, uint16_t value);
        void setInt(ThreadHandle* threadHandle, uint64_t offset, uint32_t value);
        void setLong(ThreadHandle* threadHandle, uint64_t offset, uint64_t value);
        void setFloat(ThreadHandle* threadHandle, uint64_t offset, float value);
        void setDouble(ThreadHandle* threadHandle, uint64_t offset, double value);
        [[nodiscard]] bool checkReadable(ThreadHandle* threadHandle) const;
        [[nodiscard]] bool checkWritable(ThreadHandle* threadHandle) const;
        [[nodiscard]] bool checkExecutable(ThreadHandle* threadHandle) const;

    private:
        uint64_t _start;
        uint8_t* data = nullptr;
        std::recursive_mutex _mutex;
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
#endif //VM_H
