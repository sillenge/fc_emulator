
#include <sstream>
#include <iomanip>
#include <cassert>
#include <map>

#include "cpu.h"
#include "logger.h"

namespace fc_emulator{

// 在CPU的寻址与运算函数里，我会尽量使用 value & 0x00FF 这样的操作来替代(uint8_t)uint16_t_value这样的操作
// 依赖于编译器的切片操作比较难理解，而且不同编译器之间可能有不同的做法，最好是手动操作每一个数据，保证精确


// 
CPU::CPU() {
    opcode_ = 0;
    totalCycles_ = 0;
    bus_ = nullptr;
}

// 总线连接
void CPU::connectBus(Bus* bus) {
    this->bus_ = bus;
}



// 复位
void CPU::reset() {
    // 这里可以理解为程序入口的指针，这里存放一个的地址，指向程序开始的地方
    // pc = readWord(0xFFFC); pc就绪就可以开始执行程序了
    addrAbs_ = VectorReset;
    uint16_t lo = read(addrAbs_);
    uint16_t hi = read(addrAbs_ + 1);
    regPC_ = (hi << 8) | lo;

    regA_ = 0;
    regX_ = 0;
    regY_ = 0;
    regSP_ = 0xFD;
    regFlags_ = 0x00 | Unused; // 固定设置为1

    addrRel_ = 0x0000;
    addrAbs_ = 0x0000;
    fetched_ = 0x00;

    cycles_ = 8;    // 8 个起始时钟
}


// 中断处理
void CPU::interrupt(FCInterruptType type) {
    push((regPC_ >> 8) & 0xFF);
    push(regPC_ & 0xFF);

    setFlag(Break, false);
    setFlag(Unused, true);
    push(regFlags_);

    setFlag(Interrupt, true);

    uint16_t vector = 0x0000;
    switch (type) {
    case IRQ:  
        vector = VectorIrq; break;
    case NMI:  
        vector = VectorMni; break;
    default: 
        assert(!"Wrong interrupt!!");
        break;
    }

    uint16_t lo = read(vector);
    uint16_t hi = read(vector + 1);
    regPC_ = (hi << 8) | lo;

}
// 中断请求 interrupt quest: 大致分为 mapper 和 APU 两大类
void CPU::irq() {
    // 是否屏蔽了IQR/BRK，中断中设置中断屏蔽，中断中不能再进行中断，NMI不受此影响
    if (getFlag(Interrupt) == 0) {
        interrupt(IRQ);
    }
    cycles_ = 7;
}

// 不可屏蔽中断 Non-Maskable Interrupt: 
// 发生在每次垂直空白(VBlank)时, NMI在NTSC制式下刷新次数为 60次/秒, PAL为50次/秒
void CPU::nmi() {
    interrupt(NMI);
    cycles_ = 8;
}

// 时钟周期
void CPU::clock() {
    if (cycles_ == 0) {
        static auto mapAsm = disassemble(0x8000, 0xFFFF);

        LOG_STREAM_DEBUG << (mapAsm.find(regPC_) == mapAsm.end() ? "" : mapAsm[regPC_])
        << "" << logRegs() << std::endl;

        // 读取操作码
        opcode_ = read(regPC_++);

        // 获取指令信息
        const FCInstruction& instr = lookup[opcode_];

        // 执行寻址模式
        cycles_ = instr.cycles;
        uint8_t extra1 = (this->*instr.addrmode)();
        
        // 执行指令
        uint8_t extra2 = (this->*instr.operate)();

        // 更新周期，最多一个额外周期
        cycles_ += (extra1 & extra2);
    }

    totalCycles_++;
    cycles_--;
}

// if flag == value { pc 跳转到 (pc + addRel)}
// 返回值为额外的周期数
uint8_t CPU::fetchFlagBanch(FCFlags flag, bool value) {
	uint8_t cyclesEx = 0;
	if (getFlag(flag) == value) {
		cyclesEx++;
		addrAbs_ = regPC_ + addrRel_;
		if ((addrAbs_ & 0xFF00) != (regPC_ & 0xFF00)) {
			cyclesEx++;
		}
		regPC_ = addrAbs_;
	}
	return cyclesEx;
}

// 辅助函数，读取内存中的数据
uint8_t CPU::fetch() {
    if (!(lookup[opcode_].addrmode == &CPU::IMP)) {
        fetched_ = read(addrAbs_);
    }
    return fetched_;
}

// ------------------------寻址模式实现------------------------
// 隐式寻址 (Implicit)
uint8_t CPU::IMP() { fetched_ = regA_; return 0; }

// 累加器寻址 (Accumulator) 
// 这个函数仅用于 ASL LSR ROL ROR 指令
// 正确的做法是将快表里的这四个指令的IMP寻址修改为ACC，但为了效率这样也几乎不会出错
uint8_t CPU::ACC() { fetched_ = regA_; return 0; }

// 立即数寻址 (Immediate)
uint8_t CPU::IMM() { addrAbs_ = regPC_++; return 0; }

// 零页寻址 (Zero Page)
uint8_t CPU::ZP0() { 
    addrAbs_ = read(regPC_);
    regPC_++;
    addrAbs_ &= 0x00FF;
    return 0; }

//  零页X寻址 (Zero Page, X)
uint8_t CPU::ZPX() {
    addrAbs_ = read(regPC_);
    regPC_++;
    addrAbs_ += regX_;
    addrAbs_ &= 0x00FF;
    return 0;
}

// 零页Y寻址(Zero Page, Y)
uint8_t CPU::ZPY() {
    addrAbs_ = read(regPC_);
    regPC_++;
    addrAbs_ += regY_;
    addrAbs_ &= 0x00FF;
    return 0;
}

// 相对寻址 (Relative) - 用于分支指令
uint8_t CPU::REL() {
    addrRel_ = read(regPC_);
    regPC_++;
    if (addrRel_ & 0x80) {
        addrRel_ |= 0xFF00;
    }
    return 0;
}

// 绝对寻址(Absolute)
uint8_t CPU::ABS() {
    uint16_t lo = read(regPC_);
    regPC_++;
    uint16_t hi = read(regPC_);
    regPC_++;
    addrAbs_ = (hi << 8) | lo;
    return 0;
}

// 绝对X寻址 (Absolute, X)
uint8_t CPU::ABX() {
    uint16_t lo = read(regPC_);
    regPC_++;
    uint16_t hi = read(regPC_);
    regPC_++;
    addrAbs_ = (hi << 8) | lo;
    addrAbs_ += regX_;

    if ((addrAbs_ & 0xFF00) != (hi << 8)) {
        return 1;
    }
    return 0;
}

// 绝对Y寻址 (Absolute, Y)
uint8_t CPU::ABY() {
    uint16_t lo = read(regPC_);
    regPC_++;
    uint16_t hi = read(regPC_);
    regPC_++;
    addrAbs_ = (hi << 8) | lo;
    addrAbs_ += regY_;

    if ((addrAbs_ & 0xFF00) != (hi << 8)) {
        return 1;
    }
    return 0;
}

// 间接寻址 (Indirect)
uint8_t CPU::IND() {
    uint16_t lo = read(regPC_);
    regPC_++;
    uint16_t hi = read(regPC_);
    regPC_++;
    uint16_t ptr = (hi << 8) | lo;

    if (lo == 0xFF) {
        addrAbs_ = (read(ptr & 0xFF00) << 8) | read(ptr);
    }
    else {
        addrAbs_ = (read(ptr + 1) << 8) | read(ptr);
    }
    return 0;
}

// 间接X寻址 (Indirect, X)
uint8_t CPU::IZX() {
    uint16_t t = read(regPC_);
    regPC_++;
    uint16_t lo = read((t + regX_) & 0xFF);
    uint16_t hi = read((t + regX_ + 1) & 0xFF);
    addrAbs_ = (hi << 8) | lo;
    return 0;
}

//  间接Y寻址 (Indirect, Y)
uint8_t CPU::IZY() {
    uint16_t t = read(regPC_);
    regPC_++;
    uint16_t lo = read(t & 0xFF);
    uint16_t hi = read((t + 1) & 0xFF);
    addrAbs_ = (hi << 8) | lo;
    addrAbs_ += regY_;

    if ((addrAbs_ & 0xFF00) != (hi << 8)) {
        return 1;
    }
    return 0;
}

// ------------------------指令实现------------------------

// A = regA_
// X = regX_
// Y = regY_
// SP = regSP_
// PC = regPC_
// Flags = regFlags
// M = Memory //内存地址
// 
// Add with Carry In  加法
// A = A + memory + C
uint8_t CPU::ADC() {
    fetch();
    uint16_t tmp = (uint16_t)regA_ + (uint16_t)fetched_ + (uint16_t)getFlag(Carry);
    setFlag(Carry, tmp & 0xFF00);
    setFlag(Overflow, (~((uint16_t)(regA_) ^ (uint16_t)(fetched_)) &
        ((uint16_t)(regA_) ^ (uint16_t)(tmp))) & 0x0080);
    setFlagZN((uint8_t)(tmp));
    regA_ = tmp & 0x00FF;
    return 1;
}

// Bitwise AND  与运算
// A = A & memory
uint8_t CPU::AND() {
    fetch();
    regA_ &= fetched_;
    setFlagZN((uint8_t)(regA_));
    return 1;
}

// Arithmetic Shift Left  带进位的左位移
// value = value << 1, or visually: C <- [76543210] <- 0
uint8_t CPU::ASL() {
    fetch();
    uint16_t tmp = (uint16_t)fetched_ << 1;
    setFlag(Carry, (tmp & 0xFF00) > 0);
    setFlagZN((uint8_t)(tmp));

    if (lookup[opcode_].addrmode == &CPU::IMP) {
        regA_ = tmp & 0x00FF;
    }
    else {
        write(addrAbs_, tmp & 0x00FF);
    }
    return 0;
}

// Branch if Carry Clear  如果 Flags.Carry == false 则跳转
// PC = PC + 2byte + memory (signed) 
// memory 一般在相对地址里，跳转的距离是(0x00 - 0xFF] 
// 其中，这个数是一个int8_t，也就是说他可以是一个负数，负数就向上相对地址跳转
uint8_t CPU::BCC() {
    return fetchFlagBanch(Carry, false);
}

// Branch if Carry Set  如果 Flags.Carry == true 则跳转，类似BCC
// PC = PC + 2byte + memory (signed) 
uint8_t CPU::BCS() {
	return fetchFlagBanch(Carry, true);
}

// Branch if Equal if (Flags.Zero == 1) 则跳转
// PC = PC + 2byte + memory (signed) 
uint8_t CPU::BEQ() {
    return fetchFlagBanch(Zero, true);
}

// Bit Test 测试当前数值
// A & memory
uint8_t CPU::BIT() {
	fetch();
	setFlag(Zero, ((regA_ & fetched_) & 0x00FF) == 0x00);
    setFlag(Negative, fetched_ & (1 << 7));
    setFlag(Overflow, fetched_ & (1 << 6));
	return 0;
}


// Branch if Minus 如果 Flags.Negative == true 则跳转
// PC = PC + 2byte + memory (signed)
uint8_t CPU::BMI() {
    return fetchFlagBanch(Negative, true);
}

// Branch if Not Equal  如果 Flags.Zero == false 则跳转
// PC = PC + 2byte + memory (signed)
uint8_t CPU::BNE() {
    return fetchFlagBanch(Zero, false);
}

// Branch if Positive 如果Flags.Negative == false 则跳转
// PC = PC + 2byte + memory (signed)
uint8_t CPU::BPL() {
	return fetchFlagBanch(Negative, false);
}

// Break 软中断
uint8_t CPU::BRK() {
    regPC_++;
    
    // push PC Flags
    push((uint8_t)((regPC_ >> 8) & 0x00FF));
    push((uint8_t)(regPC_ & 0x00FF));
    push(regFlags_ | (uint8_t)(Break) | (uint8_t)(Unused));

    setFlag(Interrupt, true);

    uint16_t lo = (uint16_t)(read(VectorBrk));
    uint16_t hi = (uint16_t)(read(VectorBrk + 1));
    regPC_ = lo | (hi << 8);
    return 0;
}

// Branch if Overflow Clear 如果 Flags.Overflow == 0 则跳转
// PC = PC + 2byte + memory (signed)
uint8_t CPU::BVC() {
    return fetchFlagBanch(Overflow, false);
}

// Branch if Overflow Set 如果 Flags.Overflow == 1 则跳转
uint8_t CPU::BVS() {
    return fetchFlagBanch(Overflow, true);
}

// Clear Carry Flag
uint8_t CPU::CLC() {
    setFlag(Carry, false);
    return 0;
}

// Clear Decimal Flag
uint8_t CPU::CLD() {
    setFlag(Decimal, false);
    return 0;
}

// Clear Interrupt Flag
uint8_t CPU::CLI() {
	setFlag(Interrupt, false);
	return 0;
}


// Clear Overflow Flag
uint8_t CPU::CLV() {
    setFlag(Overflow, false);
    return 0;
}

// Compare Accumulator
// Flags.Carry = ( A >= Memory)  --> (A-Memory >= 0) --> 非负数
// Flags.Zero = (A == Memory)
// Flags.Negative = (A-Memory < 0)
uint8_t CPU::CMP() {
    fetch();
    uint16_t tmp = (uint16_t)(regA_) - (uint16_t)(fetched_);
    setFlag(Carry, !(tmp & 0x8000));    // 非负数
    setFlagZN((uint8_t)(tmp));
    return 1;
}

// Compare X Accumulator
// Flags.Carry = ( X >= Memory)  --> (X-Memory >= 0) --> 非负数
// Flags.Zero = (X == Memory)
// Flags.Negative = (X-Memory < 0)
uint8_t CPU::CPX() {
    fetch();
	uint16_t tmp = (uint16_t)(regX_) - (uint16_t)(fetched_);
	setFlag(Carry, !(tmp & 0x8000));    // 非负数
	setFlagZN((uint8_t)(tmp));
	return 0;
}

// Compare Y Accumulator
// Flags.Carry = ( Y >= Memory)  --> (Y-Memory >= 0) --> 非负数
// Flags.Zero = (Y == Memory)
// Flags.Negative = (Y-Memory < 0)
uint8_t CPU::CPY() {
    fetch();
	uint16_t tmp = (uint16_t)(regY_) - (uint16_t)(fetched_);
	setFlag(Carry, !(tmp & 0x8000));    // 非负数
	setFlagZN((uint8_t)(tmp));
	return 0;
}

// Decrement Value at Memory Location
// Memory -= 1
uint8_t CPU::DEC() {
    fetch();
    uint16_t tmp = fetched_ - 1;
    setFlagZN((uint8_t)(tmp));
	write(addrAbs_, tmp & 0x00FF);
	return 0;
}

// Decrement X Register
// X -= 1
uint8_t CPU::DEX() {
	regX_--;
	setFlagZN(regX_);
	return 0;
}

// Decrement Y Register
// Y -= 1
uint8_t CPU::DEY() {
    regY_--;
	setFlagZN(regY_);
	return 0;
}

// Bitwise Logic XOR
uint8_t CPU::EOR() {
    fetch();
    regA_ ^= fetched_;
    setFlagZN(regA_);
    return 1;
}


// Increment Value at Memory Location
// Memory += 1
uint8_t CPU::INC() {
    fetch();
    uint16_t tmp = fetched_ + 1;
    setFlagZN((uint8_t)(tmp));
    write(addrAbs_, (uint8_t)(tmp));
    return 0;
}

// Increment X Register
// X += 1
uint8_t CPU::INX() {
    regX_++;
    setFlagZN(regX_);
    return 0;
}

// Increment Y Register
// Y += 1
uint8_t CPU::INY() {
	regY_++;
	setFlagZN(regY_);
	return 0;
}


// Jump To Location
uint8_t CPU::JMP() {
    regPC_ = addrAbs_;
    return 0;
}

// Jump To Sub-Routine
// push PC, JMP addrAbs
uint8_t CPU::JSR() {
    regPC_--;
    push((regPC_ >> 8) & 0x00FF);
    push(regPC_ & 0x00FF);
    regPC_ = addrAbs_;
    return 0;

}

// Load The Accumulator
uint8_t CPU::LDA() {
    fetch();
    regA_ = fetched_;
    setFlagZN(regA_);
    return 1;
}



// Load The X Register
uint8_t CPU::LDX() {
	fetch();
	regX_ = fetched_;
	setFlagZN(regX_);
	return 1;
}


// 
uint8_t CPU::LDY() {
	fetch();
	regY_ = fetched_;
	setFlagZN(regY_);
	return 1;
}


// Logical Shift Right
uint8_t CPU::LSR() {
    fetch();
    uint8_t tmp;
    setFlag(Carry, fetched_ & 0x01);
    tmp = fetched_ >> 1;
    setFlagZN(tmp);
    if (lookup[opcode_].addrmode == &CPU::IMP) {
        regA_ = tmp & 0xFF;
    }
    else {
        write(addrAbs_, tmp & 0xFF);
    }
    return 0;
}

// No Operation
uint8_t CPU::NOP() {
    switch (opcode_)
    {
    case 0x1C:
    case 0x3C:
    case 0x5C:
    case 0x7C:
    case 0xDC:
    case 0xFC:
        return 1;
    default:
        break;
    }
    return 0;
}

// Bitwise Logic OR
uint8_t CPU::ORA() {
    fetch();
    regA_ |= fetched_;
    setFlagZN(regA_);
    return 1;
}

// Push Accumulator to Stack
uint8_t CPU::PHA() {
    push(regA_);
    return 0;
}

// Push Processor Status (push Flags)
uint8_t CPU::PHP() {
    // 硬件上Unused与Break永远是1
    // 但是Flags.Break仅为1仅存在于堆栈中，不在寄存器中表现
    // Flags.Break: This flag exists only in the flags byte pushed to the stack, not as real state in the CPU
    push(regFlags_ | Unused | Break);
    return 0;
}

// Pull A Register (pop A)
uint8_t CPU::PLA() {
    regA_ = pop();
    setFlagZN(regA_);
    return 0;
}

// Pull Processor Status
uint8_t CPU::PLP() {
    regFlags_ = pop();
    setFlag(Unused, true);
    // 依照之前PHP的约定，它仅存在于堆栈中，不在寄存器中表现
    setFlag(Break, false);
    return 0;
}

// Rotate Left
// res = Carry <- value <- Carry  // ( C <- [12345678] <- C )
uint8_t CPU::ROL() {
    fetch();
    uint16_t tmp = fetched_ << 1 | (uint16_t)(getFlag(Carry));
    setFlag(Carry, tmp & 0xFF00);
    setFlagZN(tmp & 0x00FF);
    if (lookup[opcode_].addrmode == &CPU::IMP) {
        regA_ = tmp & 0x00FF;
    }
    else {
        write(addrAbs_, tmp & 0x00FF);
    }
    return 0;
}

// Rotate Right
// res = Carry -> value -> Carry  // ( C -> [12345678] -> C )
uint8_t CPU::ROR() {
	fetch();
    // [C 12345678]
	uint16_t tmp = (getFlag(Carry) << 8) | fetched_;
	setFlag(Carry, tmp & 0x0001);
    // [  C1234567] -> 8
    tmp >>= 1;
	setFlagZN(tmp & 0x00FF);

	if (lookup[opcode_].addrmode == &CPU::IMP) {
		regA_ = tmp & 0x00FF;
	}
	else {
		write(addrAbs_, tmp & 0x00FF);
	}
    return 0;
}

// Return from Interrupt
uint8_t CPU::RTI() {
    regFlags_ = pop();
    setFlag(Break, false);
    setFlag(Unused, true);

    uint16_t lo = pop();
    uint16_t hi = pop();
    regPC_ = (hi << 8) | lo;
    return 0;
}

// Return from Subroutine
// PC = pop_word()
// PC++;
uint8_t CPU::RTS() {
	uint16_t lo = pop();
	uint16_t hi = pop();
	regPC_ = (hi << 8) | lo;
    regPC_++;
	return 0;
}

// Subtract with Carry
// A = A - memory - (1 - Carry)   // ~(~Carry) = ~~Carry + 1 = Carry + 1
// A = A - memory - ~Carry
// A = A + ~memory + Carry
// 上面的三个公式用一个即可
uint8_t CPU::SBC() {
    fetch();
    uint16_t tmp = (uint16_t)regA_ - (uint16_t)fetched_ - !getFlag(Carry);
    setFlag(Carry, !(tmp & 0xFF00));
    setFlag(Overflow, ((regA_ ^ fetched_) & 0x80) && ((regA_ ^ tmp) & 0x80));
    setFlagZN((uint8_t)(tmp));
    regA_ = (uint8_t)(tmp);
    return 1;
}

// Set Carry
uint8_t CPU::SEC() {
    setFlag(Carry, true);
    return 0;
}

// Set Decimal
uint8_t CPU::SED() {
    setFlag(Decimal, true);
	return 0;
}

// Set Interrupt Disable
uint8_t CPU::SEI() {
    setFlag(Interrupt, true);
	return 0;
}

// Store A
uint8_t CPU::STA() {
    write(addrAbs_, regA_);
	return 0;
}

// Store X
uint8_t CPU::STX() {
    write(addrAbs_, regX_);
    return 0;
}

// Store Y
uint8_t CPU::STY() {
	write(addrAbs_, regY_);
	return 0;
}

// Transfer A to X
uint8_t CPU::TAX() {
    regX_ = regA_;
    setFlagZN(regX_);
    return 0;
}

// Transfer A to Y
uint8_t CPU::TAY() {
	regY_ = regA_;
	setFlagZN(regY_);
	return 0;
}

// Transfer Stack Pointer to X
uint8_t CPU::TSX() {
	regX_ = regSP_;
	setFlagZN(regX_);
	return 0;
}

// Transfer X to A
uint8_t CPU::TXA() {
	regA_ = regX_;
	setFlagZN(regA_);
	return 0;
}

// Transfer X to Stack Pointer
uint8_t CPU::TXS() {
	regSP_ = regX_;
	return 0;
}

// Transfer Y to A
uint8_t CPU::TYA() {
	regA_ = regY_;
	setFlagZN(regY_);
	return 0;
}

// 未实现指令
uint8_t CPU::XXX() {
    LOG_STREAM_ERROR << "未实现的指令: 0x" << std::hex << (int)opcode_;
    assert(!"未实现的指令: 0x" && opcode_);
    return 0;
}

// 调试信息
std::string CPU::logRegs() const
{
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << " A=" << std::setw(2) << static_cast<int>(regA_);
    ss << " X=" << std::setw(2) << static_cast<int>(regX_);
    ss << " Y=" << std::setw(2) << static_cast<int>(regY_);
    ss << " SP=" << std::setw(2) << static_cast<int>(regSP_);
    ss << " PC=" << std::setw(4) << regPC_;
    ss << " flags=" << std::setw(2) << static_cast<int>(regFlags_);

    return ss.str();
}

// 反汇编单条指令
std::string CPU::disassemble(uint16_t addr) const {
	std::stringstream ss;
	ss << std::hex << std::uppercase << std::setfill('0');

	uint16_t currentAddr = addr;
	uint8_t opcode = read(addr++);
	const FCInstruction& instr = lookup[opcode];
    

	// 显示地址
	ss << "$" << std::setw(4) << currentAddr << "  ";
    /*{
        // 显示操作码字节
	    ss << std::setw(2) << (int)opcode << " ";
	    // 读取额外的操作数字节，instr.length最长为3
        */
	    uint8_t bytes[2] = { 0 };
	    for (int i = 0; i < instr.length - 1; i++) {
		    bytes[i] = read(addr++);
		    // ss << std::setw(2) << (int)bytes[i] << " ";
	    }
        /*
	    // 对齐（确保至少显示3个字节）
	    for (int i = instr.length; i < 3; i++) {
		    ss << "   ";
	    }
    
	    ss << " ";
        }
    */

	// 添加指令助记符
	ss << instr.name << " ";
	// 根据寻址模式添加操作数
	uint16_t operand = 0;
	if (instr.addrmode == &CPU::IMP) {
		// 隐式寻址，无操作数
        ss << "{IMP}";
	}
	else if (instr.addrmode == &CPU::ACC) {
		// 累加器寻址
		ss << "A" << " {ACC}";
	}
	else if (instr.addrmode == &CPU::IMM) {
		// 立即数寻址
		ss << "#$" << std::setw(2) << (int)bytes[0] << " {IMM}";
	}
	else if (instr.addrmode == &CPU::ZP0) {
		// 零页寻址
		ss << "$" << std::setw(2) << (int)bytes[0] << " {ZP0}";
	}
	else if (instr.addrmode == &CPU::ZPX) {
		// 零页X寻址
		ss << "$" << std::setw(2) << (int)bytes[0] << ",X" << " {ZPX}";
	}
	else if (instr.addrmode == &CPU::ZPY) {
		// 零页Y寻址
		ss << "$" << std::setw(2) << (int)bytes[0] << ",Y" << " {ZPY}";
	}
	else if (instr.addrmode == &CPU::REL) {
		// 相对寻址 - 计算目标地址
		int8_t offset = bytes[0];
		uint16_t target = currentAddr + 2 + offset;
		ss << "[$" << std::setw(4) << target << "]" << " {REL}";
	}
	else if (instr.addrmode == &CPU::ABS) {
		// 绝对寻址
		operand = (bytes[1] << 8) | bytes[0];
		ss << "$" << std::setw(4) << operand << " {ABS}";
	}
	else if (instr.addrmode == &CPU::ABX) {
		// 绝对X寻址
		operand = (bytes[1] << 8) | bytes[0];
        ss << "$" << std::setw(4) << operand << ",X" << " {ABX}";
	}
	else if (instr.addrmode == &CPU::ABY) {
		// 绝对Y寻址
		operand = (bytes[1] << 8) | bytes[0];
		ss << "$" << std::setw(4) << operand << ",Y" << " {ABY}";
	}
	else if (instr.addrmode == &CPU::IND) {
		// 间接寻址
		operand = (bytes[1] << 8) | bytes[0];
		ss << "($" << std::setw(4) << operand << ")" << " {IND}";
	}
	else if (instr.addrmode == &CPU::IZX) {
		// 间接X寻址
		ss << "($" << std::setw(2) << (int)bytes[0] << ",X)" << " {IZX}";
	}
	else if (instr.addrmode == &CPU::IZY) {
		// 间接Y寻址
		ss << "($" << std::setw(2) << (int)bytes[0] << "),Y" << " {IZY}";
	}
	else {
		// 未知的寻址模式
		ss << "???";
	}

	// 如果是非法指令，添加标记
	if (instr.operate == &CPU::XXX) {
		ss << "  ; 非法操作码";
	}

	return ss.str();
}




std::map<uint16_t, std::string> CPU::disassemble(uint16_t start, uint16_t end) const {
    std::map<uint16_t, std::string> result;
    uint32_t addr = start;
    
    while (addr <= end) {
        uint8_t opcode = read(addr);
        const FCInstruction& instr = lookup[opcode];
        uint8_t length = instr.length;

        // 反汇编当前地址的指令
        result[addr] = disassemble(addr);

        // 下一条指令
        addr += length;

    }

    return result;
}


// 指令表格式：{"指令名", 执行函数, 寻址函数, 周期数, 长度}
const FCInstruction CPU::lookup[256] = {
    // 0x00
    {"BRK", &CPU::BRK, &CPU::IMM, 7, 1}, {"ORA", &CPU::ORA, &CPU::IZX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 3, 1}, {"ORA", &CPU::ORA, &CPU::ZP0, 3, 2}, {"ASL", &CPU::ASL, &CPU::ZP0, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"PHP", &CPU::PHP, &CPU::IMP, 3, 1}, {"ORA", &CPU::ORA, &CPU::IMM, 2, 2}, {"ASL", &CPU::ASL, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 3}, {"ORA", &CPU::ORA, &CPU::ABS, 4, 3}, {"ASL", &CPU::ASL, &CPU::ABS, 6, 3}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    // 0x10
    {"BPL", &CPU::BPL, &CPU::REL, 2, 2}, {"ORA", &CPU::ORA, &CPU::IZY, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 2}, {"ORA", &CPU::ORA, &CPU::ZPX, 4, 2}, {"ASL", &CPU::ASL, &CPU::ZPX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"CLC", &CPU::CLC, &CPU::IMP, 2, 1}, {"ORA", &CPU::ORA, &CPU::ABY, 4, 3}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 3}, {"ORA", &CPU::ORA, &CPU::ABX, 4, 3}, {"ASL", &CPU::ASL, &CPU::ABX, 7, 3}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    // 0x20
    {"JSR", &CPU::JSR, &CPU::ABS, 6, 3}, {"AND", &CPU::AND, &CPU::IZX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"BIT", &CPU::BIT, &CPU::ZP0, 3, 2}, {"AND", &CPU::AND, &CPU::ZP0, 3, 2}, {"ROL", &CPU::ROL, &CPU::ZP0, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"PLP", &CPU::PLP, &CPU::IMP, 4, 1}, {"AND", &CPU::AND, &CPU::IMM, 2, 2}, {"ROL", &CPU::ROL, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"BIT", &CPU::BIT, &CPU::ABS, 4, 3}, {"AND", &CPU::AND, &CPU::ABS, 4, 3}, {"ROL", &CPU::ROL, &CPU::ABS, 6, 3}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    // 0x30
    {"BMI", &CPU::BMI, &CPU::REL, 2, 2}, {"AND", &CPU::AND, &CPU::IZY, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 2}, {"AND", &CPU::AND, &CPU::ZPX, 4, 2}, {"ROL", &CPU::ROL, &CPU::ZPX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"SEC", &CPU::SEC, &CPU::IMP, 2, 1}, {"AND", &CPU::AND, &CPU::ABY, 4, 3}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 3}, {"AND", &CPU::AND, &CPU::ABX, 4, 3}, {"ROL", &CPU::ROL, &CPU::ABX, 7, 3}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    // 0x40
    {"RTI", &CPU::RTI, &CPU::IMP, 6, 1}, {"EOR", &CPU::EOR, &CPU::IZX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 3, 1}, {"EOR", &CPU::EOR, &CPU::ZP0, 3, 2}, {"LSR", &CPU::LSR, &CPU::ZP0, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"PHA", &CPU::PHA, &CPU::IMP, 3, 1}, {"EOR", &CPU::EOR, &CPU::IMM, 2, 2}, {"LSR", &CPU::LSR, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"JMP", &CPU::JMP, &CPU::ABS, 3, 3}, {"EOR", &CPU::EOR, &CPU::ABS, 4, 3}, {"LSR", &CPU::LSR, &CPU::ABS, 6, 3}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    // 0x50
    {"BVC", &CPU::BVC, &CPU::REL, 2, 2}, {"EOR", &CPU::EOR, &CPU::IZY, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 2}, {"EOR", &CPU::EOR, &CPU::ZPX, 4, 2}, {"LSR", &CPU::LSR, &CPU::ZPX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"CLI", &CPU::CLI, &CPU::IMP, 2, 1}, {"EOR", &CPU::EOR, &CPU::ABY, 4, 3}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 3}, {"EOR", &CPU::EOR, &CPU::ABX, 4, 3}, {"LSR", &CPU::LSR, &CPU::ABX, 7, 3}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    // 0x60
    {"RTS", &CPU::RTS, &CPU::IMP, 6, 1}, {"ADC", &CPU::ADC, &CPU::IZX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 3, 1}, {"ADC", &CPU::ADC, &CPU::ZP0, 3, 2}, {"ROR", &CPU::ROR, &CPU::ZP0, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"PLA", &CPU::PLA, &CPU::IMP, 4, 1}, {"ADC", &CPU::ADC, &CPU::IMM, 2, 2}, {"ROR", &CPU::ROR, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"JMP", &CPU::JMP, &CPU::IND, 5, 3}, {"ADC", &CPU::ADC, &CPU::ABS, 4, 3}, {"ROR", &CPU::ROR, &CPU::ABS, 6, 3}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    // 0x70
    {"BVS", &CPU::BVS, &CPU::REL, 2, 2}, {"ADC", &CPU::ADC, &CPU::IZY, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 2}, {"ADC", &CPU::ADC, &CPU::ZPX, 4, 2}, {"ROR", &CPU::ROR, &CPU::ZPX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"SEI", &CPU::SEI, &CPU::IMP, 2, 1}, {"ADC", &CPU::ADC, &CPU::ABY, 4, 3}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 3}, {"ADC", &CPU::ADC, &CPU::ABX, 4, 3}, {"ROR", &CPU::ROR, &CPU::ABX, 7, 3}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    // 0x80
    {"???", &CPU::NOP, &CPU::IMM, 2, 2}, {"STA", &CPU::STA, &CPU::IZX, 6, 2}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"STY", &CPU::STY, &CPU::ZP0, 3, 2}, {"STA", &CPU::STA, &CPU::ZP0, 3, 2}, {"STX", &CPU::STX, &CPU::ZP0, 3, 2}, {"???", &CPU::XXX, &CPU::IMP, 3, 1},
    {"DEY", &CPU::DEY, &CPU::IMP, 2, 1}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"TXA", &CPU::TXA, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"STY", &CPU::STY, &CPU::ABS, 4, 3}, {"STA", &CPU::STA, &CPU::ABS, 4, 3}, {"STX", &CPU::STX, &CPU::ABS, 4, 3}, {"???", &CPU::XXX, &CPU::IMP, 4, 1},
    // 0x90
    {"BCC", &CPU::BCC, &CPU::REL, 2, 2}, {"STA", &CPU::STA, &CPU::IZY, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"STY", &CPU::STY, &CPU::ZPX, 4, 2}, {"STA", &CPU::STA, &CPU::ZPX, 4, 2}, {"STX", &CPU::STX, &CPU::ZPY, 4, 2}, {"???", &CPU::XXX, &CPU::IMP, 4, 1},
    {"TYA", &CPU::TYA, &CPU::IMP, 2, 1}, {"STA", &CPU::STA, &CPU::ABY, 5, 3}, {"TXS", &CPU::TXS, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"???", &CPU::NOP, &CPU::IMP, 5, 3}, {"STA", &CPU::STA, &CPU::ABX, 5, 3}, {"???", &CPU::NOP, &CPU::IMP, 5, 1}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    // 0xA0
    {"LDY", &CPU::LDY, &CPU::IMM, 2, 2}, {"LDA", &CPU::LDA, &CPU::IZX, 6, 2}, {"LDX", &CPU::LDX, &CPU::IMM, 2, 2}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"LDY", &CPU::LDY, &CPU::ZP0, 3, 2}, {"LDA", &CPU::LDA, &CPU::ZP0, 3, 2}, {"LDX", &CPU::LDX, &CPU::ZP0, 3, 2}, {"???", &CPU::XXX, &CPU::IMP, 3, 1},
    {"TAY", &CPU::TAY, &CPU::IMP, 2, 1}, {"LDA", &CPU::LDA, &CPU::IMM, 2, 2}, {"TAX", &CPU::TAX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"LDY", &CPU::LDY, &CPU::ABS, 4, 3}, {"LDA", &CPU::LDA, &CPU::ABS, 4, 3}, {"LDX", &CPU::LDX, &CPU::ABS, 4, 3}, {"???", &CPU::XXX, &CPU::IMP, 4, 1},
    // 0xB0
    {"BCS", &CPU::BCS, &CPU::REL, 2, 2}, {"LDA", &CPU::LDA, &CPU::IZY, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"LDY", &CPU::LDY, &CPU::ZPX, 4, 2}, {"LDA", &CPU::LDA, &CPU::ZPX, 4, 2}, {"LDX", &CPU::LDX, &CPU::ZPY, 4, 2}, {"???", &CPU::XXX, &CPU::IMP, 4, 1},
    {"CLV", &CPU::CLV, &CPU::IMP, 2, 1}, {"LDA", &CPU::LDA, &CPU::ABY, 4, 3}, {"TSX", &CPU::TSX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 4, 1},
    {"LDY", &CPU::LDY, &CPU::ABX, 4, 3}, {"LDA", &CPU::LDA, &CPU::ABX, 4, 3}, {"LDX", &CPU::LDX, &CPU::ABY, 4, 3}, {"???", &CPU::XXX, &CPU::IMP, 4, 1},
    // 0xC0
    {"CPY", &CPU::CPY, &CPU::IMM, 2, 2}, {"CMP", &CPU::CMP, &CPU::IZX, 6, 2}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"CPY", &CPU::CPY, &CPU::ZP0, 3, 2}, {"CMP", &CPU::CMP, &CPU::ZP0, 3, 2}, {"DEC", &CPU::DEC, &CPU::ZP0, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"INY", &CPU::INY, &CPU::IMP, 2, 1}, {"CMP", &CPU::CMP, &CPU::IMM, 2, 2}, {"DEX", &CPU::DEX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"CPY", &CPU::CPY, &CPU::ABS, 4, 3}, {"CMP", &CPU::CMP, &CPU::ABS, 4, 3}, {"DEC", &CPU::DEC, &CPU::ABS, 6, 3}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    // 0xD0
    {"BNE", &CPU::BNE, &CPU::REL, 2, 2}, {"CMP", &CPU::CMP, &CPU::IZY, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 2}, {"CMP", &CPU::CMP, &CPU::ZPX, 4, 2}, {"DEC", &CPU::DEC, &CPU::ZPX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"CLD", &CPU::CLD, &CPU::IMP, 2, 1}, {"CMP", &CPU::CMP, &CPU::ABY, 4, 3}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 3}, {"CMP", &CPU::CMP, &CPU::ABX, 4, 3}, {"DEC", &CPU::DEC, &CPU::ABX, 7, 3}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    // 0xE0
    {"CPX", &CPU::CPX, &CPU::IMM, 2, 2}, {"SBC", &CPU::SBC, &CPU::IZX, 6, 2}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"CPX", &CPU::CPX, &CPU::ZP0, 3, 2}, {"SBC", &CPU::SBC, &CPU::ZP0, 3, 2}, {"INC", &CPU::INC, &CPU::ZP0, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 5, 1},
    {"INX", &CPU::INX, &CPU::IMP, 2, 1}, {"SBC", &CPU::SBC, &CPU::IMM, 2, 2}, {"NOP", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 2, 1},
    {"CPX", &CPU::CPX, &CPU::ABS, 4, 3}, {"SBC", &CPU::SBC, &CPU::ABS, 4, 3}, {"INC", &CPU::INC, &CPU::ABS, 6, 3}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    // 0xF0
    {"BEQ", &CPU::BEQ, &CPU::REL, 2, 2}, {"SBC", &CPU::SBC, &CPU::IZY, 5, 2}, {"???", &CPU::XXX, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 8, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 2}, {"SBC", &CPU::SBC, &CPU::ZPX, 4, 2}, {"INC", &CPU::INC, &CPU::ZPX, 6, 2}, {"???", &CPU::XXX, &CPU::IMP, 6, 1},
    {"SED", &CPU::SED, &CPU::IMP, 2, 1}, {"SBC", &CPU::SBC, &CPU::ABY, 4, 3}, {"???", &CPU::NOP, &CPU::IMP, 2, 1}, {"???", &CPU::XXX, &CPU::IMP, 7, 1},
    {"???", &CPU::NOP, &CPU::IMP, 4, 3}, {"SBC", &CPU::SBC, &CPU::ABX, 4, 3}, {"INC", &CPU::INC, &CPU::ABX, 7, 3}, {"???", &CPU::XXX, &CPU::IMP, 7, 1}
};

} // namespace fc_emulator