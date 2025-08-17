//
// Created by XiaoLi on 25-8-14.
//

#ifndef VM_H
#define VM_H
#include <cstdint>
#include <map>
#include <string>

#include "memory.h"
#include "module.h"

namespace lvm
{
    constexpr uint64_t DEFAULT_STACK_SIZE = 4 * 1024 * 1024;

    class VirtualMachine;
    class ThreadHandle;
    class ExecutionUnit;
    class FileHandle;

    class VirtualMachine
    {
    public:
        const uint64_t stackSize;
        Memory* memory;
        uint64_t entryPoint = 0;

        explicit VirtualMachine(uint64_t stackSize);
        int init(const Module* module);
        int run();
        uint64_t createThread(uint64_t entryPoint);

    private:
        std::map<uint64_t, ThreadHandle*> threadID2Handle;
        std::map<uint64_t, FileHandle*> fd2FileHandle;
        uint64_t lastFd = 0;
        uint64_t lastThreadID = 0;
        std::mutex _mutex;

        ExecutionUnit* createExecutionUnit(uint64_t threadID, uint64_t entryPoint);
        uint64_t getThreadID();
        uint64_t getFd();
    };

    class ThreadHandle
    {
    public:
        ExecutionUnit* executionUnit;
        explicit ThreadHandle(ExecutionUnit* executionUnit);
    };

    class ExecutionUnit
    {
    public:
        explicit ExecutionUnit(VirtualMachine* virtualMachine);
        void init(uint64_t threadID, uint64_t stackBase, uint64_t entryPoint);
        void execute();

    private:
        VirtualMachine* virtualMachine;
        uint64_t threadID = 0;
        uint64_t* registers = nullptr;
    };

    class FileHandle
    {
    public:
        FileHandle(std::string path, uint32_t flags, uint32_t mode, const std::istream* inputStream,
                   const std::ostream* outputStream);
        FileHandle(std::string path, uint32_t flags, uint32_t mode);

    private:
        const std::string path;
        const uint32_t flags;
        const uint32_t mode;
        const std::istream* inputStream;
        const std::ostream* outputStream;
    };
}
#endif //VM_H
