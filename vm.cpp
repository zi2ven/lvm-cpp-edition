//
// Created by XiaoLi on 25-8-14.
//
#include <fstream>
#include <iostream>
#include <utility>
#include <cmath>
#include <ranges>

#include "bytecode.h"
#include "exception.h"
#include "module.h"
#include "vm.h"

#ifdef _MSC_VER
#define USE_SWITCH_DISPATCH
#endif

#ifdef USE_SWITCH_DISPATCH
#define TARGET(opcode) case (opcode)
#define DISPATCH(opcode) break
#else
#define TARGET(opcode) opcode
#define DISPATCH() goto end_dispatch
#define DISPATCH_TABLE_ENTRY(opcode) [opcode] = &&opcode
#endif


namespace lvm
{
    using namespace bytecode;

    VirtualMachine::VirtualMachine(uint64_t heapSize, uint64_t stackSize): stackSize(stackSize)
    {
        this->memory = new Memory(heapSize);
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

    inline uint64_t VirtualMachine::open(const char* path, uint32_t flags, uint32_t mode)
    {
        const uint64_t fd = getFd();
        fd2FileHandle[fd] = new FileHandle(path, flags, mode);
        return fd;
    }

    inline uint64_t VirtualMachine::close(uint64_t fd)
    {
        FileHandle* fileHandle = fd2FileHandle[fd];
        fd2FileHandle.erase(fd);
        if (fd <= lastFd) lastFd = fd - 1;
        delete fileHandle;
        return 0;
    }

    inline uint32_t VirtualMachine::read(const uint64_t fd, uint8_t* buffer, const uint32_t count)
    {
        FileHandle* fileHandle = fd2FileHandle[fd];
        if (fileHandle == nullptr)
        {
            throw VMException("Invalid file descriptor: " + fd);
        }
        return fileHandle->_read(buffer, count);
    }

    inline uint32_t VirtualMachine::write(const uint64_t fd, const uint8_t* buffer, const uint32_t count)
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
        this->registers = new uint64_t[REGISTER_COUNT]{};

        this->registers[BP_REGISTER] = stackBase;
        this->registers[SP_REGISTER] = stackBase;
        this->registers[PC_REGISTER] = entryPoint;
    }

    void ExecutionUnit::setThreadHandle(ThreadHandle* threadHandle)
    {
        this->threadHandle = threadHandle;
    }


    void ExecutionUnit::execute() const
    {
        ThreadHandle* threadHandle = this->threadHandle;
        Memory* memory = this->virtualMachine->memory;
        const auto base = reinterpret_cast<uint64_t>(memory->heap);
        uint64_t* registers = this->registers;
        // std::cout << registers[PC_REGISTER] << ": " << getInstructionName(
        // *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER])) << std::endl;
#ifdef USE_SWITCH_DISPATCH
        for (;;)
        {
            switch (const uint8_t code = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++))
            {
#else
        static void* dispatchTable[] = {
            DISPATCH_TABLE_ENTRY(NOP),DISPATCH_TABLE_ENTRY(PUSH_1),DISPATCH_TABLE_ENTRY(PUSH_2),
            DISPATCH_TABLE_ENTRY(PUSH_4),DISPATCH_TABLE_ENTRY(PUSH_8),DISPATCH_TABLE_ENTRY(POP_1),
            DISPATCH_TABLE_ENTRY(POP_2),DISPATCH_TABLE_ENTRY(POP_4),DISPATCH_TABLE_ENTRY(POP_8),
            DISPATCH_TABLE_ENTRY(LOAD_1),DISPATCH_TABLE_ENTRY(LOAD_2),DISPATCH_TABLE_ENTRY(LOAD_4),
            DISPATCH_TABLE_ENTRY(LOAD_8),DISPATCH_TABLE_ENTRY(STORE_1),DISPATCH_TABLE_ENTRY(STORE_2),
            DISPATCH_TABLE_ENTRY(STORE_4),DISPATCH_TABLE_ENTRY(STORE_8),DISPATCH_TABLE_ENTRY(CMP),
            DISPATCH_TABLE_ENTRY(ATOMIC_CMP),DISPATCH_TABLE_ENTRY(MOV_E),DISPATCH_TABLE_ENTRY(MOV_NE),
            DISPATCH_TABLE_ENTRY(MOV_L),DISPATCH_TABLE_ENTRY(MOV_LE),DISPATCH_TABLE_ENTRY(MOV_G),
            DISPATCH_TABLE_ENTRY(MOV_GE),DISPATCH_TABLE_ENTRY(MOV_UL),DISPATCH_TABLE_ENTRY(MOV_ULE),
            DISPATCH_TABLE_ENTRY(MOV_UG),DISPATCH_TABLE_ENTRY(MOV_UGE),DISPATCH_TABLE_ENTRY(MOV),
            DISPATCH_TABLE_ENTRY(MOV_IMMEDIATE1),DISPATCH_TABLE_ENTRY(MOV_IMMEDIATE2),
            DISPATCH_TABLE_ENTRY(MOV_IMMEDIATE4),DISPATCH_TABLE_ENTRY(MOV_IMMEDIATE8),DISPATCH_TABLE_ENTRY(JUMP),
            DISPATCH_TABLE_ENTRY(JUMP_IMMEDIATE),DISPATCH_TABLE_ENTRY(JE),DISPATCH_TABLE_ENTRY(JNE),
            DISPATCH_TABLE_ENTRY(JL),DISPATCH_TABLE_ENTRY(JLE),DISPATCH_TABLE_ENTRY(JG),DISPATCH_TABLE_ENTRY(JGE),
            DISPATCH_TABLE_ENTRY(JUL),DISPATCH_TABLE_ENTRY(JULE),DISPATCH_TABLE_ENTRY(JUG),
            DISPATCH_TABLE_ENTRY(JUGE),DISPATCH_TABLE_ENTRY(MALLOC),DISPATCH_TABLE_ENTRY(FREE),DISPATCH_TABLE_ENTRY(REALLOC),
            DISPATCH_TABLE_ENTRY(ADD),DISPATCH_TABLE_ENTRY(SUB),DISPATCH_TABLE_ENTRY(MUL),DISPATCH_TABLE_ENTRY(DIV),
            DISPATCH_TABLE_ENTRY(MOD),DISPATCH_TABLE_ENTRY(AND),DISPATCH_TABLE_ENTRY(OR),DISPATCH_TABLE_ENTRY(XOR),
            DISPATCH_TABLE_ENTRY(NOT), DISPATCH_TABLE_ENTRY(NEG),DISPATCH_TABLE_ENTRY(SHL),DISPATCH_TABLE_ENTRY(SHR),
            DISPATCH_TABLE_ENTRY(USHR),DISPATCH_TABLE_ENTRY(INC),DISPATCH_TABLE_ENTRY(DEC),
            DISPATCH_TABLE_ENTRY(ADD_DOUBLE), DISPATCH_TABLE_ENTRY(SUB_DOUBLE),DISPATCH_TABLE_ENTRY(MUL_DOUBLE),
            DISPATCH_TABLE_ENTRY(DIV_DOUBLE),DISPATCH_TABLE_ENTRY(MOD_DOUBLE),DISPATCH_TABLE_ENTRY(ADD_FLOAT),
            DISPATCH_TABLE_ENTRY(SUB_FLOAT),DISPATCH_TABLE_ENTRY(MUL_FLOAT),DISPATCH_TABLE_ENTRY(DIV_FLOAT),
            DISPATCH_TABLE_ENTRY(MOD_FLOAT),DISPATCH_TABLE_ENTRY(ATOMIC_ADD),DISPATCH_TABLE_ENTRY(ATOMIC_SUB),
            DISPATCH_TABLE_ENTRY(ATOMIC_MUL),DISPATCH_TABLE_ENTRY(ATOMIC_DIV),DISPATCH_TABLE_ENTRY(ATOMIC_MOD),
            DISPATCH_TABLE_ENTRY(ATOMIC_AND),DISPATCH_TABLE_ENTRY(ATOMIC_OR),DISPATCH_TABLE_ENTRY(ATOMIC_XOR),
            DISPATCH_TABLE_ENTRY(ATOMIC_NOT),DISPATCH_TABLE_ENTRY(ATOMIC_NEG),DISPATCH_TABLE_ENTRY(ATOMIC_SHL),
            DISPATCH_TABLE_ENTRY(ATOMIC_SHR),DISPATCH_TABLE_ENTRY(ATOMIC_USHR),DISPATCH_TABLE_ENTRY(ATOMIC_INC),
            DISPATCH_TABLE_ENTRY(ATOMIC_DEC),DISPATCH_TABLE_ENTRY(ATOMIC_ADD_DOUBLE),
            DISPATCH_TABLE_ENTRY(ATOMIC_SUB_DOUBLE),DISPATCH_TABLE_ENTRY(ATOMIC_MUL_DOUBLE),
            DISPATCH_TABLE_ENTRY(ATOMIC_DIV_DOUBLE),DISPATCH_TABLE_ENTRY(ATOMIC_MOD_DOUBLE),
            DISPATCH_TABLE_ENTRY(ATOMIC_ADD_FLOAT),DISPATCH_TABLE_ENTRY(ATOMIC_SUB_FLOAT),
            DISPATCH_TABLE_ENTRY(ATOMIC_MUL_FLOAT),DISPATCH_TABLE_ENTRY(ATOMIC_DIV_FLOAT),
            DISPATCH_TABLE_ENTRY(ATOMIC_MOD_FLOAT),DISPATCH_TABLE_ENTRY(CAS),DISPATCH_TABLE_ENTRY(INVOKE),
            DISPATCH_TABLE_ENTRY(INVOKE_IMMEDIATE),DISPATCH_TABLE_ENTRY(RETURN),DISPATCH_TABLE_ENTRY(INTERRUPT),
            DISPATCH_TABLE_ENTRY(INTERRUPT_RETURN),DISPATCH_TABLE_ENTRY(INT_TYPE_CAST),
            DISPATCH_TABLE_ENTRY(LONG_TO_DOUBLE),DISPATCH_TABLE_ENTRY(DOUBLE_TO_LONG),
            DISPATCH_TABLE_ENTRY(DOUBLE_TO_FLOAT),DISPATCH_TABLE_ENTRY(FLOAT_TO_DOUBLE),DISPATCH_TABLE_ENTRY(OPEN),
            DISPATCH_TABLE_ENTRY(CLOSE),DISPATCH_TABLE_ENTRY(READ),DISPATCH_TABLE_ENTRY(WRITE),
            DISPATCH_TABLE_ENTRY(CREATE_FRAME), DISPATCH_TABLE_ENTRY(DESTROY_FRAME), DISPATCH_TABLE_ENTRY(EXIT),
            DISPATCH_TABLE_ENTRY(EXIT_IMMEDIATE), DISPATCH_TABLE_ENTRY(GET_FIELD_ADDRESS),
            DISPATCH_TABLE_ENTRY(GET_LOCAL_ADDRESS),DISPATCH_TABLE_ENTRY(GET_PARAMETER_ADDRESS),
            DISPATCH_TABLE_ENTRY(CREATE_THREAD),DISPATCH_TABLE_ENTRY(THREAD_CONTROL),DISPATCH_TABLE_ENTRY(LOAD_FIELD),
            DISPATCH_TABLE_ENTRY(STORE_FIELD),DISPATCH_TABLE_ENTRY(LOAD_LOCAL),DISPATCH_TABLE_ENTRY(STORE_LOCAL),
            DISPATCH_TABLE_ENTRY(LOAD_PARAMETER),DISPATCH_TABLE_ENTRY(STORE_PARAMETER),
            DISPATCH_TABLE_ENTRY(JUMP_IF_TRUE), DISPATCH_TABLE_ENTRY(JUMP_IF_FALSE),DISPATCH_TABLE_ENTRY(SYSCALL),
            DISPATCH_TABLE_ENTRY(THREAD_FINISH), DISPATCH_TABLE_ENTRY(NEG_DOUBLE), DISPATCH_TABLE_ENTRY(NEG_FLOAT),
            DISPATCH_TABLE_ENTRY(ATOMIC_NEG_DOUBLE), DISPATCH_TABLE_ENTRY(ATOMIC_NEG_FLOAT), DISPATCH_TABLE_ENTRY(JIT_FOR_RANGE),
            DISPATCH_TABLE_ENTRY(INVOKE_NATIVE),
        };
        DISPATCH();
#endif
    TARGET(NOP):
        {
            {
            }
            DISPATCH();
        }
    TARGET(PUSH_1):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                --registers[SP_REGISTER];
                *reinterpret_cast<uint8_t*>(base + registers[SP_REGISTER]) = registers[reg];
            }
            DISPATCH();
        }
    TARGET(PUSH_2):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[SP_REGISTER] -= 2;
                *reinterpret_cast<uint16_t*>(base + registers[SP_REGISTER]) = registers[reg];
            }
            DISPATCH();
        }
    TARGET(PUSH_4):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[SP_REGISTER] -= 4;
                *reinterpret_cast<uint32_t*>(base + registers[SP_REGISTER]) = registers[reg];
            }
            DISPATCH();
        }
    TARGET(PUSH_8):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[SP_REGISTER] -= 8;
                *reinterpret_cast<uint64_t*>(base + registers[SP_REGISTER]) = registers[reg];
            }
            DISPATCH();
        }
    TARGET(POP_1):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[reg] = *reinterpret_cast<uint8_t*>(base + registers[SP_REGISTER]);
                ++registers[SP_REGISTER];
            }
            DISPATCH();
        }
    TARGET(POP_2):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[reg] = *reinterpret_cast<uint16_t*>(base + registers[SP_REGISTER]);
                registers[SP_REGISTER] += 2;
            }
            DISPATCH();
        }
    TARGET(POP_4):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[reg] = *reinterpret_cast<uint32_t*>(base + registers[SP_REGISTER]);
                registers[SP_REGISTER] += 4;
            }
            DISPATCH();
        }
    TARGET(POP_8):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[reg] = *reinterpret_cast<uint64_t*>(base + registers[SP_REGISTER]);
                registers[SP_REGISTER] += 8;
            }
            DISPATCH();
        }
    TARGET(LOAD_1):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = *reinterpret_cast<uint8_t*>(base + registers[address]);
            }
            DISPATCH();
        }
    TARGET(LOAD_2):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = *reinterpret_cast<uint16_t*>(base + registers[address]);
            }
            DISPATCH();
        }
    TARGET(LOAD_4):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = *reinterpret_cast<uint32_t*>(base + registers[address]);
            }
            DISPATCH();
        }
    TARGET(LOAD_8):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = *reinterpret_cast<uint64_t*>(base + registers[address]);
            }
            DISPATCH();
        }
    TARGET(STORE_1):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                *reinterpret_cast<uint8_t*>(base + registers[address]) = registers[source];
            }
            DISPATCH();
        }
    TARGET(STORE_2):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                *reinterpret_cast<uint16_t*>(base + registers[address]) = registers[source];
            }
            DISPATCH();
        }
    TARGET(STORE_4):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                *reinterpret_cast<uint32_t*>(base + registers[address]) = registers[source];
            }
            DISPATCH();
        }
    TARGET(STORE_8):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                *reinterpret_cast<uint64_t*>(base + registers[address]) = registers[source];
            }
            DISPATCH();
        }
    TARGET(CMP):
        {
            {
                const uint8_t type = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                auto value1 = static_cast<int64_t>(registers[operand1]);
                auto value2 = static_cast<int64_t>(registers[operand2]);
                uint64_t flags = registers[FLAGS_REGISTER];
                if (type == FLOAT_TYPE)
                {
                    const auto float1 = std::bit_cast<float>(static_cast<uint32_t>(value1 & 0xFFFFFFFFL));
                    const auto float2 = std::bit_cast<float>(static_cast<uint32_t>(value2 & 0xFFFFFFFFL));
                    const bool result = float1 < float2;
                    flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) | ((
                        result ? 0b11 : 0b00) << 1);
                }
                else if (type == DOUBLE_TYPE)
                {
                    const auto float1 = std::bit_cast<double>(value1);
                    const auto float2 = std::bit_cast<double>(value2);
                    const bool result = float1 < float2;
                    flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) | ((
                        result ? 0b11 : 0b00) << 1);
                }
                else
                {
                    if (type == BYTE_TYPE)
                    {
                        value1 = std::bit_cast<int8_t>(static_cast<uint8_t>(value1 & 0xff));
                        value2 = std::bit_cast<int8_t>(static_cast<uint8_t>(value2 & 0xff));
                    }
                    else if (type == SHORT_TYPE)
                    {
                        value1 = static_cast<int16_t>(value1 & 0xffff);
                        value2 = static_cast<int16_t>(value2 & 0xffff);
                    }
                    else if (type == INT_TYPE)
                    {
                        value1 = static_cast<int32_t>(value1 & 0xffffffffL);
                        value2 = static_cast<int32_t>(value2 & 0xffffffffL);
                    }
                    else if (type != LONG_TYPE)
                    {
                        throw VMException("Unsupported type");
                    }
                    if (value1 == value2)
                    {
                        flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) |
                            1;
                    }
                    else
                    {
                        bool signedResult = value1 < value2;
                        bool unsignedResult = std::bit_cast<uint64_t>(value1) < std::bit_cast<uint64_t>(value2);
                        flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) |
                            (signedResult ? CARRY_MASK : 0) | (unsignedResult
                                                                   ? UNSIGNED_MASK
                                                                   : 0);
                    }
                }
                registers[FLAGS_REGISTER] = flags;
            }
            DISPATCH();
        }
    TARGET(ATOMIC_CMP):
        {
            {
                memory->lock();
                const uint8_t type = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                auto value1 = static_cast<int64_t>(*reinterpret_cast<uint64_t*>(base + registers[operand1]));
                auto value2 = static_cast<int64_t>(registers[operand2]);
                uint64_t flags = registers[FLAGS_REGISTER];
                if (type == FLOAT_TYPE)
                {
                    const auto float1 = std::bit_cast<float>(static_cast<uint32_t>(value1 & 0xFFFFFFFFL));
                    const auto float2 = std::bit_cast<float>(static_cast<uint32_t>(value2 & 0xFFFFFFFFL));
                    const bool result = float1 < float2;
                    flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) | ((
                        result ? 0b11 : 0b00) << 1);
                }
                else if (type == DOUBLE_TYPE)
                {
                    const auto float1 = std::bit_cast<double>(value1);
                    const auto float2 = std::bit_cast<double>(value2);
                    const bool result = float1 < float2;
                    flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) | ((
                        result ? 0b11 : 0b00) << 1);
                }
                else
                {
                    if (type == BYTE_TYPE)
                    {
                        value1 = static_cast<int8_t>(value1 & 0xff);
                        value2 = static_cast<int8_t>(value2 & 0xff);
                    }
                    else if (type == SHORT_TYPE)
                    {
                        value1 = static_cast<int16_t>(value1 & 0xffff);
                        value2 = static_cast<int16_t>(value2 & 0xffff);
                    }
                    else if (type == INT_TYPE)
                    {
                        value1 = static_cast<int32_t>(value1 & 0xffffffffL);
                        value2 = static_cast<int32_t>(value2 & 0xffffffffL);
                    }
                    else if (type != LONG_TYPE)
                    {
                        throw VMException("Unsupported type");
                    }
                    if (value1 == value2)
                    {
                        flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) |
                            1;
                    }
                    else
                    {
                        bool signedResult = value1 < value2;
                        bool unsignedResult = std::bit_cast<uint64_t>(value1) < std::bit_cast<uint64_t>(value2);
                        flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) |
                            ((signedResult ? 1 : 0) << 1) | ((unsignedResult ? 1 : 0) << 2);
                    }
                }
                registers[FLAGS_REGISTER] = flags;
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(MOV_E):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if ((registers[FLAGS_REGISTER] & ZERO_MASK) != 0)
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_NE):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if ((registers[FLAGS_REGISTER] & ZERO_MASK) == 0)
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_L):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & CARRY_MASK) != 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_LE):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & CARRY_MASK) != 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_G):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & CARRY_MASK) == 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_GE):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & CARRY_MASK) == 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_UL):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & UNSIGNED_MASK) != 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_ULE):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & UNSIGNED_MASK) != 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_UG):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & UNSIGNED_MASK) == 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV_UGE):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & UNSIGNED_MASK) == 0))
                    registers[target] = registers[value];
            }
            DISPATCH();
        }
    TARGET(MOV):
        {
            {
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[source];
            }
            DISPATCH();
        }
    TARGET(MOV_IMMEDIATE1):
        {
            {
                const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = value;
            }
            DISPATCH();
        }
    TARGET(MOV_IMMEDIATE2):
        {
            {
                const uint16_t value = *reinterpret_cast<uint16_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 2;
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = value;
            }
            DISPATCH();
        }
    TARGET(MOV_IMMEDIATE4):
        {
            {
                const uint32_t value = *reinterpret_cast<uint32_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 4;
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = value;
            }
            DISPATCH();
        }
    TARGET(MOV_IMMEDIATE8):
        {
            {
                const uint64_t value = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = value;
            }
            DISPATCH();
        }
    TARGET(JUMP):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JUMP_IMMEDIATE):
        {
            {
                const uint64_t address = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] = address;
            }
            DISPATCH();
        }
    TARGET(JE):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if ((registers[FLAGS_REGISTER] & ZERO_MASK) != 0)
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JNE):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if ((registers[FLAGS_REGISTER] & ZERO_MASK) == 0)
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JL):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & CARRY_MASK) != 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JLE):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & CARRY_MASK) != 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JG):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & CARRY_MASK) == 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JGE):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & CARRY_MASK) == 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JUL):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & UNSIGNED_MASK) != 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JULE):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & UNSIGNED_MASK) != 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JUG):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) == 0)
                    && ((flags & UNSIGNED_MASK) == 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(JUGE):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (const uint64_t flags = registers[FLAGS_REGISTER]; ((flags & ZERO_MASK) != 0)
                    || ((flags & UNSIGNED_MASK) == 0))
                    registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(MALLOC):
        {
            {
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = memory->allocateMemory(threadHandle, registers[size]);
            }
            DISPATCH();
        }
    TARGET(FREE):
        {
            {
                const uint8_t ptr = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                memory->freeMemory(threadHandle, registers[ptr]);
            }
            DISPATCH();
        }
    TARGET(REALLOC):
        {
            {
                const uint8_t ptr = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = memory->reallocateMemory(threadHandle, registers[ptr], registers[size]);
            }
            DISPATCH();
        }
    TARGET(ADD):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] + registers[operand2];
            }
            DISPATCH();
        }
    TARGET(SUB):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] - registers[operand2];
            }
            DISPATCH();
        }
    TARGET(MUL):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] * registers[operand2];
            }
            DISPATCH();
        }
    TARGET(DIV):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] / registers[operand2];
            }
            DISPATCH();
        }
    TARGET(MOD):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] % registers[operand2];
            }
            DISPATCH();
        }
    TARGET(AND):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] & registers[operand2];
            }
            DISPATCH();
        }
    TARGET(OR):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] | registers[operand2];
            }
            DISPATCH();
        }
    TARGET(XOR):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] ^ registers[operand2];
            }
            DISPATCH();
        }
    TARGET(NOT):
        {
            {
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = ~registers[operand];
            }
            DISPATCH();
        }
    TARGET(NEG):
        {
            {
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(-static_cast<int64_t>(registers[operand]));
            }
            DISPATCH();
        }
    TARGET(SHL):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] << registers[operand2];
            }
            DISPATCH();
        }
    TARGET(SHR):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<int64_t>(registers[operand1]) >> registers[operand2];
            }
            DISPATCH();
        }
    TARGET(USHR):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] >> registers[operand2];
            }
            DISPATCH();
        }
    TARGET(INC):
        {
            {
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                ++registers[operand];
            }
            DISPATCH();
        }
    TARGET(DEC):
        {
            {
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                --registers[operand];
            }
            DISPATCH();
        }
    TARGET(ADD_DOUBLE):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) + std::bit_cast<double>(registers[operand2]));
            }
            DISPATCH();
        }
    TARGET(SUB_DOUBLE):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) - std::bit_cast<double>(registers[operand2]));
            }
            DISPATCH();
        }
    TARGET(MUL_DOUBLE):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) * std::bit_cast<double>(registers[operand2]));
            }
            DISPATCH();
        }
    TARGET(DIV_DOUBLE):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) / std::bit_cast<double>(registers[operand2]));
            }
            DISPATCH();
        }
    TARGET(MOD_DOUBLE):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(std::fmod(
                    std::bit_cast<double>(registers[operand1]), std::bit_cast<double>(registers[operand2])));
            }
            DISPATCH();
        }
    TARGET(ADD_FLOAT):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) +
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
            }
            DISPATCH();
        }
    TARGET(SUB_FLOAT):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) -
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
            }
            DISPATCH();
        }
    TARGET(MUL_FLOAT):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) *
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
            }
            DISPATCH();
        }
    TARGET(DIV_FLOAT):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) /
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
            }
            DISPATCH();
        }
    TARGET(MOD_FLOAT):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::fmod(std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)),
                              std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL)))));
            }
            DISPATCH();
        }
    TARGET(ATOMIC_ADD):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] + registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_SUB):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] - registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_MUL):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] * registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_DIV):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] / registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_MOD):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] % registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_AND):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] & registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_OR):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] | registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_XOR):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] ^ registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_NOT):
        {
            {
                memory->lock();
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = ~registers[operand];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_NEG):
        {
            {
                memory->lock();
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(-static_cast<int64_t>(registers[operand]));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_SHL):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] << registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_SHR):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<int64_t>(registers[operand1]) >> registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_USHR):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = registers[operand1] >> registers[operand2];
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_INC):
        {
            {
                memory->lock();
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t address = registers[operand];
                const uint64_t tmp = *reinterpret_cast<uint64_t*>(base + address) + 1;
                *reinterpret_cast<uint64_t*>(base + address) = tmp;
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_DEC):
        {
            {
                memory->lock();
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t address = registers[operand];
                const uint64_t tmp = *reinterpret_cast<uint64_t*>(base + address) - 1;
                *reinterpret_cast<uint64_t*>(base + address) = tmp;
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_ADD_DOUBLE):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) + std::bit_cast<double>(registers[operand2]));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_SUB_DOUBLE):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) - std::bit_cast<double>(registers[operand2]));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_MUL_DOUBLE):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) * std::bit_cast<double>(registers[operand2]));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_DIV_DOUBLE):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    std::bit_cast<double>(registers[operand1]) / std::bit_cast<double>(registers[operand2]));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_MOD_DOUBLE):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(std::fmod(
                    std::bit_cast<double>(registers[operand1]), std::bit_cast<double>(registers[operand2])));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_ADD_FLOAT):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) +
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_SUB_FLOAT):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) -
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_MUL_FLOAT):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) *
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_DIV_FLOAT):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)) /
                    std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL))));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_MOD_FLOAT):
        {
            {
                memory->lock();
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = static_cast<uint64_t>(std::bit_cast<uint32_t>(
                    std::fmod(std::bit_cast<float>(static_cast<uint32_t>(registers[operand1] & 0xffffffffL)),
                              std::bit_cast<float>(static_cast<uint32_t>(registers[operand2] & 0xffffffffL)))));
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(CAS):
        {
            {
                const uint8_t operand1 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand2 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t operand3 = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                uint64_t value1 = registers[operand1];
                uint64_t value2 = registers[operand2];
                uint64_t flags = registers[FLAGS_REGISTER];
                if (value1 == value2)
                {
                    flags = (flags & ~ZERO_MASK) | 1;
                    registers[operand1] = registers[operand3];
                }
                else
                {
                    bool signedResult = value1 < value2;
                    bool unsignedResult = std::bit_cast<uint64_t>(value1) < std::bit_cast<uint64_t>(value2);
                    flags = (flags & ~ZERO_MASK & ~CARRY_MASK & ~UNSIGNED_MASK) |
                        ((signedResult ? 1 : 0) << 1) | ((unsignedResult ? 1 : 0) << 2);
                    registers[operand2] = value1;
                }
                registers[FLAGS_REGISTER] = flags;
            }
            DISPATCH();
        }
    TARGET(INVOKE):
        {
            {
                const uint8_t address = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[SP_REGISTER] -= 8;
                *reinterpret_cast<uint64_t*>(base + registers[SP_REGISTER]) = registers[
                    PC_REGISTER];
                registers[PC_REGISTER] = registers[address];
            }
            DISPATCH();
        }
    TARGET(INVOKE_IMMEDIATE):
        {
            {
                const uint64_t address = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[SP_REGISTER] -= 8;
                *reinterpret_cast<uint64_t*>(base + registers[SP_REGISTER]) = (registers[
                        PC_REGISTER] +
                    8);
                registers[PC_REGISTER] = address;
            }
            DISPATCH();
        }
    TARGET(RETURN):
        {
            {
                registers[PC_REGISTER] = *reinterpret_cast<uint64_t*>(base + registers[
                    SP_REGISTER]);
                registers[SP_REGISTER] += 8;
            }
            DISPATCH();
        }
    TARGET(INTERRUPT):
        {
            {
                const uint8_t interruptNumber = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                this->interrupt(interruptNumber);
            }
            DISPATCH();
        }
    TARGET(INTERRUPT_RETURN):
        {
            {
                registers[PC_REGISTER] = *reinterpret_cast<uint64_t*>(base + registers[
                    SP_REGISTER]);
                registers[SP_REGISTER] += 8;
                registers[FLAGS_REGISTER] = *reinterpret_cast<uint64_t*>(base + registers[
                    SP_REGISTER]);
                registers[SP_REGISTER] += 8;
            }
            DISPATCH();
        }
    TARGET(INT_TYPE_CAST):
        {
            {
                const uint8_t types = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
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
            }
            DISPATCH();
        }
    TARGET(LONG_TO_DOUBLE):
        {
            {
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    static_cast<double>(static_cast<int64_t>(registers[source])));
            }
            DISPATCH();
        }
    TARGET(DOUBLE_TO_LONG):
        {
            {
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    static_cast<int64_t>(std::bit_cast<double>(registers[source])));
            }
            DISPATCH();
        }
    TARGET(DOUBLE_TO_FLOAT):
        {
            {
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint32_t>(
                    static_cast<float>(std::bit_cast<double>(registers[source])));
            }
            DISPATCH();
        }
    TARGET(FLOAT_TO_DOUBLE):
        {
            {
                const uint8_t source = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[target] = std::bit_cast<uint64_t>(
                    static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(registers[source]))));
            }
            DISPATCH();
        }
    TARGET(OPEN):
        {
            {
                const uint8_t pathRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t flagsRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint8_t modeRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t resultRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                uint64_t address = registers[pathRegister];
                std::string path;
                char c;
                while ((c = static_cast<char>(*reinterpret_cast<uint8_t*>(base + address++))) != '\0') path += c;
                registers[resultRegister] = virtualMachine->open(path.c_str(), flagsRegister, modeRegister);
            }
            DISPATCH();
        }
    TARGET(CLOSE):
        {
            {
                const uint8_t fdRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t resultRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                registers[resultRegister] = virtualMachine->close(registers[fdRegister]);
            }
            DISPATCH();
        }
    TARGET(READ):
        {
            {
                const uint8_t fdRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t bufferRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint8_t countRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint8_t resultRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                uint64_t bufferAddress = registers[bufferRegister];
                uint64_t count = registers[countRegister];
                uint32_t readCount = virtualMachine->read(registers[fdRegister],
                                                          reinterpret_cast<uint8_t*>(base + bufferAddress), count);
                registers[resultRegister] = readCount;
            }
            DISPATCH();
        }
    TARGET(WRITE):
        {
            {
                const uint8_t fdRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t bufferRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint8_t countRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint8_t resultRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                uint64_t address = registers[bufferRegister];
                uint64_t count = registers[countRegister];
                registers[resultRegister] = virtualMachine->write(registers[fdRegister],
                                                                  reinterpret_cast<uint8_t*>(base + address),
                                                                  count);
            }
            DISPATCH();
        }
    TARGET(CREATE_FRAME):
        {
            {
                const uint64_t size = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                registers[SP_REGISTER] -= 8;
                *reinterpret_cast<uint64_t*>(base + registers[SP_REGISTER]) = registers[
                    BP_REGISTER];
                registers[BP_REGISTER] = registers[SP_REGISTER];
                registers[SP_REGISTER] -= size;
            }
            DISPATCH();
        }
    TARGET(DESTROY_FRAME):
        {
            {
                const uint64_t size = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                registers[SP_REGISTER] += size;
                registers[BP_REGISTER] = *reinterpret_cast<uint64_t*>(base + registers[
                    SP_REGISTER]);
                registers[SP_REGISTER] += 8;
            }
            DISPATCH();
        }
    TARGET(EXIT):
        {
            {
                const uint8_t statusRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]);
                virtualMachine->exit(registers[statusRegister]);
            }
            goto end;
        }
    TARGET(EXIT_IMMEDIATE):
        {
            {
                const uint64_t status = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                virtualMachine->exit(status);
            }
            goto end;
        }
    TARGET(GET_FIELD_ADDRESS):
        {
            {
                const uint8_t objectRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t targetRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                registers[targetRegister] = registers[objectRegister] + offset;
            }
            DISPATCH();
        }
    TARGET(GET_LOCAL_ADDRESS):
        {
            {
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t targetRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                registers[targetRegister] = registers[BP_REGISTER] - offset;
            }
            DISPATCH();
        }
    TARGET(GET_PARAMETER_ADDRESS):
        {
            {
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t targetRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                registers[targetRegister] = registers[BP_REGISTER] + offset;
            }
            DISPATCH();
        }
    TARGET(CREATE_THREAD):
        {
            {
                const uint8_t entryPointRegister = *reinterpret_cast<uint8_t*>(base + registers[
                    PC_REGISTER]++);
                const uint8_t resultRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                registers[resultRegister] = virtualMachine->createThread(
                    threadHandle, registers[entryPointRegister]);
            }
            DISPATCH();
        }
    TARGET(THREAD_CONTROL):
        {
            {
                const uint8_t threadIDRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint8_t command = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                ThreadHandle* handle = virtualMachine->threadID2Handle[registers[threadIDRegister]];
                switch (command)
                {
                case TC_STOP:
                    {
                        break;
                    }
                case TC_WAIT:
                    {
                        break;
                    }
                case TC_GET_REGISTER:
                    {
                        const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                        const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                            ++);
                        registers[target] = handle->executionUnit->registers[reg];
                        break;
                    }
                case TC_SET_REGISTER:
                    {
                        const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                        const uint8_t value = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                            ++);
                        handle->executionUnit->registers[reg] = registers[value];
                        break;
                    }
                default:
                    {
                    }
                }
            }
            DISPATCH();
        }
    TARGET(LOAD_FIELD):
        {
            {
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t objectRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t targetRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t address = registers[objectRegister] + offset;
                if (size == 1)
                {
                    registers[targetRegister] = *reinterpret_cast<uint8_t*>(base + address) & 0xFF;
                }
                else if (size == 2)
                {
                    registers[targetRegister] = *reinterpret_cast<uint16_t*>(base + address) & 0xFFFF;
                }
                else if (size == 4)
                {
                    registers[targetRegister] = *reinterpret_cast<uint32_t*>(base + address) & 0xFFFFFFFFL;
                }
                else if (size == 8)
                {
                    registers[targetRegister] = *reinterpret_cast<uint64_t*>(base + address);
                }
                else
                {
                    throw VMException("Unsupported size: " + size);
                }
            }
            DISPATCH();
        }
    TARGET(STORE_FIELD):
        {
            {
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t objectRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t valueRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);

                const uint64_t address = registers[objectRegister] + offset;
                if (size == 1)
                {
                    *reinterpret_cast<uint8_t*>(base + address) = (registers[valueRegister] & 0xFF);
                }
                else if (size == 2)
                {
                    *reinterpret_cast<uint16_t*>(base + address) = (registers[valueRegister] & 0xFFFF);
                }
                else if (size == 4)
                {
                    *reinterpret_cast<uint32_t*>(base + address) = (registers[valueRegister] & 0xFFFFFFFFL);
                }
                else if (size == 8)
                {
                    *reinterpret_cast<uint64_t*>(base + address) = registers[valueRegister];
                }
                else
                {
                    throw VMException("Unsupported size: " + size);
                }
            }
            DISPATCH();
        }
    TARGET(LOAD_LOCAL):
        {
            {
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t targetRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t address = registers[BP_REGISTER] - offset;
                if (size == 1)
                {
                    registers[targetRegister] = *reinterpret_cast<uint8_t*>(base + address) & 0xFF;
                }
                else if (size == 2)
                {
                    registers[targetRegister] = *reinterpret_cast<uint16_t*>(base + address) & 0xFFFF;
                }
                else if (size == 4)
                {
                    registers[targetRegister] = *reinterpret_cast<uint32_t*>(base + address) & 0xFFFFFFFFL;
                }
                else if (size == 8)
                {
                    registers[targetRegister] = *reinterpret_cast<uint64_t*>(base + address);
                }
                else
                {
                    throw VMException("Unsupported size: " + size);
                }
            }
            DISPATCH();
        }
    TARGET(STORE_LOCAL):
        {
            {
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t valueRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t address = registers[BP_REGISTER] - offset;
                if (size == 1)
                {
                    *reinterpret_cast<uint8_t*>(base + address) = (registers[valueRegister] & 0xFF);
                }
                else if (size == 2)
                {
                    *reinterpret_cast<uint16_t*>(base + address) = (registers[valueRegister] & 0xFFFF);
                }
                else if (size == 4)
                {
                    *reinterpret_cast<uint32_t*>(base + address) = (registers[valueRegister] & 0xFFFFFFFFL);
                }
                else if (size == 8)
                {
                    *reinterpret_cast<uint64_t*>(base + address) = registers[valueRegister];
                }
                else
                {
                    throw VMException("Unsupported size: " + size);
                }
            }
            DISPATCH();
        }
    TARGET(LOAD_PARAMETER):
        {
            {
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t targetRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t address = registers[BP_REGISTER] + offset;
                if (size == 1)
                {
                    registers[targetRegister] = *reinterpret_cast<uint8_t*>(base + address) & 0xFF;
                }
                else if (size == 2)
                {
                    registers[targetRegister] = *reinterpret_cast<uint16_t*>(base + address) & 0xFFFF;
                }
                else if (size == 4)
                {
                    registers[targetRegister] = *reinterpret_cast<uint32_t*>(base + address) & 0xFFFFFFFFL;
                }
                else if (size == 8)
                {
                    registers[targetRegister] = *reinterpret_cast<uint64_t*>(base + address);
                }
                else
                {
                    throw VMException("Unsupported size: " + size);
                }
            }
            DISPATCH();
        }
    TARGET(STORE_PARAMETER):
        {
            {
                const uint8_t size = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t offset = *reinterpret_cast<uint64_t*>(base + registers[PC_REGISTER]);
                registers[PC_REGISTER] += 8;
                const uint8_t valueRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t address = registers[BP_REGISTER] + offset;
                if (size == 1)
                {
                    *reinterpret_cast<uint8_t*>(base + address) = (registers[valueRegister] & 0xFF);
                }
                else if (size == 2)
                {
                    *reinterpret_cast<uint16_t*>(base + address) = (registers[valueRegister] & 0xFFFF);
                }
                else if (size == 4)
                {
                    *reinterpret_cast<uint32_t*>(base + address) = (registers[valueRegister] & 0xFFFFFFFFL);
                }
                else if (size == 8)
                {
                    *reinterpret_cast<uint64_t*>(base + address) = registers[valueRegister];
                }
                else
                {
                    throw VMException("Unsupported size: " + size);
                }
            }
            DISPATCH();
        }
    TARGET(JUMP_IF_TRUE):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (registers[reg] != 0)
                {
                    registers[PC_REGISTER] = registers[target];
                }
            }
            DISPATCH();
        }
    TARGET(JUMP_IF_FALSE):
        {
            {
                const uint8_t reg = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint8_t target = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                if (registers[reg] == 0)
                {
                    registers[PC_REGISTER] = registers[target];
                }
            }
            DISPATCH();
        }
    TARGET(SYSCALL):
        {
            {
                const uint8_t syscallRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]
                    ++);
                const uint64_t syscallNumber = registers[syscallRegister];
            }
            DISPATCH();
        }
    TARGET(THREAD_FINISH):
        {
            goto end;
        }
    TARGET(NEG_DOUBLE):
        {
            {
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[operand] = std::bit_cast<uint64_t>(-std::bit_cast<double>(registers[operand]));
            }
            DISPATCH();
        }
    TARGET(NEG_FLOAT):
        {
            {
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                registers[operand] = std::bit_cast<uint32_t>(
                    -std::bit_cast<float>(static_cast<uint32_t>(registers[operand] & 0xFFFFFFFFL)));
            }
            DISPATCH();
        }
    TARGET(ATOMIC_NEG_DOUBLE):
        {
            {
                memory->lock();
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t address = registers[operand];
                const double tmp = -*reinterpret_cast<double*>(base + address);
                *reinterpret_cast<double*>(base + address) = tmp;
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(ATOMIC_NEG_FLOAT):
        {
            {
                memory->lock();
                const uint8_t operand = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                const uint64_t address = registers[operand];
                const float tmp = -*reinterpret_cast<float*>(base + address);
                *reinterpret_cast<float*>(base + address) = tmp;
                memory->unlock();
            }
            DISPATCH();
        }
    TARGET(JIT_FOR_RANGE):
        {
            {
                // TODO
                uint8_t startRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                uint8_t lengthRegister = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
            }
            DISPATCH();
        }
    TARGET(INVOKE_NATIVE):
        {
            {
                // Currently only native function calls with no arguments and no return values are supported
                uint8_t ptr = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
                reinterpret_cast<void(*)()>(registers[ptr])();
            }
            DISPATCH();
        }
#ifdef USE_SWITCH_DISPATCH
            default:
                std::cout << "Unsupported opcode: " << code << std::endl;
            }
        }
#else
    end_dispatch:
        {
            const uint8_t code = *reinterpret_cast<uint8_t*>(base + registers[PC_REGISTER]++);
            goto *dispatchTable[code];
        }
#endif

    end:
        // std::cout << registers[RETURN_VALUE_REGISTER] << std::endl;
        return;
    }

    void ExecutionUnit::interrupt(const uint8_t interruptNumber) const
    {
        Memory* memory = this->virtualMachine->memory;
        const uint64_t base = reinterpret_cast<uint64_t>(memory->heap);
        registers[SP_REGISTER] -= 8;
        *reinterpret_cast<uint64_t*>(base + registers[SP_REGISTER]) = registers[FLAGS_REGISTER];
        registers[SP_REGISTER] -= 8;
        *reinterpret_cast<uint64_t*>(base + registers[SP_REGISTER]) = registers[PC_REGISTER];
        const uint64_t idtEntry = registers[IDTR_REGISTER] + interruptNumber * 8;
        registers[PC_REGISTER] = *reinterpret_cast<uint64_t*>(base + idtEntry);
    }

    void ExecutionUnit::destroy()
    {
        std::lock_guard lock(_mutex);
        delete[] registers;
        registers = nullptr;
    }


    FileHandle::FileHandle(std::string path, const uint32_t flags, const uint32_t mode, FILE* input,
                           FILE* output)
        :
        path(std::move(path)), flags(flags | FH_PREOPEN), mode(mode),
        input(input), output(output)
    {
    }

    FileHandle::FileHandle(std::string path, uint32_t flags, uint32_t mode)
        :
        path(std::move(path)), flags(flags),
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

    inline uint32_t FileHandle::_read(uint8_t* buffer, const uint32_t count) const
    {
        return fread(buffer, 1, count, this->input);
    }

    inline uint32_t FileHandle::_write(const uint8_t* buffer, const uint32_t count) const
    {
        fwrite(buffer, 1, count, this->output);
        return count;
    }
}
