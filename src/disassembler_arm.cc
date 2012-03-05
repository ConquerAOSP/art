/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "disassembler_arm.h"

#include "stringprintf.h"

#include <iostream>

namespace art {
namespace arm {

DisassemblerArm::DisassemblerArm() {
}


void DisassemblerArm::Dump(std::ostream& os, const uint8_t* begin, const uint8_t* end) {
  if ((reinterpret_cast<intptr_t>(begin) & 1) == 0) {
    for (const uint8_t* cur = begin; cur < end; cur += 4) {
      DumpArm(os, cur);
    }
  } else {
    // remove thumb specifier bits
    begin = reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(begin) & ~1);
    end = reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(end) & ~1);
    for (const uint8_t* cur = begin; cur < end;) {
      cur += DumpThumb16(os, cur);
    }
  }
}

static const char* ConditionCodeNames[] = {
    "EQ",  // 0000 - equal
    "NE",  // 0001 - not-equal
    "CS",  // 0010 - carry-set, greater than, equal or unordered
    "CC",  // 0011 - carry-clear, less than
    "MI",  // 0100 - minus, negative
    "PL",  // 0101 - plus, positive or zero
    "VS",  // 0110 - overflow
    "VC",  // 0111 - no overflow
    "HI",  // 1000 - unsigned higher
    "LS",  // 1001 - unsigned lower or same
    "GE",  // 1010 - signed greater than or equal
    "LT",  // 1011 - signed less than
    "GT",  // 1100 - signed greater than
    "LE",  // 1101 - signed less than or equal
    "AL",  // 1110 - always
};

void DisassemblerArm::DumpCond(std::ostream& os, uint32_t cond) {
  if (cond < 15) {
    os << ConditionCodeNames[cond];
  } else {
    os << "Unexpected condition: " << cond;
  }
}

void DisassemblerArm::DumpReg(std::ostream& os, uint32_t reg) {
  switch (reg) {
    case 13: os << "SP"; break;
    case 14: os << "LR"; break;
    case 15: os << "PC"; break;
    default: os << "r" << reg; break;
  }
}

void DisassemblerArm::DumpRegList(std::ostream& os, uint32_t reg_list) {
  if (reg_list == 0) {
    os << "<no register list?>";
    return;
  }
  bool first = true;
  for (size_t i = 0; i < 16; i++) {
    if ((reg_list & (1 << i)) != 0) {
      if (first) {
        os << "{";
        first = false;
      } else {
        os << ", ";
      }
      DumpReg(os, i);
    }
  }
  os << "}";
}

void DisassemblerArm::DumpBranchTarget(std::ostream& os, const uint8_t* instr_ptr, int32_t imm32) {
  os << imm32 << " (" << reinterpret_cast<const void*>(instr_ptr + imm32) << ")";
}

static uint32_t ReadU16(const uint8_t* ptr) {
  return ptr[0] | (ptr[1] << 8);
}

static uint32_t ReadU32(const uint8_t* ptr) {
  return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}


void DisassemblerArm::DumpArm(std::ostream& os, const uint8_t* instr_ptr) {
  os << StringPrintf("\t\t\t%p: %08x\n", instr_ptr, ReadU32(instr_ptr));
}

size_t DisassemblerArm::DumpThumb32(std::ostream& os, const uint8_t* instr_ptr) {
  uint32_t instr = (ReadU16(instr_ptr) << 16) | ReadU16(instr_ptr + 2);
  // |111|1 1|1000000|0000|1111110000000000|
  // |5 3|2 1|0987654|3  0|5    0    5    0|
  // |---|---|-------|----|----------------|
  // |332|2 2|2222222|1111|1111110000000000|
  // |1 9|8 7|6543210|9  6|5    0    5    0|
  // |---|---|-------|----|----------------|
  // |111|op1| op2   |    |                |
  uint32_t op1 = (instr >> 27) & 3;
  uint32_t op2 = (instr >> 20) & 0x7F;
  os << StringPrintf("\t\t\t%p: ", instr_ptr);
  switch (op1) {
    case 0:
      return DumpThumb16(os, instr_ptr);
      break;
    case 1:
      switch (op2) {
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x08: case 0x09: case 0x0A: case 0x0B:
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x18: case 0x19: case 0x1A: case 0x1B: {
          // |111|11|10|00|0|00|0000|1111110000000000|
          // |5 3|21|09|87|6|54|3  0|5    0    5    0|
          // |---|--|--|--|-|--|----|----------------|
          // |332|22|22|22|2|22|1111|1111110000000000|
          // |1 9|87|65|43|2|10|9  6|5    0    5    0|
          // |---|--|--|--|-|--|----|----------------|
          // |111|01|00|op|0|WL| Rn |                |
          // |111|01| op2      |    |                |
          // STM - 111 01 00-01-0-W0 nnnn rrrrrrrrrrrrrrrr
          // LDM - 111 01 00-01-0-W1 nnnn rrrrrrrrrrrrrrrr
          // PUSH- 111 01 00-01-0-10 1101 0M0rrrrrrrrrrrrr
          // POP - 111 01 00-01-0-11 1101 PM0rrrrrrrrrrrrr
          uint32_t op = (instr >> 23) & 3;
          uint32_t W = (instr >> 21) & 1;
          uint32_t L = (instr >> 20) & 1;
          uint32_t Rn = (instr >> 16) & 0xF;
          uint32_t reg_list = instr & 0xFFFF;
          if (op == 1 || op == 2) {
            if (op == 1) {
              if (L == 0) {
                os << "STM ";
                DumpReg(os, Rn);
                if (W == 0) {
                  os << ", ";
                } else {
                  os << "!, ";
                }
              } else {
                if (Rn != 13) {
                  os << "LDM ";
                  DumpReg(os, Rn);
                  if (W == 0) {
                    os << ", ";
                  } else {
                    os << "!, ";
                  }
                } else {
                  os << "POP ";
                }
              }
            } else {
              if (L == 0) {
                if (Rn != 13) {
                  os << "STMDB ";
                  DumpReg(os, Rn);
                  if (W == 0) {
                    os << ", ";
                  } else {
                    os << "!, ";
                  }
                } else {
                  os << "PUSH ";
                }
              } else {
                os << "LDMDB ";
                DumpReg(os, Rn);
                if (W == 0) {
                  os << ", ";
                } else {
                  os << "!, ";
                }
              }
            }
            DumpRegList(os, reg_list);
            os << "  // ";
          }
          break;
        }
        default:
          break;
      }
      break;
    case 2:
      if ((instr & 0x8000) == 0 && (op2 & 0x20) == 0) {
        // Data-processing (modified immediate)
        // |111|11|10|0000|0|0000|1|111|1100|00000000|
        // |5 3|21|09|8765|4|3  0|5|4 2|10 8|7 5    0|
        // |---|--|--|----|-|----|-|---|----|--------|
        // |332|22|22|2222|2|1111|1|111|1100|00000000|
        // |1 9|87|65|4321|0|9  6|5|4 2|10 8|7 5    0|
        // |---|--|--|----|-|----|-|---|----|--------|
        // |111|10|i0| op3|S| Rn |0|iii| Rd |iiiiiiii|
        //  111 10 x0 xxxx x xxxx opxxx xxxx xxxxxxxx
        //  111 10 00 0110 0 0000 1 000 0000 10101101 - f0c080ad
        uint32_t i = (instr >> 26) & 1;
        uint32_t op3 = (instr >> 21) & 0xF;
        uint32_t S = (instr >> 20) & 1;
        uint32_t Rn = (instr >> 16) & 0xF;
        uint32_t imm3 = (instr >> 12) & 7;
        uint32_t Rd = (instr >> 8) & 0xF;
        uint32_t imm8 = instr & 0xFF;
        int32_t imm32 = (i << 12) | (imm3 << 8) | imm8;
        switch (op3) {
          case 0x0: os << "AND"; break;
          case 0x1: os << "BIC"; break;
          case 0x2: os << "ORR"; break;
          case 0x3: os << "ORN"; break;
          case 0x4: os << "EOR"; break;
          case 0x8: os << "ADD"; break;
          case 0xA: os << "ADC"; break;
          case 0xB: os << "SBC"; break;
          case 0xD: os << "SUB"; break;
          case 0xE: os << "RSB"; break;
          default: os << "UNKNOWN DPMI-" << op3; break;
        }
        if (S == 1) {
          os << "S ";
        } else {
          os << " ";
        }
        DumpReg(os, Rd);
        os << ", ";
        DumpReg(os, Rn);
        os << ", ThumbExpand(" << imm32 << ")  // ";
      } else if ((instr & 0x8000) == 0 && (op2 & 0x20) != 0) {
        // Data-processing (plain binary immediate)
        // |111|11|10|00000|0000|1|111110000000000|
        // |5 3|21|09|87654|3  0|5|4   0    5    0|
        // |---|--|--|-----|----|-|---------------|
        // |332|22|22|22222|1111|1|111110000000000|
        // |1 9|87|65|43210|9  6|5|4   0    5    0|
        // |---|--|--|-----|----|-|---------------|
        // |111|10|x1| op3 | Rn |0|xxxxxxxxxxxxxxx|
        uint32_t op3 = (instr >> 20) & 0x1F;
        uint32_t Rn = (instr >> 16) & 0xF;
        switch (op3) {
          case 0x04: {
            // MOVW Rd, #imm16     - 111 10 i0 0010 0 iiii 0 iii dddd iiiiiiii
            uint32_t Rd = (instr >> 8) & 0xF;
            uint32_t i = (instr >> 26) & 1;
            uint32_t imm3 = (instr >> 12) & 0x7;
            uint32_t imm8 = instr & 0xFF;
            uint32_t imm16 = (Rn << 12) | (i << 11) | (imm3 << 8) | imm8;
            os << "MOVW ";
            DumpReg(os, Rd);
            os << ", #" << imm16 << "  // ";
            break;
          }
          case 0x0A: {
            // SUB.W Rd, Rn #imm12 - 111 10 i1 0101 0 nnnn 0 iii dddd iiiiiiii
            uint32_t Rd = (instr >> 8) & 0xF;
            uint32_t i = (instr >> 26) & 1;
            uint32_t imm3 = (instr >> 12) & 0x7;
            uint32_t imm8 = instr & 0xFF;
            uint32_t imm12 = (i << 11) | (imm3 << 8) | imm8;
            os << "SUB.W ";
            DumpReg(os, Rd);
            os << ", ";
            DumpReg(os, Rn);
            os << ", #" << imm12 << "  // ";
            break;
          }
          default:
            break;
        }
      } else {
        // Branches and miscellaneous control
        // |111|11|1000000|0000|1|111|1100|00000000|
        // |5 3|21|0987654|3  0|5|4 2|10 8|7 5    0|
        // |---|--|-------|----|-|---|----|--------|
        // |332|22|2222222|1111|1|111|1100|00000000|
        // |1 9|87|6543210|9  6|5|4 2|10 8|7 5    0|
        // |---|--|-------|----|-|---|----|--------|
        // |111|10| op2   |    |1|op3|op4 |        |

        uint32_t op3 = (instr >> 12) & 7;
        //uint32_t op4 = (instr >> 8) & 0xF;
        switch (op3) {
          case 0:
            if ((op2 & 0x38) != 0x38) {
              // Conditional branch
              // |111|11|1|0000|000000|1|1|1 |1|1 |10000000000|
              // |5 3|21|0|9876|543  0|5|4|3 |2|1 |0    5    0|
              // |---|--|-|----|------|-|-|--|-|--|-----------|
              // |332|22|2|2222|221111|1|1|1 |1|1 |10000000000|
              // |1 9|87|6|5432|109  6|5|4|3 |2|1 |0    5    0|
              // |---|--|-|----|------|-|-|--|-|--|-----------|
              // |111|10|S|cond| imm6 |1|0|J1|0|J2| imm11     |
              uint32_t S = (instr >> 26) & 1;
              uint32_t J2 = (instr >> 11) & 1;
              uint32_t J1 = (instr >> 13) & 1;
              uint32_t imm6 = (instr >> 16) & 0x3F;
              uint32_t imm11 = instr & 0x7FF;
              uint32_t cond = (instr >> 22) & 0xF;
              int32_t imm32 = (S << 20) |  (J2 << 19) | (J1 << 18) | (imm6 << 12) | (imm11 << 1);
              imm32 = (imm32 << 11) >> 11;  // sign extend 21bit immediate
              os << "B";
              DumpCond(os, cond);
              os << ".W ";
              DumpBranchTarget(os, instr_ptr + 4, imm32);
              os << "  // ";
            }
            break;
          case 2:
          case 1: case 3:
            break;
          case 4: case 6: case 5: case 7: {
            // BL, BLX (immediate)
            // |111|11|1|0000000000|11|1 |1|1 |10000000000|
            // |5 3|21|0|9876543  0|54|3 |2|1 |0    5    0|
            // |---|--|-|----------|--|--|-|--|-----------|
            // |332|22|2|2222221111|11|1 |1|1 |10000000000|
            // |1 9|87|6|5    0   6|54|3 |2|1 |0    5    0|
            // |---|--|-|----------|--|--|-|--|-----------|
            // |111|10|S| imm10    |11|J1|L|J2| imm11     |
            uint32_t S = (instr >> 26) & 1;
            uint32_t J2 = (instr >> 11) & 1;
            uint32_t L = (instr >> 12) & 1;
            uint32_t J1 = (instr >> 13) & 1;
            uint32_t imm10 = (instr >> 16) & 0x3FF;
            uint32_t imm11 = instr & 0x7FF;
            if (L == 0) {
              os << "BX ";
            } else {
              os << "BLX ";
            }
            uint32_t I1 = ~(J1 ^ S);
            uint32_t I2 = ~(J2 ^ S);
            int32_t imm32 = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
            imm32 = (imm32 << 8) >> 8;  // sign extend 24 bit immediate.
            DumpBranchTarget(os, instr_ptr + 4, imm32);
            break;
          }
        }
      }
      break;
    case 3:
      switch (op2) {
        case 0x00: case 0x02: case 0x04: case 0x06:  // 000xxx0
        case 0x08: case 0x0A: case 0x0C: case 0x0E: {
          // Store single data item
          // |111|11|100|000|0|0000|1111|110000|000000|
          // |5 3|21|098|765|4|3  0|5  2|10   6|5    0|
          // |---|--|---|---|-|----|----|------|------|
          // |332|22|222|222|2|1111|1111|110000|000000|
          // |1 9|87|654|321|0|9  6|5  2|10   6|5    0|
          // |---|--|---|---|-|----|----|------|------|
          // |111|11|000|op3|0|    |    |  op4 |      |

          uint32_t op3 = (instr >> 21) & 7;
          //uint32_t op4 = (instr >> 6) & 0x3F;
          switch (op3) {
            case 0x2: case 0x6: {
              // STR.W Rt, [Rn, #imm12] - 111 11 000 110 0 nnnn tttt iiiiiiiiiiii
              // STR Rt, [Rn, #imm8]    - 111 11 000 010 0 nnnn tttt 1PUWiiiiiiii
              uint32_t Rn = (instr >> 16) & 0xF;
              uint32_t Rt = (instr >> 12) & 0xF;
              if (op3 == 2) {
                uint32_t P = (instr >> 10) & 1;
                uint32_t U = (instr >> 9) & 1;
                uint32_t W = (instr >> 8) & 1;
                uint32_t imm8 = instr & 0xFF;
                int32_t imm32 = (imm8 << 24) >> 24;  // sign-extend imm8
                if (Rn == 13 && P == 1 && U == 0 && W == 1) {
                  os << "PUSH ";
                  DumpReg(os, Rt);
                  os << "  // ";
                } else if (Rn == 15 || (P == 0 && W == 0)) {
                  os << "UNDEFINED ";
                } else {
                  if (P == 1 && U == 1 && W == 0) {
                    os << "STRT ";
                  } else {
                    os << "STR ";
                  }
                  DumpReg(os, Rt);
                  os << ", [";
                  DumpReg(os, Rn);
                  if (P == 0 && W == 1) {
                    os << "], #" << imm32;
                  } else {
                    os << ", #" << imm32 << "]";
                    if (W == 1) {
                      os << "!";
                    }
                  }
                  os << "  // ";
                }
              } else if (op3 == 6) {
                uint32_t imm12 = instr & 0xFFF;
                os << "STR.W ";
                DumpReg(os, Rt);
                os << ", [";
                DumpReg(os, Rn);
                os << ", #" << imm12 << "]  // ";
              }
              break;
            }
          }

          break;
        }
        case 0x05: case 0x0D: case 0x15: case 0x1D: { // 00xx101
          // Load word
          // |111|11|10|0 0|00|0|0000|1111|110000|000000|
          // |5 3|21|09|8 7|65|4|3  0|5  2|10   6|5    0|
          // |---|--|--|---|--|-|----|----|------|------|
          // |332|22|22|2 2|22|2|1111|1111|110000|000000|
          // |1 9|87|65|4 3|21|0|9  6|5  2|10   6|5    0|
          // |---|--|--|---|--|-|----|----|------|------|
          // |111|11|00|op3|10|1| Rn | Rt | op4  |      |
          // |111|11| op2       |    |    | imm12       |
          uint32_t op3 = (instr >> 23) & 3;
          uint32_t op4 = (instr >> 6) & 0x3F;
          uint32_t Rn = (instr >> 16) & 0xF;
          uint32_t Rt = (instr >> 12) & 0xF;
          if (op3 == 1 || Rn == 15) {
            // LDR.W Rt, [Rn, #imm12]          - 111 11 00 00 101 nnnn tttt iiiiiiiiiiii
            // LDR.W Rt, [PC, #imm12]          - 111 11 00 0x 101 1111 tttt iiiiiiiiiiii
            uint32_t imm12 = instr & 0xFFF;
            os << "LDR.W ";
            DumpReg(os, Rt);
            os << ", [";
            DumpReg(os, Rn);
            os << ", #" << imm12 << "]  // ";
          } else if (op4 == 0) {
            // LDR.W Rt, [Rn, Rm{, LSL #imm2}] - 111 11 00 00 101 nnnn tttt 000000iimmmm
            uint32_t imm2 = (instr >> 4) & 0xF;
            uint32_t Rm = instr & 0xF;
            os << "LDR.W ";
            DumpReg(os, Rt);
            os << ", [";
            DumpReg(os, Rn);
            os << ", ";
            DumpReg(os, Rm);
            if (imm2 != 0) {
              os << ", LSL #" << imm2;
            }
            os << "]  // ";
          } else {
            // LDRT Rt, [Rn, #imm8]            - 111 11 00 00 101 nnnn tttt 1110iiiiiiii
            uint32_t imm8 = instr & 0xFF;
            os << "LDRT ";
            DumpReg(os, Rt);
            os << ", [";
            DumpReg(os, Rn);
            os << ", #" << imm8 << "]  // ";
          }
          break;
        }
      }
    default:
      break;
  }
  os << StringPrintf("%08x\n", instr);
  return 4;
}

size_t DisassemblerArm::DumpThumb16(std::ostream& os, const uint8_t* instr_ptr) {
  uint16_t instr = ReadU16(instr_ptr);
  bool is_32bit = ((instr & 0xF000) == 0xF000) || ((instr & 0xF800) == 0xE800);
  if (is_32bit) {
    return DumpThumb32(os, instr_ptr);
  } else {
    os << StringPrintf("\t\t\t%p: ", instr_ptr);
    uint16_t opcode1 = instr >> 10;
    if (opcode1 < 0x10) {
      // shift (immediate), add, subtract, move, and compare
      uint16_t opcode2 = instr >> 9;
      switch (opcode2) {
        case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7:
        case 0x8: case 0x9: case 0xA: case 0xB: {
          // Logical shift left     - 00 000xx xxxxxxxxx
          // Logical shift right    - 00 001xx xxxxxxxxx
          // Arithmetic shift right - 00 010xx xxxxxxxxx
          uint16_t imm5 = (instr >> 6) & 0x1F;
          uint16_t Rm = (instr >> 3) & 7;
          uint16_t Rd = instr & 7;
          if (opcode2 <= 3) {
            os << "LSLS ";
          } else if (opcode2 <= 7) {
            os << "LSRS ";
          } else {
            os << "ASRS ";
          }
          DumpReg(os, Rd);
          os << ", ";
          DumpReg(os, Rm);
          os << ", #" << imm5 << "  // ";
          break;
        }
        case 0xC: case 0xD: case 0xE: case 0xF: {
          // Add register        - 00 01100 mmm nnn ddd
          // Sub register        - 00 01101 mmm nnn ddd
          // Add 3-bit immediate - 00 01110 iii nnn ddd
          // Sub 3-bit immediate - 00 01111 iii nnn ddd
          uint16_t imm3_or_Rm = (instr >> 6) & 7;
          uint16_t Rn = (instr >> 3) & 7;
          uint16_t Rd = instr & 7;
          if ((opcode2 & 2) != 0 && imm3_or_Rm == 0) {
            os << "MOV ";
          } else {
            if ((opcode2 & 1) == 0) {
              os << "ADDS ";
            } else {
              os << "SUBS ";
            }
          }
          DumpReg(os, Rd);
          os << ", ";
          DumpReg(os, Rn);
          if ((opcode2 & 2) == 0) {
            os << ", ";
            DumpReg(os, imm3_or_Rm);
          } else if (imm3_or_Rm != 0) {
            os << ", #" << imm3_or_Rm;
          }
          os << "  // ";
          break;
        }
        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x15: case 0x16: case 0x17:
        case 0x18: case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E: case 0x1F: {
          // MOVS Rd, #imm8 - 00100 ddd iiiiiiii
          // CMP  Rn, #imm8 - 00101 nnn iiiiiiii
          // ADDS Rn, #imm8 - 00110 nnn iiiiiiii
          // SUBS Rn, #imm8 - 00111 nnn iiiiiiii
          uint16_t Rn = (instr >> 8) & 7;
          uint16_t imm8 = instr & 0xFF;
          switch (opcode2 >> 2) {
            case 4: os << "MOVS "; break;
            case 5: os << "CMP "; break;
            case 6: os << "ADDS "; break;
            case 7: os << "SUBS "; break;
          }
          DumpReg(os, Rn);
          os << ", #" << imm8 << "  // ";
          break;
        }
        default:
          break;
      }
    } else if (opcode1 == 0x11) {
      // Special data instructions and branch and exchange
      uint16_t opcode2 = (instr >> 6) & 0x0F;
      switch (opcode2) {
        case 0x0: case 0x1: case 0x2: case 0x3: {
          // Add low registers  - 010001 0000 xxxxxx
          // Add high registers - 010001 0001/001x xxxxxx
          uint16_t DN = (instr >> 7) & 1;
          uint16_t Rm = (instr >> 3) & 0xF;
          uint16_t Rdn = instr & 7;
          uint16_t DN_Rdn = (DN << 3) | Rdn;
          os << "ADD ";
          DumpReg(os, DN_Rdn);
          os << ", ";
          DumpReg(os, Rm);
          os << "  // ";
          break;
        }
        case 0x8: case 0x9: case 0xA: case 0xB: {
          // Move low registers  - 010001 1000 xxxxxx
          // Move high registers - 010001 1001/101x xxxxxx
          uint16_t DN = (instr >> 7) & 1;
          uint16_t Rm = (instr >> 3) & 0xF;
          uint16_t Rdn = instr & 7;
          uint16_t DN_Rdn = (DN << 3) | Rdn;
          os << "MOV ";
          DumpReg(os, DN_Rdn);
          os << ", ";
          DumpReg(os, Rm);
          os << "  // ";
          break;
        }
        case 0x5: case 0x6: case 0x7: {
          // Compare high registers - 010001 0101/011x xxxxxx
          uint16_t N = (instr >> 7) & 1;
          uint16_t Rm = (instr >> 3) & 0xF;
          uint16_t Rn = instr & 7;
          uint16_t N_Rn = (N << 3) | Rn;
          os << "CMP ";
          DumpReg(os, N_Rn);
          os << ", ";
          DumpReg(os, Rm);
          os << "  // ";
          break;
        }
        case 0xC: case 0xD: case 0xE: case 0xF: {
          // Branch and exchange           - 010001 110x xxxxxx
          // Branch with link and exchange - 010001 111x xxxxxx
          uint16_t Rm = instr >> 3 & 0xF;
          if ((opcode2 & 0x2) == 0) {
            os << "BX ";
          } else {
            os << "BLX ";
          }
          DumpReg(os, Rm);
          os << "  // ";
          break;
        }
        default:
          break;
      }
    } else if ((instr & 0xF000) == 0xB000) {
      // Miscellaneous 16-bit instructions
      uint16_t opcode2 = (instr >> 5) & 0x7F;
      switch (opcode2) {
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: {
          // Add immediate to SP        - 1011 00000 ii iiiii
          // Subtract immediate from SP - 1011 00001 ii iiiii
          int imm7 = instr & 0x7F;
          if ((opcode2 & 4) == 0) {
            os << "ADD SP, SP, #";
          } else {
            os << "SUB SP, SP, #";
          }
          os << (imm7 << 2) << "  // ";
          break;
        }
        case 0x78: case 0x79: case 0x7A: case 0x7B:  // 1111xxx
        case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
          // If-Then, and hints
          uint16_t opA = (instr >> 4) & 0xF;
          uint16_t opB = instr & 0xF;
          if (opB == 0) {
            switch (opA) {
              case 0: os << "NOP  // "; break;
              case 1: os << "YIELD  // "; break;
              case 2: os << "WFE  // ";  break;
              case 3: os << "SEV  // "; break;
              default: break;
            }
          } else {
            os << "IT " << reinterpret_cast<void*>(opB) << " ";
            DumpCond(os, opA);
            os << "  // ";
          }
          break;
        }
        default:
          break;
      }
    } else if (((instr & 0xF000) == 0x5000) || ((instr & 0xE000) == 0x6000) ||
        ((instr & 0xE000) == 0x8000)) {
      // Load/store single data item
      uint16_t opA = instr >> 12;
      //uint16_t opB = (instr >> 9) & 7;
      switch (opA) {
        case 0x6: {
          // STR Rt, Rn, #imm - 01100 iiiii nnn ttt
          // LDR Rt, Rn, #imm - 01101 iiiii nnn ttt
          uint16_t imm5 = (instr >> 6) & 0x1F;
          uint16_t Rn = (instr >> 3) & 7;
          uint16_t Rt = instr & 7;
          if ((instr & 0x800) == 0) {
            os << "STR ";
          } else {
            os << "LDR ";
          }
          DumpReg(os, Rt);
          os << ", [";
          DumpReg(os, Rn);
          os << ", #" << (imm5 << 2) << "]  // ";
          break;
        }
        case 0x9: {
          // STR Rt, [SP, #imm] - 01100 ttt iiiiiiii
          // LDR Rt, [SP, #imm] - 01101 ttt iiiiiiii
          uint16_t imm8 = instr & 0xFF;
          uint16_t Rt = (instr >> 8) & 7;
          if ((instr & 0x800) == 0) {
            os << "STR ";
          } else {
            os << "LDR ";
          }
          DumpReg(os, Rt);
          os << ", [SP, #" << (imm8 << 2) << "]  // ";
          break;
        }
        default:
          break;
      }
    } else if (opcode1 == 0x38 || opcode1 == 0x39) {
      uint16_t imm11 = instr & 0x7FFF;
      int32_t imm32 = imm11 << 1;
      imm32 = (imm32 << 20) >> 20;  // sign extend 12 bit immediate
      os << "B ";
      DumpBranchTarget(os, instr_ptr + 4, imm32);
      os << "  // ";
    }
    os << StringPrintf("%04x\n", instr);
  }
  return 2;
}

}  // namespace arm
}  // namespace art
