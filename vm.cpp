//
// Created by XiaoLi on 25-8-14.
//
#include "vm.h"

#include <fstream>
#include <iostream>
#include <utility>

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

        this->fd2FileHandle.insert(std::make_pair(0, new FileHandle("stdin", 0, 0, &std::cin, nullptr)));
        this->fd2FileHandle.insert(std::make_pair(1, new FileHandle("stdout", 0, 0, nullptr, &std::cout)));
        this->fd2FileHandle.insert(std::make_pair(2, new FileHandle("stderr", 0, 0, nullptr, &std::cerr)));

        this->lastFd = 2;

        return 0;
    }

    int VirtualMachine::run()
    {
        this->createThread(this->entryPoint);
        return 0;
    }

    uint64_t VirtualMachine::createThread(uint64_t entryPoint)
    {
        uint64_t threadID = this->getThreadID();
        ExecutionUnit* executionUnit = this->createExecutionUnit(threadID, entryPoint);
        auto* threadHandle = new ThreadHandle(executionUnit);
        this->threadID2Handle.insert(std::make_pair(threadID, threadHandle));
        return threadID;
    }

    ExecutionUnit* VirtualMachine::createExecutionUnit(uint64_t threadID, uint64_t entryPoint)
    {
        auto* executionUnit = new ExecutionUnit(this);
        uint64_t stack = this->memory->allocateMemory(this->stackSize);
        executionUnit->init(threadID, stack + this->stackSize - 1, entryPoint);
        return executionUnit;
    }

    uint64_t VirtualMachine::getThreadID()
    {
        this->_mutex.lock();
        uint64_t threadID = this->lastThreadID + 1;
        while (this->threadID2Handle.contains(threadID)) ++threadID;
        this->lastThreadID = threadID;
        this->_mutex.unlock();
        return threadID;
    }

    uint64_t VirtualMachine::getFd()
    {
        this->_mutex.lock();
        uint64_t fd = this->lastFd + 1;
        while (this->fd2FileHandle.contains(fd)) ++fd;
        this->lastFd = fd;
        this->_mutex.unlock();
        return fd;
    }


    ThreadHandle::ThreadHandle(ExecutionUnit* executionUnit): executionUnit(executionUnit)
    {
    }

    ExecutionUnit::ExecutionUnit(VirtualMachine* virtualMachine): virtualMachine(virtualMachine)
    {
    }

    void ExecutionUnit::init(uint64_t threadID, uint64_t stackBase, uint64_t entryPoint)
    {
        this->threadID = threadID;
        this->registers = new uint64_t[bytecode::REGISTER_COUNT];

        this->registers[bytecode::BP_REGISTER] = stackBase;
        this->registers[bytecode::SP_REGISTER] = stackBase;
        this->registers[bytecode::PC_REGISTER] = entryPoint;
    }

    void ExecutionUnit::execute()
    {
        Memory* memory = this->virtualMachine->memory;
        for (;;)
        {
            switch (const uint8_t code = this->virtualMachine->memory->getByte(this->registers[bytecode::PC_REGISTER]))
            {
            case bytecode::NOP:
                {
                    break;
                }
            case bytecode::PUSH_1:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    --registers[bytecode::SP_REGISTER];
                    memory->setByte(registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::PUSH_2:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::SP_REGISTER] -= 2;
                    memory->setShort(registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::PUSH_4:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::SP_REGISTER] -= 4;
                    memory->setInt(registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::PUSH_8:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[bytecode::SP_REGISTER] -= 8;
                    memory->setLong(registers[bytecode::SP_REGISTER], registers[reg]);
                    break;
                }
            case bytecode::POP_1:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getByte(registers[bytecode::SP_REGISTER]);
                    ++registers[bytecode::SP_REGISTER];
                    break;
                }
            case bytecode::POP_2:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getShort(registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 2;
                    break;
                }
            case bytecode::POP_4:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getInt(registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 4;
                    break;
                }
            case bytecode::POP_8:
                {
                    const uint8_t reg = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[reg] = memory->getLong(registers[bytecode::SP_REGISTER]);
                    registers[bytecode::SP_REGISTER] += 8;
                    break;
                }
            case bytecode::LOAD_1:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getByte(address);
                    break;
                }
            case bytecode::LOAD_2:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getShort(address);
                    break;
                }
            case bytecode::LOAD_4:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getInt(address);
                    break;
                }
            case bytecode::LOAD_8:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    registers[target] = memory->getLong(address);
                    break;
                }
            case bytecode::STORE_1:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    memory->setByte(address, registers[source]);
                    break;
                }
            case bytecode::STORE_2:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    memory->setShort(address, registers[source]);
                    break;
                }
            case bytecode::STORE_4:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    memory->setInt(address, registers[source]);
                    break;
                }
            case bytecode::STORE_8:
                {
                    const uint8_t address = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t source = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    memory->setLong(address, registers[source]);
                    break;
                }
            case bytecode::CMP:
                {
                    const uint8_t type = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand1 = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(registers[bytecode::PC_REGISTER]++);
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
                    break;
                }
            case bytecode::ATOMIC_CMP:
                {
                    memory->lock();
                    const uint8_t type = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand1 = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t operand2 = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    auto value1 = static_cast<int64_t>(memory->getLong(registers[operand1]));
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
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if ((registers[bytecode::FLAGS_REGISTER] & bytecode::ZERO_MASK) != 0)
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_NE:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if ((registers[bytecode::FLAGS_REGISTER] & bytecode::ZERO_MASK) == 0)
                        registers[target] = registers[value];
                    break;
                }
            case bytecode::MOV_L:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::CARRY_MASK) != 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            case bytecode::MOV_LE:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::CARRY_MASK) != 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            case bytecode::MOV_G:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::CARRY_MASK) == 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            case bytecode::MOV_GE:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::CARRY_MASK) == 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            case bytecode::MOV_UL:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::UNSIGNED_MASK) != 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            case bytecode::MOV_ULE:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::UNSIGNED_MASK) != 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            case bytecode::MOV_UG:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) == 0)
                        && ((flags & bytecode::UNSIGNED_MASK) == 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            case bytecode::MOV_UGE:
                {
                    const uint8_t value = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    const uint8_t target = memory->getByte(registers[bytecode::PC_REGISTER]++);
                    if (const uint64_t flags = registers[bytecode::FLAGS_REGISTER]; ((flags & bytecode::ZERO_MASK) != 0)
                        || ((flags & bytecode::UNSIGNED_MASK) == 0))
                        registers[target] = memory->getLong(registers[value]);
                    break;
                }
            default:
                std::cout << "Unsupported opcode: " << code << std::endl;
            }
            goto end;
        }
    end:
        return;
    }


    FileHandle::FileHandle(std::string path, uint32_t flags, uint32_t mode, const std::istream* inputStream,
                           const std::ostream* outputStream): path(std::move(path)), flags(flags), mode(mode),
                                                              inputStream(inputStream), outputStream(outputStream)
    {
    }

    FileHandle::FileHandle(std::string path, uint32_t flags, uint32_t mode): path(std::move(path)), flags(flags),
                                                                             mode(mode)
    {
        this->inputStream = new std::ifstream(path);
        this->outputStream = new std::ofstream(path);
    }
}
