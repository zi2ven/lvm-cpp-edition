//
// Created by XiaoLi on 25-8-17.
//
#include "bytecode.h"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>

#define LVM_OPCODE_LIST(OP)                     \
  OP(NOP)                                       \
  OP(PUSH_1) OP(PUSH_2) OP(PUSH_4) OP(PUSH_8)   \
  OP(POP_1)  OP(POP_2)  OP(POP_4)  OP(POP_8)    \
  OP(LOAD_1) OP(LOAD_2) OP(LOAD_4) OP(LOAD_8)   \
  OP(STORE_1) OP(STORE_2) OP(STORE_4) OP(STORE_8) \
  OP(CMP) OP(ATOMIC_CMP)                        \
  OP(MOV_E) OP(MOV_NE) OP(MOV_L) OP(MOV_LE)     \
  OP(MOV_G) OP(MOV_GE) OP(MOV_UL) OP(MOV_ULE)   \
  OP(MOV_UG) OP(MOV_UGE) OP(MOV)                \
  OP(MOV_IMMEDIATE1) OP(MOV_IMMEDIATE2) OP(MOV_IMMEDIATE4) OP(MOV_IMMEDIATE8) \
  OP(JUMP) OP(JUMP_IMMEDIATE)                   \
  OP(JE) OP(JNE) OP(JL) OP(JLE) OP(JG) OP(JGE)  \
  OP(JUL) OP(JULE) OP(JUG) OP(JUGE)             \
  OP(MALLOC) OP(FREE) OP(REALLOC)               \
  OP(ADD) OP(SUB) OP(MUL) OP(DIV) OP(MOD)       \
  OP(AND) OP(OR) OP(XOR) OP(NOT) OP(NEG)        \
  OP(SHL) OP(SHR) OP(USHR)                      \
  OP(INC) OP(DEC)                               \
  OP(ADD_DOUBLE) OP(SUB_DOUBLE) OP(MUL_DOUBLE) OP(DIV_DOUBLE) OP(MOD_DOUBLE) \
  OP(ADD_FLOAT)  OP(SUB_FLOAT)  OP(MUL_FLOAT)  OP(DIV_FLOAT)  OP(MOD_FLOAT)  \
  OP(ATOMIC_ADD) OP(ATOMIC_SUB) OP(ATOMIC_MUL) OP(ATOMIC_DIV) OP(ATOMIC_MOD) \
  OP(ATOMIC_AND) OP(ATOMIC_OR)  OP(ATOMIC_XOR)  \
  OP(ATOMIC_NOT) OP(ATOMIC_NEG)                 \
  OP(ATOMIC_SHL) OP(ATOMIC_SHR) OP(ATOMIC_USHR) \
  OP(ATOMIC_INC) OP(ATOMIC_DEC)                 \
  OP(ATOMIC_ADD_DOUBLE) OP(ATOMIC_SUB_DOUBLE) OP(ATOMIC_MUL_DOUBLE) \
  OP(ATOMIC_DIV_DOUBLE) OP(ATOMIC_MOD_DOUBLE)   \
  OP(ATOMIC_ADD_FLOAT) OP(ATOMIC_SUB_FLOAT) OP(ATOMIC_MUL_FLOAT) \
  OP(ATOMIC_DIV_FLOAT) OP(ATOMIC_MOD_FLOAT)     \
  OP(CAS) OP(INVOKE) OP(INVOKE_IMMEDIATE) OP(RETURN) \
  OP(INTERRUPT) OP(INTERRUPT_RETURN)            \
  OP(INT_TYPE_CAST) OP(LONG_TO_DOUBLE) OP(DOUBLE_TO_LONG) \
  OP(DOUBLE_TO_FLOAT) OP(FLOAT_TO_DOUBLE)       \
  OP(OPEN) OP(CLOSE) OP(READ) OP(WRITE)         \
  OP(CREATE_FRAME) OP(DESTROY_FRAME)            \
  OP(EXIT) OP(EXIT_IMMEDIATE)                   \
  OP(GET_FIELD_ADDRESS) OP(GET_LOCAL_ADDRESS) OP(GET_PARAMETER_ADDRESS) \
  OP(CREATE_THREAD) OP(THREAD_CONTROL)          \
  OP(LOAD_FIELD) OP(STORE_FIELD) OP(LOAD_LOCAL) OP(STORE_LOCAL) \
  OP(LOAD_PARAMETER) OP(STORE_PARAMETER)        \
  OP(JUMP_IF_TRUE) OP(JUMP_IF_FALSE) OP(SYSCALL) OP(THREAD_FINISH) \
  OP(NEG_DOUBLE) OP(NEG_FLOAT) OP(ATOMIC_NEG_DOUBLE) OP(ATOMIC_NEG_FLOAT) \
  OP(JUMP_IF) OP(INVOKE_NATIVE)

namespace lvm::bytecode
{
    [[nodiscard]] std::string_view getInstructionName(const uint8_t code)
    {
        static constexpr std::array<std::string_view,
#define COUNT_1(x) 1 +
                                    (LVM_OPCODE_LIST(COUNT_1) 0)
#undef COUNT_1
        > kNames = {
#define NAME_ITEM(op) #op,
            LVM_OPCODE_LIST(NAME_ITEM)
#undef NAME_ITEM
        };

        return (code < kNames.size()) ? kNames[code] : std::string_view{"UNKNOWN"};
    }

    [[nodiscard]] uint8_t parseInstructionCode(const std::string& code)
    {
        std::string up(code);
        std::ranges::transform(up, up.begin(), [](auto c)
        {
            return static_cast<char>(std::toupper(c));
        });

#define CHECK_ITEM(op)  if (up == #op) return static_cast<uint8_t>(op);
        LVM_OPCODE_LIST(CHECK_ITEM)
#undef CHECK_ITEM

        throw std::runtime_error("Unknown instruction: " + code);
    }

#undef LVM_OPCODE_LIST
}
