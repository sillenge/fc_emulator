#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <functional>
#include "logger.h"
#include "olcPixelGameEngine.h"
#include <optional>


namespace fc_emulator {

// 渲染相关
constexpr int kScreenWidth = 256;
constexpr int kScreenHeight = 240;

using FnCallback = std::function<void()>;

class Bus;

// PPU寄存器地址（CPU视角）
enum PPURegister {
	PPU_CTRL = 0x2000,   // 写
	PPU_MASK = 0x2001,   // 写
	PPU_STATUS = 0x2002,   // 读
	OAM_ADDR = 0x2003,   // 写
	OAM_DATA = 0x2004,   // 读写
	PPU_SCROLL = 0x2005,   // 写
	PPU_ADDR = 0x2006,   // 写
	PPU_DATA = 0x2007    // 读写
};

// PPU控制寄存器1 (PPUCTRL) 位定义
union PPUCtrl {
	struct {
		uint8_t nametableX : 1;  // Y+X 基础名称表地址 (0=$2000,1=$2400,2=$2800,3=$2C00)
		uint8_t nametableY : 1;  // Y+X 基础名称表地址 (0=$2000,1=$2400,2=$2800,3=$2C00)
		uint8_t increment : 1;  // VRAM地址增量 (0: +1, 1: +32)
		uint8_t spriteTable : 1;  // 精灵图案表地址 (0:$0000, 1:$1000)
		uint8_t backgroundTable : 1; // 背景图案表地址 (0:$0000, 1:$1000)
		uint8_t spriteSize : 1;  // 精灵大小 (0:8x8, 1:8x16)
		uint8_t masterSlave : 1;  // 未使用（FC中恒为0）
		uint8_t nmiEnable : 1;  // 垂直空白时是否产生NMI
	};
	uint8_t reg;
};

// PPU控制寄存器2 (PPUMASK) 位定义
union PPUMask {
	struct {
		uint8_t grayscale : 1;  // 灰度模式 (0:彩色, 1:灰度)
		uint8_t showBackgroundLeft : 1; // 最左8像素背景是否显示
		uint8_t showSpritesLeft : 1;    // 最左8像素精灵是否显示
		uint8_t showBackground : 1;     // 是否显示背景
		uint8_t showsSprites : 1;      // 是否显示精灵
		uint8_t emphasizeRed : 1;      // 强调红色
		uint8_t emphasizeGreen : 1;      // 强调绿色
		uint8_t emphasizeBlue : 1;      // 强调蓝色
	};
	uint8_t reg;
};

// PPU状态寄存器 (PPUSTATUS) 位定义
union PPUStatus {
	struct {
		uint8_t openBus : 5;  // 未使用位（总是读为0）
		uint8_t spriteOverflow : 1; // 精灵溢出（一帧超过8个）
		uint8_t sprite_0_Hit : 1;    // 精灵0命中
		uint8_t vblank : 1;    // 垂直空白标志（写入0清除）
	};
	uint8_t reg;
};

// yyy nn YYYYY XXXXX 
// 其中y：fine_y，n：nametable，Y：coarse_y，X：coarse_x
union VRAMAddr {
	struct {
		uint16_t coarseX : 5;
		uint16_t coarseY : 5;
		uint16_t nametableX : 1;
		uint16_t nametableY : 1;
		uint16_t fineY : 3;
		uint16_t unused : 1;
	};
	struct {
		uint8_t lower;
		uint8_t upper;
	};
	uint16_t reg;
};

union Uint8x2 {
	struct {
		uint8_t lower;
		uint8_t upper;
	};
	uint16_t data;

	Uint8x2(uint16_t data) : data(data) {}
};

union RGBA {
	struct { uint8_t r, g, b, a; };
	uint32_t    data;

	constexpr RGBA() : data(0) {}
	constexpr RGBA(const uint32_t val) : data(val) {}
	constexpr RGBA(const uint8_t red, const uint8_t green, const uint8_t blue, const uint8_t alpha) :
		r(red), g(green), b(green), a(alpha) {}
};
using FrameBuffer = std::array<RGBA, kScreenWidth* kScreenHeight>;

struct LineSprite {
	uint8_t tileIndex;   // 实际使用的图块索引（8x16模式下可能是偶数或奇数）
	uint8_t rowInTile;   // 在当前 tile 内的行偏移（0-7）
	uint8_t x;           // 精灵 X 坐标
	uint8_t palette;     // 调色板索引（0-3）
	bool hFlip;          // 水平翻转
	bool priority;       // 优先级（0=前景，1=背景后）
	uint8_t oamIndex;    // 原始 OAM 索引（用于精灵0碰撞）
};

struct SpriteBlendResult {
    bool spriteUsed;
    uint8_t paletteSelector;
    uint8_t colorIndex;
    int oamIndex;  // 最终使用的精灵的 OAM 索引，用于碰撞检测
};

enum class PPUClockState {
	PreRender,   // scan line: -1
	Visible,     // scan line: 0-239
	PostRender,  // scan line: 240
	VBlank       // scan line: 241-260
};

// 调色板内存（ [0x3F00-0x3F20），32字节，但只有前16字节用于背景和精灵，后16为镜像）
using PaletteRAM = std::array<uint8_t, 32>;
// OAM内存（256字节，64个精灵，每个4字节）
using OAMMemory = std::array<uint8_t, 256>;
// 显存（2KB，CPU通过PPU访问，用于名称表和属性表）
using VRAM = std::array<uint8_t, 2048>;

class PPU {
public:
	//PPU() = default;
	//~PPU() = default;

	// 重置
	void reset();

	// 总线连接
	void connectBus(Bus* bus);
	bool checkBusReady() const { return bus_ != nullptr; }
	void regFrameReadyCallback(FnCallback callback);
	inline bool isFrameComplete() { return bFrameComplete_; }
public:
	// CPU 读写 PPU 寄 存器（通过总线调用）
	uint8_t cpuRead(uint16_t addr);
	void cpuWrite(uint16_t addr, uint8_t data);

	// PPU读写调色板
	uint8_t readPalette(uint16_t addr) const;   // 调色板读取（处理镜像）
	void writePalette(uint16_t addr, uint8_t data); // 调色板写入

	// 时钟驱动（每个PPU时钟周期调用一次，通常3倍于CPU频率）
	void clock();

	// 获取当前扫描线/周期（用于调试）
	int getScanline() const { return scanline_; }
	int getCycle() const { return cycle_; }

	// 获取当前帧缓冲区（用于渲染）
	RGBA getColorFromPalette(uint8_t paletteSelector, uint8_t paletteIndex);
	RGBA getColorFromPalette(uint8_t paletteColor);
	olc::Sprite& GetPatternTable(uint8_t tableIndex, uint8_t paletteIdx);
	std::optional<FrameBuffer> getFrameBuffer();

private:
	// -----------------渲染相关---------------------
	bool renderingEnabled() const;
	bool isVisiblePixel() const;

	PPUClockState getCurClockState() const;
	void handlePreRenderLine();
	void handleVisibleLine();
	void handlePostRenderLine();
	void handleVBlankLine();

	void composePixel();
	void advanceCycle();
private:
	// 内部寄存器
	PPUCtrl  ctrl_;        // $2000
	PPUMask  mask_;        // $2001
	PPUStatus status_;     // $2002
	uint8_t  oamAddr_;     // $2003  OAM地址（写）
	uint8_t  oamData_;     // $2004  OAM数据（读写）
	uint8_t  scroll_;      // $2005  (双缓冲，需要内部暂存)
	uint16_t ppuAddr_;     // $2006  VRAM地址（写两次）
	uint8_t  ppuData_;     // $2007  VRAM数据（读写）
	// 辅助寄存器
	//uint8_t ppuLatchData_; // $2007  VRAM数据缓存，当读取非调色板数据时，预读一个数据存储
	bool     bFirstWrite;  // $2005/$2006写入切换标志，双写寄存器第一次写入
	uint8_t  ppuDataBuffer_; // $2007  VRAM数据缓存，当读取非调色板数据时，预读一个数据存储

	// 内部地址寄存器（v, t, x, w 等用于精细滚动）
	VRAMAddr vramAddr_;      // 当前VRAM地址（v）
	VRAMAddr tramAddr_;      // 临时VRAM地址（t）
	uint8_t  fineXScroll_;   // 精细X滚动（x）

	// 内存
	PaletteRAM palette_;     // 调色板内存（0x3F00-0x3F1F(32字节)）
	OAMMemory  oam_;         // OAM内存（0x00-0xFF(256字节)）
	VRAM       vram_;        // 2KB VRAM（名称表和属性表）

	
private:
	// 渲染相关
	static constexpr int SCREEN_WIDTH = 256;
	static constexpr int SCREEN_HEIGHT = 240;
	FrameBuffer frameBuffer_; // 32位ARGB像素

	// 扫描线/周期计数器（NTSC制式：262扫描线，每线341周期）
	int scanline_;					// 当前扫描线（0-261，其中0-239为可见，240-260为垂直空白，261为预渲染）
	int cycle_;						// 当前周期（0-340）
	struct PPURenderContext {
	public:	 // 背景
		// 背景移位寄存器（16位，高8位为当前，低8位为下一个）
		Uint8x2 bgShifterPatternLower_ = 0;			// 背景图案低位移位寄存器
		Uint8x2 bgShifterPatternUpper_ = 0;			// 背景图案高位移位寄存器
		Uint8x2 bgShifterAttributeLower_ = 0;		// 背景属性低位移位寄存器
		Uint8x2 bgShifterAttributeUpper_ = 0;		// 背景属性低高移位寄存器

		// 当前正在获取的下一个tile数据
		uint8_t nextTileId = 0;						// 下一个tile ID，16*16个tile
		uint8_t nextTileAttribute_ = 0;				// 下一个tile 属性 （颜色选择子，可选4组调色板0-3 4-7 8-11 12-15）
		uint8_t nextTileLsb_ = 0;					// 下一个tile低位平面 Least Significant Bit plane
		uint8_t nextTileMsb_ = 0;					// 下一个tile高位平面 Most  Significant Bit plane

		// 辅助函数（可以直接操作成员）
		void incrementScrollX(PPU& ppu);
		void incrementScrollY(PPU& ppu);
		void transferAddressX(PPU& ppu);
		void transferAddressY(PPU& ppu);
		void loadShifters();
		void updateShifters(const PPU& ppu);
		// 取指相关函数需要访问PPU内存，可以传引用或使用函数对象
		void fetchTileId(PPU& ppu);
		void fetchTileAttrib(PPU& ppu);
		void fetchTileLsb(PPU& ppu);
		void fetchTileMsb(PPU& ppu);
		void fetchTileIdDummy(PPU& ppu);

	public: // 精灵
        LineSprite lineSprites[8];
        int lineSpriteCount;     // 当前行有效精灵数量
        bool overflowFlag;       // 本行精灵溢出标志（用于设置 $2002 bit5）

		void evaluateSprites(PPU& ppu);
		uint8_t fetchSpritePixel(PPU& ppu, const LineSprite& sprite, int offsetX);
		SpriteBlendResult blendSprites(PPU& ppu, int x, bool bgOpaque);
		void checkSprite0Hit(PPU& ppu, const SpriteBlendResult& result, bool bgOpaque);

    } renderCtx_;

    bool bFrameComplete_;
    FnCallback fnFrameCallback_;
    bool nmiTriggered_;

private:
	// 总线指针
	Bus* bus_;

private:
	// PPU硬编码64色调色板，其中有54中颜色，包括48种彩色和4种灰阶
	static constexpr RGBA kBasePalette_[64] = { 
		{ 0x7F, 0x7F, 0x7F, 0xFF }, { 0x20, 0x00, 0xB0, 0xFF }, { 0x28, 0x00, 0xB8, 0xFF }, { 0x60, 0x10, 0xA0, 0xFF },
		{ 0x98, 0x20, 0x78, 0xFF }, { 0xB0, 0x10, 0x30, 0xFF }, { 0xA0, 0x30, 0x00, 0xFF }, { 0x78, 0x40, 0x00, 0xFF },
		{ 0x48, 0x58, 0x00, 0xFF }, { 0x38, 0x68, 0x00, 0xFF }, { 0x38, 0x6C, 0x00, 0xFF }, { 0x30, 0x60, 0x40, 0xFF },
		{ 0x30, 0x50, 0x80, 0xFF },  { 0x00, 0x00, 0x00, 0xFF }, { 0x00, 0x00, 0x00, 0xFF }, { 0x00, 0x00, 0x00, 0xFF },

		{ 0xBC, 0xBC, 0xBC, 0xFF }, { 0x40, 0x60, 0xF8, 0xFF }, { 0x40, 0x40, 0xFF, 0xFF }, { 0x90, 0x40, 0xF0, 0xFF },
		{ 0xD8, 0x40, 0xC0, 0xFF }, { 0xD8, 0x40, 0x60, 0xFF }, { 0xE0, 0x50, 0x00, 0xFF }, { 0xC0, 0x70, 0x00, 0xFF },
		{ 0x88, 0x88, 0x00, 0xFF }, { 0x50, 0xA0, 0x00, 0xFF }, { 0x48, 0xA8, 0x10, 0xFF }, { 0x48, 0xA0, 0x68, 0xFF },
		{ 0x40, 0x90, 0xC0, 0xFF },  { 0x00, 0x00, 0x00, 0xFF }, { 0x00, 0x00, 0x00, 0xFF }, { 0x00, 0x00, 0x00, 0xFF },

		{ 0xFF, 0xFF, 0xFF, 0xFF }, { 0x60, 0xA0, 0xFF, 0xFF }, { 0x50, 0x80, 0xFF, 0xFF }, { 0xA0, 0x70, 0xFF, 0xFF },
		{ 0xF0, 0x60, 0xFF, 0xFF }, { 0xFF, 0x60, 0xB0, 0xFF }, { 0xFF, 0x78, 0x30, 0xFF }, { 0xFF, 0xA0, 0x00, 0xFF },
		{ 0xE8, 0xD0, 0x20, 0xFF }, { 0x98, 0xE8, 0x00, 0xFF }, { 0x70, 0xF0, 0x40, 0xFF }, { 0x70, 0xE0, 0x90, 0xFF },
		{ 0x60, 0xD0, 0xE0, 0xFF }, { 0x60, 0x60, 0x60, 0xFF },  { 0x00, 0x00, 0x00, 0xFF }, { 0x00, 0x00, 0x00, 0xFF },

		{ 0xFF, 0xFF, 0xFF, 0xFF }, { 0x90, 0xD0, 0xFF, 0xFF }, { 0xA0, 0xB8, 0xFF, 0xFF }, { 0xC0, 0xB0, 0xFF, 0xFF },
		{ 0xE0, 0xB0, 0xFF, 0xFF }, { 0xFF, 0xB8, 0xE8, 0xFF }, { 0xFF, 0xC8, 0xB8, 0xFF }, { 0xFF, 0xD8, 0xA0, 0xFF },
		{ 0xFF, 0xF0, 0x90, 0xFF }, { 0xC8, 0xF0, 0x80, 0xFF }, { 0xA0, 0xF0, 0xA0, 0xFF }, { 0xA0, 0xFF, 0xC8, 0xFF },
		{ 0xA0, 0xFF, 0xF0, 0xFF }, { 0xA0, 0xA0, 0xA0, 0xFF },  { 0x00, 0x00, 0x00, 0xFF }, { 0x00, 0x00, 0x00, 0xFF }
	};
}; // class PPU

} // namespace fc_emulator