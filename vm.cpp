//
// Created by XiaoLi on 25-8-14.
//
#include "vm.h"

#include <fstream>
#include <iostream>
#include <utility>
#include <cmath>
#include <ranges>

#include "bytecode.h"
#include "exception.h"
#include "module.h"

namespace lvm
{
    VirtualMachine::VirtualMachine(uint64_t stackSize): stackSize(stackSize)
    {
        this->memory = new Memory();
    }

    int VirtualMachine::init(const Module* module)
    {
        this->memory->init(module->text, module->textLength, module->rodata, module->rodataLength, module->data,
                           module->dataLength, module->bssLength);
        this->entryPoint = module->entryPoint;

        this->fd2FileHandle.insert(std::make_pair(0, new FileHandle("stdin", 0, 0, stdin, nullptr)));
        this->fd2FileHandle.insert(std::make_pair(1, new FileHandle("stdout", 0, 0, nullptr, stdout)));
        this->fd2FileHandle.insert(std::make_pair(2, new FileHandle("stderr", 0, 0, nullptr, stderr)));

        this->lastFd = 2;

        return 0;
    }

    void VirtualMachine::destroy()
    {
        memory->destroy();
        delete this->memory;
        this->memory = nullptr;
        for (const auto& val : this->fd2FileHandle | std::views::values)
            delete val;
    }


    int VirtualMachine::run()
    {
        this->createThread(nullptr, this->entryPoint);
        running = true;
        while (running && !threadID2Handle.empty())
        {
            const ThreadHandle* threadHandle = threadID2Handle.begin()->second;
            threadHandle->_thread->join();
            destroyThread(threadHandle);
        }
        return 0;
    }

    uint64_t VirtualMachine::createThread(ThreadHandle* threadHandle, const uint64_t entryPoint)
    {
        uint64_t threadID = this->getThreadID();
        ExecutionUnit* executionUnit = this->createExecutionUnit(threadHandle, entryPoint);
        auto* handle = new ThreadHandle(threadID, executionUnit);
        executionUnit->setThreadHandle(handle);
        this->threadID2Handle.insert(std::make_pair(threadID, handle));
        handle->start();
        return threadID;
    }

    ExecutionUnit* VirtualMachine::createExecutionUnit(ThreadHandle* threadHandle, const uint64_t entryPoint)
    {
        auto* executionUnit = new ExecutionUnit(this);
        uint64_t stack = this->memory->allocateMemory(threadHandle, this->stackSize);
        executionUnit->init(stack + this->stackSize - 1, entryPoint);
        return executionUnit;
    }

    void VirtualMachine::destroyThread(const ThreadHandle* threadHandle)
    {
        threadHandle->executionUnit->destroy();
        threadID2Handle.erase(threadHandle->threadID);
        if (threadHandle->threadID <= lastThreadID) lastThreadID = threadHandle->threadID - 1;
        delete threadHandle;
    }

    uint64_t VirtualMachine::open(const char* path, uint32_t flags, uint32_t mode)
    {
        const uint64_t fd = getFd();
        fd2FileHandle[fd] = new FileHandle(path, flags, mode);
        return fd;
    }

    uint64_t VirtualMachine::close(uint64_t fd)
    {
        FileHandle* fileHandle = fd2FileHandle[fd];
        fd2FileHandle.erase(fd);
        if (fd <= lastFd) lastFd = fd - 1;
        delete fileHandle;
        return 0;
    }

    uint32_t VirtualMachine::read(const uint64_t fd, uint8_t* buffer, const uint32_t count)
    {
        FileHandle* fileHandle = fd2FileHandle[fd];
        if (fileHandle == nullptr)
        {
            throw VMException("Invalid file descriptor: " + fd);
        }
        return fileHandle->_read(buffer, count);
    }

    uint32_t VirtualMachine::write(const uint64_t fd, const uint8_t* buffer, const uint32_t count)
    {
        const FileHandle* fileHandle = fd2FileHandle[fd];
        if (fileHandle == nullptr)
        {
            throw VMException("Invalid file descriptor: " + fd);
        }
        return fileHandle->_write(buffer, count);
    }

    void VirtualMachine::exit(uint64_t status)
    {
        // TODO
        running = false;
    }


    uint64_t VirtualMachine::getThreadID()
    {
        std::lock_guard lock(_mutex);
        uint64_t threadID = this->lastThreadID + 1;
        while (this->threadID2Handle.contains(threadID)) ++threadID;
        this->lastThreadID = threadID;
        return threadID;
    }

    uint64_t VirtualMachine::getFd()
    {
        std::lock_guard lock(_mutex);
        uint64_t fd = this->lastFd + 1;
        while (this->fd2FileHandle.contains(fd)) ++fd;
        this->lastFd = fd;
        return fd;
    }


    ThreadHandle::ThreadHandle(const uint64_t threadID, ExecutionUnit* executionUnit): threadID(threadID),
        executionUnit(executionUnit)
    {
    }

    ThreadHandle::~ThreadHandle()
    {
        delete this->executionUnit;
        delete this->_thread;
    }

    void ThreadHandle::start()
    {
        std::lock_guard lock(_mutex);
        if (this->_thread == nullptr)
        {
            this->_thread = new std::thread(&ExecutionUnit::execute, this->executionUnit);
        }
    }


    ExecutionUnit::ExecutionUnit(VirtualMachine* virtualMachine): virtualMachine(virtualMachine)
    {
    }

    void ExecutionUnit::init(uint64_t stackBase, uint64_t entryPoint)
    {
        this->registers = new uint64_t[bytecode::REGISTER_COUNT]{};

        this->registers[bytecode::BP_REGISTER] = stackBase;
        this->registers[bytecode::SP_REGISTER] = stackBase;
        this->registers[bytecode::PC_REGISTER] = entryPoint;
    }

    void ExecutionUnit::setThreadHandle(ThreadHandle* threadHandle)
    {
        this->threadHandle = threadHandle;
    }


    void ExecutionUnit::execute() const
    {
        ThreadHandle* threadHandle = this->threadHandle;
        Memory* memory = this->virtualMachine->memory;
        uint64_t* registers = this->registers;
        auto rStart = std::chrono::high_resolution_clock::now();
        for (;;)
        {
            // std::cout << registers[bytecode::PC_REGISTER] << ": " << bytecode::getInstructionName(
            // this->virtualMachine->memory->getByte(threadHandle, registers[bytecode::PC_REGISTER])) <<
            // std::endl;
            switch (const uint8_t code = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++))
            {
            case bytecode::NOP:
                {
                    break;
                }
            case bytecode::PUSH_1:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    --registers[bytecode::SP_REGISTER];
                    memory->setByte(threadHandle, registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::PUSH_2:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::SP_REGISTER] -= 2;
                    memory->setShort(threadHandle, registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::PUSH_4:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::SP_REGISTER] -= 4;
                    memory->setInt(threadHandle, registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::PUSH_8:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::SP_REGISTER] -= 8;
                    memory->setLong(threadHandle, registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::POP_1:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getByte(threadHandle, registers[bytecode::SP_REGISTER]);
                    ++registers[bytecode::SP_REGISTER];
                    break;
                }
            case bytecode::POP_2:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getShort(threadHandle, registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 2;
                    break;
                }
            case bytecode::POP_4:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getInt(threadHandle, registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 4;
                    break;
                }
            case bytecode::POP_8:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getLong(threadHandle, registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 8;
                    break;
                }
            case bytecode::LOAD_1:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getByte(threadHandle, registers[address]);
                    break;
                }
            case bytecode::LOAD_2:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getShort(threadHandle, registers[address]);
                    break;
                }
            case bytecode::LOAD_4:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getInt(threadHandle, registers[address]);
                    break;
                }
            case bytecode::LOAD_8:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getLong(threadHandle, registers[address]);
                    break;
                }
            case bytecode::STORE_1:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    memory->setByte(threadHandle, registers[address], registers[source]);
                    break;
                }
            case bytecode::STORE_2:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    memory->setShort(threadHandle, registers[address], registers[source]);
                    break;
                }
            case bytecode::STORE_4:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    memory->setInt(threadHandle, registers[address], registers[source]);
                    break;
                }
            case bytecode::STORE_8:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    memory->setLong(threadHandle, registers[address], registers[source]);
                    break;
                }
            case bytecode::CMP:
                {
                    const uint8_t type = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    auto value1 = static_cast<int64_t>(registers[operand1]);
                    auto value2 = static_cast<int64_t>(registers[operand2]);
                    uint64_t flags = registers[bytecode::FLAGS_REGISTER];
                    if (type == bytecode::FLOAT_TYPE)
                    {
                        const auto float1 = std::bit_cast<float>(static_cast<uint32_t>(value1 & 0xFFFFFFFFL));
                        const auto float2 = std::bit_cast<float>(static_cast<uint32_t>(value2 & 0xFFFFFFFFL));
                        const bool result = float1 < float2;
                        flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) | ((
                            result ? 0b11 : 0b00) << 1);
                    }
                    else if (type == bytecode::DOUBLE_TYPE)
                    {
                        const auto float1 = std::bit_cast<double>(value1);
                        const auto float2 = std::bit_cast<double>(value2);
                        const bool result = float1 < float2;
                        flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) | ((
                            result ? 0b11 : 0b00) << 1);
                    }
                    else
                    {
                        if (type == bytecode::BYTE_TYPE)
                        {
                            value1 = std::bit_cast<int8_t>(static_cast<uint8_t>(value1 & 0xff));
                            value2 = std::bit_cast<int8_t>(static_cast<uint8_t>(value2 & 0xff));
                        }
                        else if (type == bytecode::SHORT_TYPE)
                        {
                            value1 = static_cast<int16_t>(value1 & 0xffff);
                            value2 = static_cast<int16_t>(value2 & 0xffff);
                        }
                        else if (type == bytecode::INT_TYPE)
                        {
                            value1 = static_cast<int32_t>(value1 & 0xffffffffL);
                            value2 = static_cast<int32_t>(value2 & 0xffffffffL);
                        }
                        else if (type != bytecode::LONG_TYPE)
                        {
                            throw VMException("Unsupported type");
                        }
                        if (value1 == value2)
                        {
                            flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) |
                                1;
                        }
                        else
                        {
                            bool signedResult = value1 < value2;
                            bool unsignedResult = std::bit_cast<uint64_t>(value1) < std::bit_cast<uint64_t>(value2);
                            flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) |
                                (signedResult ? bytecode::CARRY_MASK : 0) | (unsignedResult
                                                                                 ? bytecode::UNSIGNED_MASK
                                                                                 : 0);
                        }
                    }
                    registers[bytecode::FLAGS_REGISTER] = flags;
                    break;
                }
            case bytecode::ATOMIC_CMP:
                {
                    memory->lock();
                    const uint8_t type = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    auto value1 = static_cast<int64_t>(memory->getLong(threadHandle, registers[operand1]));
                    auto value2 = static_cast<int64_t>(registers[operand2]);
                    uint64_t flags = registers[bytecode::FLAGS_REGISTER];
                    if (type == bytecode::FLOAT_TYPE)
                    {
                        const auto float1 = std::bit_cast<float>(static_cast<uint32_t>(value1 & 0xFFFFFFFFL));
                        const auto float2 = std::bit_cast<float>(static_cast<uint32_t>(value2 & 0xFFFFFFFFL));
                        const bool result = float1 < float2;
                        flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) | ((
                            result ? 0b11 : 0b00) << 1);
                    }
                    else if (type == bytecode::DOUBLE_TYPE)
                    {
                        const auto float1 = std::bit_cast<double>(value1);
                        const auto float2 = std::bit_cast<double>(value2);
                        const bool result = float1 < float2;
                        flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) | ((
                            result ? 0b11 : 0b00) << 1);
                    }
                    else
                    {
                        if (type == bytecode::BYTE_TYPE)
                        {
                            value1 = static_cast<int8_t>(value1 & 0xff);
                            value2 = static_cast<int8_t>(value2 & 0xff);
                        }
                        else if (type == bytecode::SHORT_TYPE)
                        {
                            value1 = static_cast<int16_t>(value1 & 0xffff);
                            value2 = static_cast<int16_t>(value2 & 0xffff);
                        }
                        else if (type == bytecode::INT_TYPE)
                        {
                            value1 = static_cast<int32_t>(value1 & 0xffffffffL);
                            value2 = static_cast<int32_t>(value2 & 0xffffffffL);
                        }
                        else if (type != bytecode::LONG_TYPE)
                        {
                            throw VMException("Unsupported type");
                        }
                        if (value1 == value2)
                        {
                            flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) |
                                1;
                        }
                        else
                        {
                            bool signedResult = value1 < value2;
                            bool unsignedResult = std::bit_cast<uint64_t>(value1) < std::bit_cast<uint64_t>(value2);
                            flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) |
                                ((signedResult ? 1 : 0) << 1) | ((unsignedResult ? 1 : 0) << 2);
                        }
                    }
                    registers[bytecode::FLAGS_REGISTER] = flags;
                    memory->unlock();
                    break;
                }
            case bytecode::MOV_E:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if ((registers[bytecode::FLAGS_REGISTER] & bytecode::ZERO_MASK) != 0)
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_NE:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if ((registers[bytecode::FLAGS_REGISTER] & bytecode::ZERO_MASK) == 0)
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_L:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::CARRY_MASK) != 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_LE:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::CARRY_MASK) != 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_G:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::CARRY_MASK) == 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_GE:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::CARRY_MASK) == 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_UL:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::UNSIGNED_MASK) != 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_ULE:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::UNSIGNED_MASK) != 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_UG:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::UNSIGNED_MASK) == 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_UGE:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::UNSIGNED_MASK) == 0))
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV:
                {
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[source];
                    break;
                }
            case bytecode::MOV_IMMEDIATE1:
                {
                    const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = value;
                    break;
                }
            case bytecode::MOV_IMMEDIATE2:
                {
                    const uint16_t value = memory->getShort(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 2;
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = value;
                    break;
                }
            case bytecode::MOV_IMMEDIATE4:
                {
                    const uint32_t value = memory->getInt(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 4;
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = value;
                    break;
                }
            case bytecode::MOV_IMMEDIATE8:
                {
                    const uint64_t value = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = value;
                    break;
                }
            case bytecode::JUMP:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JUMP_IMMEDIATE:
                {
                    const uint64_t address = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] = address;
                    break;
                }
            case bytecode::JE:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if ((registers[bytecode::FLAGS_REGISTER] & bytecode::ZERO_MASK) != 0)
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JNE:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if ((registers[bytecode::FLAGS_REGISTER] & bytecode::ZERO_MASK) == 0)
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JL:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::CARRY_MASK) != 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JLE:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::CARRY_MASK) != 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JG:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::CARRY_MASK) == 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JGE:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::CARRY_MASK) == 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JUL:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::UNSIGNED_MASK) != 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JULE:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::UNSIGNED_MASK) != 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JUG:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::UNSIGNED_MASK) == 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::JUGE:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::UNSIGNED_MASK) == 0))
                        registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::MALLOC:
                {
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->allocateMemory(threadHandle, registers[size]);
                    break;
                }
            case bytecode::FREE:
                {
                    const uint8_t ptr = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    memory->freeMemory(threadHandle, registers[ptr]);
                    break;
                }
            case bytecode::REALLOC:
                {
                    const uint8_t ptr = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->reallocateMemory(threadHandle, registers[ptr], registers[size]);
                    break;
                }
            case bytecode::ADD:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] + registers[operand2];
                    break;
                }
            case bytecode::SUB:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] - registers[operand2];
                    break;
                }
            case bytecode::MUL:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] * registers[operand2];
                    break;
                }
            case bytecode::DIV:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] / registers[operand2];
                    break;
                }
            case bytecode::MOD:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] % registers[operand2];
                    break;
                }
            case bytecode::AND:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] & registers[operand2];
                    break;
                }
            case bytecode::OR:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] | registers[operand2];
                    break;
                }
            case bytecode::XOR:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] ^ registers[operand2];
                    break;
                }
            case bytecode::NOT:
                {
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = ~registers[operand];
                    break;
                }
            case bytecode::NEG:
                {
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(-static_cast<int64_t>(registers[operand]));
                    break;
                }
            case bytecode::SHL:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] << registers[operand2];
                    break;
                }
            case bytecode::SHR:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<int64_t>(registers[operand1]) >> registers[operand2];
                    break;
                }
            case bytecode::USHR:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] >> registers[operand2];
                    break;
                }
            case bytecode::INC:
                {
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    ++registers[operand];
                    break;
                }
            case bytecode::DEC:
                {
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    --registers[operand];
                    break;
                }
            case bytecode::ADD_DOUBLE:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) + std::bit_cast<double>(registers[operand2]));
                    break;
                }
            case bytecode::SUB_DOUBLE:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) - std::bit_cast<double>(registers[operand2]));
                    break;
                }
            case bytecode::MUL_DOUBLE:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) * std::bit_cast<double>(registers[operand2]));
                    break;
                }
            case bytecode::DIV_DOUBLE:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) / std::bit_cast<double>(registers[operand2]));
                    break;
                }
            case bytecode::MOD_DOUBLE:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(std::fmod(
                        std::bit_cast<double>(registers[operand1]), std::bit_cast<double>(registers[operand2])));
                    break;
                }
            case bytecode::ADD_FLOAT:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) +
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    break;
                }
            case bytecode::SUB_FLOAT:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) -
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    break;
                }
            case bytecode::MUL_FLOAT:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) *
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    break;
                }
            case bytecode::DIV_FLOAT:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) /
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    break;
                }
            case bytecode::MOD_FLOAT:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::fmod(std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)),
                                  std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL)))));
                    break;
                }
            case bytecode::ATOMIC_ADD:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] + registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_SUB:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] - registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_MUL:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] * registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_DIV:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] / registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_MOD:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] % registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_AND:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] & registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_OR:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] | registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_XOR:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] ^ registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_NOT:
                {
                    memory->lock();
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = ~registers[operand];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_NEG:
                {
                    memory->lock();
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(-static_cast<int64_t>(registers[operand]));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_SHL:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] << registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_SHR:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<int64_t>(registers[operand1]) >> registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_USHR:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = registers[operand1] >> registers[operand2];
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_INC:
                {
                    memory->lock();
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[operand];
                    const uint64_t tmp = memory->getLong(threadHandle, address) + 1;
                    memory->setLong(threadHandle, address, tmp);
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_DEC:
                {
                    memory->lock();
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[operand];
                    const uint64_t tmp = memory->getLong(threadHandle, address) - 1;
                    memory->setLong(threadHandle, address, tmp);
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_ADD_DOUBLE:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) + std::bit_cast<double>(registers[operand2]));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_SUB_DOUBLE:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) - std::bit_cast<double>(registers[operand2]));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_MUL_DOUBLE:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) * std::bit_cast<double>(registers[operand2]));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_DIV_DOUBLE:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        std::bit_cast<double>(registers[operand1]) / std::bit_cast<double>(registers[operand2]));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_MOD_DOUBLE:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(std::fmod(
                        std::bit_cast<double>(registers[operand1]), std::bit_cast<double>(registers[operand2])));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_ADD_FLOAT:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) +
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_SUB_FLOAT:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) -
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_MUL_FLOAT:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) *
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_DIV_FLOAT:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) /
                        std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_MOD_FLOAT:
                {
                    memory->lock();
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                        std::fmod(std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)),
                                  std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL)))));
                    memory->unlock();
                    break;
                }
            case bytecode::CAS:
                {
                    const uint8_t operand1 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand3 = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    uint64_t value1 = registers[operand1];
                    uint64_t value2 = registers[operand2];
                    uint64_t flags = registers[bytecode::FLAGS_REGISTER];
                    if (value1 == value2)
                    {
                        flags = (flags & ~bytecode::ZERO_MASK) | 1;
                        registers[operand1] = registers[operand3];
                    }
                    else
                    {
                        bool signedResult = value1 < value2;
                        bool unsignedResult = std::bit_cast<uint64_t>(value1) < std::bit_cast<uint64_t>(value2);
                        flags = (flags & ~bytecode::ZERO_MASK & ~bytecode::CARRY_MASK & ~bytecode::UNSIGNED_MASK) |
                            ((signedResult ? 1 : 0) << 1) | ((unsignedResult ? 1 : 0) << 2);
                        registers[operand2] = value1;
                    }
                    registers[bytecode::FLAGS_REGISTER] = flags;
                    break;
                }
            case bytecode::INVOKE:
                {
                    const uint8_t address = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::SP_REGISTER] -= 8;
                    memory->setLong(threadHandle, registers[bytecode::SP_REGISTER], registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] = registers[address];
                    break;
                }
            case bytecode::INVOKE_IMMEDIATE:
                {
                    const uint64_t address = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::SP_REGISTER] -= 8;
                    memory->setLong(threadHandle, registers[bytecode::SP_REGISTER],
                                    registers[bytecode::PC_REGISTER] + 8);
                    registers[bytecode::PC_REGISTER] = address;
                    break;
                }
            case bytecode::RETURN:
                {
                    registers[bytecode::PC_REGISTER] = memory->getLong(threadHandle, registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 8;
                    break;
                }
            case bytecode::INTERRUPT:
                {
                    const uint8_t interruptNumber = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    this->interrupt(interruptNumber);
                    break;
                }
            case bytecode::INTERRUPT_RETURN:
                {
                    registers[bytecode::PC_REGISTER] = memory->getLong(threadHandle, registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 8;
                    registers[bytecode::FLAGS_REGISTER] = memory->getLong(
                        threadHandle, registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 8;
                    break;
                }
            case bytecode::INT_TYPE_CAST:
                {
                    const uint8_t types = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t type1 = types >> 4;
                    const uint8_t type2 = types & 0x0f;
                    const uint64_t src = registers[source];
                    if (type1 == type2)
                    {
                        registers[target] = src;
                    }
                    else
                    {
                        const uint64_t srcBits = (8L < type1) - 1;
                        const uint64_t sign = (src & (1L << srcBits)) >> srcBits;
                        const uint64_t targetBits = (8L < type2) - 1;
                        registers[target] = (sign << targetBits) | (src & ((1L << targetBits) - 1));
                    }
                    break;
                }
            case bytecode::LONG_TO_DOUBLE:
                {
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        static_cast<double>(static_cast<int64_t>(registers[source])));
                    break;
                }
            case bytecode::DOUBLE_TO_LONG:
                {
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        static_cast<int64_t>(std::bit_cast<double>(registers[source])));
                    break;
                }
            case bytecode::DOUBLE_TO_FLOAT:
                {
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint32_t>(
                        static_cast<float>(std::bit_cast<double>(registers[source])));
                    break;
                }
            case bytecode::FLOAT_TO_DOUBLE:
                {
                    const uint8_t source = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[target] = std::bit_cast<uint64_t>(
                        static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(registers[source]))));
                    break;
                }
            case bytecode::OPEN:
                {
                    const uint8_t pathRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t flagsRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t modeRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t resultRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    uint64_t address = registers[pathRegister];
                    std::string path;
                    char c;
                    while ((c = static_cast<char>(memory->getByte(threadHandle, address++))) != '\0') path += c;
                    registers[resultRegister] = virtualMachine->open(path.c_str(), flagsRegister, modeRegister);
                    break;
                }
            case bytecode::CLOSE:
                {
                    const uint8_t fdRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t resultRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[resultRegister] = virtualMachine->close(registers[fdRegister]);
                }
            case bytecode::READ:
                {
                    const uint8_t fdRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t bufferRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t countRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t resultRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    uint64_t bufferAddress = registers[bufferRegister];
                    uint64_t count = registers[countRegister];
                    auto* buffer = new uint8_t[count];
                    uint32_t readCount = virtualMachine->read(registers[fdRegister], buffer, count);
                    registers[resultRegister] = readCount;
                    for (uint64_t i = 0; i < count; i++) memory->setByte(threadHandle, bufferAddress + i, buffer[i]);
                    delete[] buffer;
                    break;
                }
            case bytecode::WRITE:
                {
                    const uint8_t fdRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t bufferRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t countRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t resultRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    uint64_t address = registers[bufferRegister];
                    uint64_t count = registers[countRegister];
                    auto* buffer = new uint8_t[count];
                    for (uint64_t i = 0; i < count; i++)buffer[i] = memory->getByte(threadHandle, address + i);
                    registers[resultRegister] = virtualMachine->write(registers[fdRegister], buffer, count);
                    delete[] buffer;
                    break;
                }
            case bytecode::CREATE_FRAME:
                {
                    const uint64_t size = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    registers[bytecode::SP_REGISTER] -= 8;
                    memory->setLong(threadHandle, registers[bytecode::SP_REGISTER], registers[bytecode::BP_REGISTER]);
                    registers[bytecode::BP_REGISTER] = registers[bytecode::SP_REGISTER];
                    registers[bytecode::SP_REGISTER] -= size;
                    break;
                }
            case bytecode::DESTROY_FRAME:
                {
                    const uint64_t size = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    registers[bytecode::SP_REGISTER] += size;
                    registers[bytecode::BP_REGISTER] = memory->getLong(threadHandle, registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 8;
                    break;
                }
            case bytecode::EXIT:
                {
                    const uint8_t statusRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]);
                    virtualMachine->exit(registers[statusRegister]);
                    goto end;
                }
            case bytecode::EXIT_IMMEDIATE:
                {
                    const uint64_t status = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    virtualMachine->exit(status);
                    goto end;
                }
            case bytecode::GET_FIELD_ADDRESS:
                {
                    const uint8_t objectRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t targetRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[targetRegister] = registers[objectRegister] + offset;
                    break;
                }
            case bytecode::GET_LOCAL_ADDRESS:
                {
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t targetRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[targetRegister] = registers[bytecode::BP_REGISTER] - offset;
                    break;
                }
            case bytecode::GET_PARAMETER_ADDRESS:
                {
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t targetRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[targetRegister] = registers[bytecode::BP_REGISTER] + offset;
                    break;
                }
            case bytecode::CREATE_THREAD:
                {
                    const uint8_t entryPointRegister = memory->
                        getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t resultRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[resultRegister] = virtualMachine->createThread(
                        threadHandle, registers[entryPointRegister]);
                    break;
                }
            case bytecode::THREAD_CONTROL:
                {
                    const uint8_t threadIDRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t command = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    ThreadHandle* handle = virtualMachine->threadID2Handle[registers[threadIDRegister]];
                    switch (command)
                    {
                    case bytecode::TC_STOP:
                        {
                            break;
                        }
                    case bytecode::TC_WAIT:
                        {
                            break;
                        }
                    case bytecode::TC_GET_REGISTER:
                        {
                            const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                            const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                            registers[target] = handle->executionUnit->registers[reg];
                            break;
                        }
                    case bytecode::TC_SET_REGISTER:
                        {
                            const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                            const uint8_t value = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                            handle->executionUnit->registers[reg] = registers[value];
                            break;
                        }
                    }
                    break;
                }
            case bytecode::LOAD_FIELD:
                {
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t objectRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t targetRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[objectRegister] + offset;
                    if (size == 1)
                    {
                        registers[targetRegister] = memory->getByte(threadHandle, address) & 0xFF;
                    }
                    else if (size == 2)
                    {
                        registers[targetRegister] = memory->getShort(threadHandle, address) & 0xFFFF;
                    }
                    else if (size == 4)
                    {
                        registers[targetRegister] = memory->getInt(threadHandle, address) & 0xFFFFFFFFL;
                    }
                    else if (size == 8)
                    {
                        registers[targetRegister] = memory->getLong(threadHandle, address);
                    }
                    else
                    {
                        throw VMException("Unsupported size: " + size);
                    }
                    break;
                }
            case bytecode::STORE_FIELD:
                {
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t objectRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t valueRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);

                    const uint64_t address = registers[objectRegister] + offset;
                    if (size == 1)
                    {
                        memory->setByte(threadHandle, address, registers[valueRegister] & 0xFF);
                    }
                    else if (size == 2)
                    {
                        memory->setShort(threadHandle, address, registers[valueRegister] & 0xFFFF);
                    }
                    else if (size == 4)
                    {
                        memory->setInt(threadHandle, address, registers[valueRegister] & 0xFFFFFFFFL);
                    }
                    else if (size == 8)
                    {
                        memory->setLong(threadHandle, address, registers[valueRegister]);
                    }
                    else
                    {
                        throw VMException("Unsupported size: " + size);
                    }
                    break;
                }
            case bytecode::LOAD_LOCAL:
                {
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t targetRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[bytecode::BP_REGISTER] - offset;
                    if (size == 1)
                    {
                        registers[targetRegister] = memory->getByte(threadHandle, address) & 0xFF;
                    }
                    else if (size == 2)
                    {
                        registers[targetRegister] = memory->getShort(threadHandle, address) & 0xFFFF;
                    }
                    else if (size == 4)
                    {
                        registers[targetRegister] = memory->getInt(threadHandle, address) & 0xFFFFFFFFL;
                    }
                    else if (size == 8)
                    {
                        registers[targetRegister] = memory->getLong(threadHandle, address);
                    }
                    else
                    {
                        throw VMException("Unsupported size: " + size);
                    }
                    break;
                }
            case bytecode::STORE_LOCAL:
                {
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t valueRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[bytecode::BP_REGISTER] - offset;
                    if (size == 1)
                    {
                        memory->setByte(threadHandle, address, registers[valueRegister] & 0xFF);
                    }
                    else if (size == 2)
                    {
                        memory->setShort(threadHandle, address, registers[valueRegister] & 0xFFFF);
                    }
                    else if (size == 4)
                    {
                        memory->setInt(threadHandle, address, registers[valueRegister] & 0xFFFFFFFFL);
                    }
                    else if (size == 8)
                    {
                        memory->setLong(threadHandle, address, registers[valueRegister]);
                    }
                    else
                    {
                        throw VMException("Unsupported size: " + size);
                    }
                    break;
                }
            case bytecode::LOAD_PARAMETER:
                {
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t targetRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[bytecode::BP_REGISTER] + offset;
                    if (size == 1)
                    {
                        registers[targetRegister] = memory->getByte(threadHandle, address) & 0xFF;
                    }
                    else if (size == 2)
                    {
                        registers[targetRegister] = memory->getShort(threadHandle, address) & 0xFFFF;
                    }
                    else if (size == 4)
                    {
                        registers[targetRegister] = memory->getInt(threadHandle, address) & 0xFFFFFFFFL;
                    }
                    else if (size == 8)
                    {
                        registers[targetRegister] = memory->getLong(threadHandle, address);
                    }
                    else
                    {
                        throw VMException("Unsupported size: " + size);
                    }
                    break;
                }
            case bytecode::STORE_PARAMETER:
                {
                    const uint8_t size = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t offset = memory->getLong(threadHandle, registers[bytecode::PC_REGISTER]);
                    registers[bytecode::PC_REGISTER] += 8;
                    const uint8_t valueRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[bytecode::BP_REGISTER] + offset;
                    if (size == 1)
                    {
                        memory->setByte(threadHandle, address, registers[valueRegister] & 0xFF);
                    }
                    else if (size == 2)
                    {
                        memory->setShort(threadHandle, address, registers[valueRegister] & 0xFFFF);
                    }
                    else if (size == 4)
                    {
                        memory->setInt(threadHandle, address, registers[valueRegister] & 0xFFFFFFFFL);
                    }
                    else if (size == 8)
                    {
                        memory->setLong(threadHandle, address, registers[valueRegister]);
                    }
                    else
                    {
                        throw VMException("Unsupported size: " + size);
                    }
                    break;
                }
            case bytecode::JUMP_IF_TRUE:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (registers[reg] != 0)
                    {
                        registers[bytecode::PC_REGISTER] = registers[target];
                    }
                    break;
                }
            case bytecode::JUMP_IF_FALSE:
                {
                    const uint8_t reg = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    if (registers[reg] == 0)
                    {
                        registers[bytecode::PC_REGISTER] = registers[target];
                    }
                    break;
                }
            case bytecode::SYSCALL:
                {
                    const uint8_t syscallRegister = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t syscallNumber = registers[syscallRegister];
                    break;
                }
            case bytecode::THREAD_FINISH:
                {
                    goto end;
                }
            case bytecode::NEG_DOUBLE:
                {
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[operand] = std::bit_cast<uint64_t>(-std::bit_cast<double>(registers[operand]));
                    break;
                }
            case bytecode::NEG_FLOAT:
                {
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    registers[operand] = std::bit_cast<uint32_t>(
                        -std::bit_cast<float>(static_cast<uint32_t>(registers[operand] & 0xFFFFFFFFL)));
                    break;
                }
            case bytecode::ATOMIC_NEG_DOUBLE:
                {
                    memory->lock();
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[operand];
                    const double tmp = -memory->getDouble(threadHandle, address);
                    memory->setDouble(threadHandle, address, tmp);
                    memory->unlock();
                    break;
                }
            case bytecode::ATOMIC_NEG_FLOAT:
                {
                    memory->lock();
                    const uint8_t operand = memory->getByte(threadHandle, registers[bytecode::PC_REGISTER]++);
                    const uint64_t address = registers[operand];
                    const float tmp = -memory->getFloat(threadHandle, address);
                    memory->setFloat(threadHandle, address, tmp);
                    memory->unlock();
                    break;
                }
            default:
                std::cout << "Unsupported opcode: " << code << std::endl;
            }
        }
    end:
        // std::cout << registers[bytecode::RETURN_VALUE_REGISTER] << std::endl;
        return;
    }

    void ExecutionUnit::interrupt(const uint8_t interruptNumber) const
    {
        Memory* memory = this->virtualMachine->memory;
        registers[bytecode::SP_REGISTER] -= 8;
        memory->setLong(threadHandle, registers[bytecode::SP_REGISTER], registers[bytecode::FLAGS_REGISTER]);
        registers[bytecode::SP_REGISTER] -= 8;
        memory->setLong(threadHandle, registers[bytecode::SP_REGISTER], registers[bytecode::PC_REGISTER]);
        const uint64_t idtEntry = registers[bytecode::IDTR_REGISTER] + interruptNumber * 8;
        registers[bytecode::PC_REGISTER] = memory->getLong(threadHandle, idtEntry);
    }

    void ExecutionUnit::destroy()
    {
        std::lock_guard lock(_mutex);
        delete[] registers;
        registers = nullptr;
    }


    FileHandle::FileHandle(std::string path, const uint32_t flags, const uint32_t mode, FILE* input,
                           FILE* output): path(std::move(path)), flags(flags | FH_PREOPEN), mode(mode),
                                          input(input), output(output)
    {
    }

    FileHandle::FileHandle(std::string path, uint32_t flags, uint32_t mode): path(std::move(path)), flags(flags),
                                                                             mode(mode)
    {
        if ((this->flags & FH_READ) != 0) this->input = fopen(path.c_str(), "rb");
        else this->input = nullptr;
        if ((this->flags & FH_WRITE) != 0) this->output = fopen(path.c_str(), "wb");
        else this->output = nullptr;
    }

    FileHandle::~FileHandle()
    {
        if (this->flags & FH_PREOPEN)return;
        if (this->input != nullptr) fclose(this->input);
        if (this->output != nullptr) fclose(this->output);
    }

    uint32_t FileHandle::_read(uint8_t* buffer, const uint32_t count) const
    {
        return fread(buffer, 1, count, this->input);
    }

    uint32_t FileHandle::_write(const uint8_t* buffer, const uint32_t count) const
    {
        fwrite(buffer, 1, count, this->output);
        return count;
    }
}
