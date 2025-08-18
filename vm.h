//
// Created by XiaoLi on 25-8-14.
//

#ifndef VM_H
#define VM_H
#include <cstdint>
#include <map>
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
        uint64_t createThread(uint64_t entryPoint);
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
        std::mutex _mutex;

        ExecutionUnit* createExecutionUnit(uint64_t entryPoint);
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
        explicit ExecutionUnit(VirtualMachine* virtualMachine);
        void init(uint64_t stackBase, uint64_t entryPoint);
        void setThreadHandle(ThreadHandle* threadHandle);
        void execute();
        void interrupt(uint8_t interruptNumber);
        void destroy();

    private:
        VirtualMachine* virtualMachine;
        ThreadHandle* threadHandle = nullptr;
        uint64_t* registers = nullptr;
        std::mutex _mutex;
    };

    class FileHandle
    {
    public:
        FileHandle(std::string path, uint32_t flags, uint32_t mode, std::istream* inputStream,
                   std::ostream* outputStream);
        FileHandle(std::string path, uint32_t flags, uint32_t mode);
        ~FileHandle();
        uint32_t read(uint8_t* buffer, uint32_t count) const;
        uint32_t write(const uint8_t* buffer, uint32_t count);

    private:
        const std::string path;
        const uint32_t flags;
        const uint32_t mode;
        std::istream* inputStream;
        std::ostream* outputStream;
    };
}
#endif //VM_H
