#pragma once


// CPU指令实现参考：https://www.nesdev.org/wiki/Instruction_reference#ADC
#include <stdint.h>
#include <string>
#include <functional>
#include <map>

#include "bus.h"
#include "cartridge.h"
#include "logger.h"

namespace fc_emulator{
class Bus;
class CPU;

// 6502状态寄存器标志位
enum FCFlags {
    Carry       = (1 << 0),     // 进位标志
    Zero        = (1 << 1),     // 零标志
    Interrupt   = (1 << 2),     // 中断禁止标志
    Decimal     = (1 << 3),     // 十进制模式标志
    Break       = (1 << 4),     // 中断命令标志
    Unused      = (1 << 5),     // 未使用位
    Overflow    = (1 << 6),     // 溢出标志
    Negative    = (1 << 7)      // 负标志
};

// 中断类型
// 优先级高的排前面
// 这几种中断不论哪一种都恒存在 FlagUnused == 1;
// 而regFlags_.FlagBreak则用来区分是否是软中断，1: 软中断
enum FCInterruptType {
    NONE,
    IRQ,      // 可屏蔽中断
    //BRK,      // 软中断，CPU调用BRK函数实现
    NMI,      // 非可屏蔽中断
    //RESET     // 复位中断
};

enum FCInterruptVector {
    VectorMni = 0xFFFA,
    VectorReset = 0xFFFC,
    VectorBrk = 0xFFFE,
    VectorIrq = 0xFFFE
};

// 寻址模式
enum FCAddressingMode {
    IMP,      // 隐式寻址
    ACC,      // 累加器寻址
    IMM,      // 立即寻址
    ZP0,      // 零页寻址
    ZPX,      // 零页X变址
    ZPY,      // 零页Y变址
    REL,      // 相对寻址
    ABS,      // 绝对寻址
    ABX,      // 绝对X变址
    ABY,      // 绝对Y变址
    IND,      // 间接寻址
    IZX,      // X间接寻址
    IZY       // Y间接寻址
};

// 指令信息结构
struct FCInstruction {
    std::string name;                           // 指令助记符
    uint8_t(CPU::* operate)(void) = nullptr;    // 操作函数
    uint8_t(CPU::* addrmode)(void) = nullptr;   // 寻址函数
    uint8_t cycles = 0;                         // 基本周期数
    uint8_t length = 0;                         // 指令长度

};

class CPU {
public:
    CPU();
    ~CPU() = default;

    // 连接总线
    void connectBus(Bus* bus);
    bool checkBusReady() { return bus_ != nullptr; }

    // 读写操作    
	inline uint8_t read(uint16_t addr) const        { return bus_->CPURead(addr); }

	// 内存读写
	inline void write(uint16_t addr, uint8_t data)  { bus_->CPUWrite(addr, data); }

    // 主要接口
    void reset();                           // 复位
    void clock();                           // 时钟周期
    void interrupt(FCInterruptType type);   // 中断处理
    void irq();                             // 中断请求
    void nmi();                             // 非可屏蔽中断
    
    
    // 状态查询
    inline bool isComplete() const { return cycles_ == 0; }
    inline uint64_t getCycles() const { return totalCycles_; }
    inline uint16_t getPC() const { return regPC_; }

public:
    // 调试信息
    std::string logRegs() const;
	// 单条指令反汇编
	std::string disassemble(uint16_t addr) const;
	// 范围反汇编
    std::map<uint16_t, std::string> disassemble(uint16_t start, uint16_t end) const;

    //// 保存/加载状态 
    //void save_state(std::vector<uint8_t>& out) const;
    //void load_state(const std::vector<uint8_t>& in);
    uint8_t getRegA() const { return regA_; }
    uint8_t getRegX() const { return regX_; }
    uint8_t getRegY() const { return regY_; }
    uint8_t getRegSP() const { return regSP_; }
    uint16_t getRegPC() const { return regPC_; }
    uint8_t getRegFlags() const { return regFlags_; }

private:
    // 6502寄存器
    uint8_t  regA_ = 0x00;          // 累加器
    uint8_t  regX_ = 0x00;          // 寄存器 X
    uint8_t  regY_ = 0x00;          // 寄存器 Y
    uint8_t  regSP_ = 0x00;         // 栈指针
    uint16_t regPC_ = 0x0000;       // 程序计数器
    uint8_t  regFlags_ = 0x00;      // 状态寄存器

    // 内部状态
    uint8_t  fetched_ = 0x00;    // 获取的操作数
    uint16_t addrAbs_ = 0x0000; // 绝对地址
    uint16_t addrRel_ = 0x0000; // 相对地址
    uint8_t  opcode_ = 0x00;     // 当前操作码
    // CPU时钟周期
    uint8_t  cycles_ = 0;        // 剩余周期
    uint64_t totalCycles_ = 0;  // 总周期计数

    // 外部组件
    Bus* bus_ = nullptr;

private:
    // 指令快表
    static const FCInstruction lookup[256];
private:
    // 出入栈辅助函数
	inline void push(uint8_t data) {
		write(0x0100 + regSP_, data);
		regSP_--;
	}
	inline uint8_t pop() {
		regSP_++;
		return read(0x0100 + regSP_);
	}

	inline bool getFlag(FCFlags flag) const { return (regFlags_ & flag) != 0; }

	inline void setFlag(FCFlags flag, bool value) {
		/*常用代码优化，使其不带分支
			if (value) {
				regFlags_ |= flag;
			}
			else {
				regFlags_ &= ~flag;
			}*/
		uint8_t f = static_cast<uint8_t>(flag);
		// v=0 --> 0x00, v=1 --> 0xFF（无符号取负）
		uint8_t mask = -static_cast<uint8_t>((value));
		regFlags_ = (regFlags_ & ~f) | (mask & f);
	}

	// 根据value 设置Flags.Zero和Flags.Negative
	inline void setFlagZN(uint8_t value) {
		setFlag(Zero, !value);
		setFlag(Negative, value & 0x80);
	}

	// 辅助函数
	uint8_t fetchFlagBanch(FCFlags flag, bool value);
	uint8_t fetch();
    // 寻址模式函数
    uint8_t IMP(); uint8_t ACC(); uint8_t IMM(); uint8_t ZP0();
    uint8_t ZPX(); uint8_t ZPY(); uint8_t REL(); uint8_t ABS();
    uint8_t ABX(); uint8_t ABY(); uint8_t IND(); uint8_t IZX();
    uint8_t IZY();
    // 指令函数
    uint8_t ADC(); uint8_t AND(); uint8_t ASL(); uint8_t BCC();
    uint8_t BCS(); uint8_t BEQ(); uint8_t BIT(); uint8_t BMI();
    uint8_t BNE(); uint8_t BPL(); uint8_t BRK(); uint8_t BVC();
    uint8_t BVS(); uint8_t CLC(); uint8_t CLD(); uint8_t CLI();
    uint8_t CLV(); uint8_t CMP(); uint8_t CPX(); uint8_t CPY();
    uint8_t DEC(); uint8_t DEX(); uint8_t DEY(); uint8_t EOR();
    uint8_t INC(); uint8_t INX(); uint8_t INY(); uint8_t JMP();
    uint8_t JSR(); uint8_t LDA(); uint8_t LDX(); uint8_t LDY();
    uint8_t LSR(); uint8_t NOP(); uint8_t ORA(); uint8_t PHA();
    uint8_t PHP(); uint8_t PLA(); uint8_t PLP(); uint8_t ROL();
    uint8_t ROR(); uint8_t RTI(); uint8_t RTS(); uint8_t SBC();
    uint8_t SEC(); uint8_t SED(); uint8_t SEI(); uint8_t STA();
    uint8_t STX(); uint8_t STY(); uint8_t TAX(); uint8_t TAY();
    uint8_t TSX(); uint8_t TXA(); uint8_t TXS(); uint8_t TYA();

    uint8_t XXX();
};

} // namespace fc_emulator