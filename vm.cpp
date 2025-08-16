//
// Created by XiaoLi on 25-8-14.
//
#include "vm.h"

#include <fstream>
#include <iostream>
#include <utility>

#include "bytecode.h"
#include "module.h"

namespace lvm
{
    VirtualMachine::VirtualMachine(uint64_t stackSize): stackSize(stackSize)
    {
        this->memory = new Memory();
    }

    int VirtualMachine::init(const Module* module)
    {
        this->memory->init(module->text, module->rodata, module->data, module->bssLength);
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
        return 0;
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
        for (;;)
        {
            switch (uint8_t code = this->virtualMachine->memory->getByte(this->registers[bytecode::PC_REGISTER]))
            {
            case bytecode::NOP:
                {
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
