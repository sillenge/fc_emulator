#include <cassert>

#include "ppu.h"
#include "bus.h"

namespace fc_emulator {

// 默认背景色地址
constexpr uint16_t kAddrBasePallete = 0x3F00;
constexpr uint16_t kAddrBaseNametable = 0x2000;
constexpr uint16_t kAddrBaseAttribute = kAddrBaseNametable | 0x3C0;
// 
void PPU::reset() {

	ctrl_.reg = 0x00;
	mask_.reg = 0x00;
	status_.reg = 0x00;
	oamAddr_ = 0x00;
	oamData_ = 0x00;
	scroll_ = 0x00;
	ppuAddr_ = 0x00;
	ppuData_ = 0x00;
	bFirstWrite = true;
	ppuDataBuffer_ = 0x00;
	vramAddr_.reg = 0x00;
	tramAddr_.reg = 0x00;
	fineXScroll_ = 0x00;
    scanline_ = -1;
    cycle_ = 0;
	renderCtx_ = { 0 };
	bFrameComplete_ = false;
	nmiTriggered_ = false;
	memset(palette_.data(), 0, palette_.size());
	memset(oam_.data(), 0, oam_.size());
	memset(vram_.data(), 0, vram_.size());

	bus_->mappingPPUNametables(vram_.data());
}

void PPU::connectBus(Bus* bus) {
	bus_ = bus;
}

// 一个时钟周期
void PPU::clock() {
	// 处理奇帧跳过（仅当渲染开启）
	if (scanline_ == 0 && cycle_ == 0 && renderingEnabled()) {
		cycle_ = 1;
	}

	if (cycle_ == 0) {
		renderCtx_.evaluateSprites(*this);
	}

	// 根据扫描线确定阶段
	switch (getCurClockState()) {
	case PPUClockState::PreRender:   
		handlePreRenderLine();   
		break;
	case PPUClockState::Visible:     
		handleVisibleLine();     
		break;
	case PPUClockState::PostRender:  
		handlePostRenderLine();  
		break;
	case PPUClockState::VBlank:      
		handleVBlankLine();      
		break;
	}

	// 像素合成（始终进行，但只在可见区域写入帧缓冲）
	composePixel();

	// 更新周期/扫描线
	advanceCycle();
}

// 当当前帧未就绪时，返回空指针
std::optional<FrameBuffer> PPU::getFrameBuffer() {
	if (!bFrameComplete_) {
		return std::nullopt;
	}
	bFrameComplete_ = false;
	return frameBuffer_;
}

// 注册回调函数，用于处理一帧准备好后的渲染事项
void PPU::regFrameReadyCallback(FnCallback callback) {
	fnFrameCallback_ = callback;
}

// CPU 通过 BUS 访问，读取PPU寄存器
uint8_t PPU::cpuRead(uint16_t addr) {
	uint8_t data = 0x00;
	switch (addr)
	{
		// PPU Control regs
	case PPURegister::PPU_CTRL:
		// 只写寄存器
	case PPURegister::PPU_MASK:
		// 只写寄存器
		assert(!"Write Only");
		break;
	case PPURegister::PPU_STATUS:
		// 只读状态寄存器
		data = status_.reg;
		// 读取后清除 VBlank 状态
		status_.vblank = 0;
		break;
	case PPURegister::OAM_ADDR:
		// 只写寄存器
        assert(!"Write Only");
		break;
	case PPURegister::OAM_DATA:
		// 读写寄存器
		data = oam_[oamAddr_];
		break;
	case PPURegister::PPU_SCROLL:
	case PPURegister::PPU_ADDR:
		// 双写寄存器
		assert(!"Write Only");
		break;
	case PPURegister::PPU_DATA:
		// VRAM 读写端口
		data = bus_->PPURead(addr);
		// 在非调色版区，预读一个数据缓冲到
		// 由于PPU有概率（读地址需要2/3CPU周期）不能在一个CPU周期里读取出数据
		// 但CPU无法等待PPU完成VRAM的读取，所以需要预读到一个缓存里
		if (addr < 0x3F00) {
			std::swap(ppuDataBuffer_, data);
		}
		vramAddr_.reg += (uint16_t)((ctrl_.increment) ? 0x20: 0x01);
		break;
	default:
		break;
	}
	return data;
}

// CPU 通过 BUS 访问，写入PPU寄存器
void PPU::cpuWrite(uint16_t addr, uint8_t data) {
	switch (addr)
	{
		// PPU Control regs
	case PPURegister::PPU_CTRL:
		// 只写寄存器
		ctrl_.reg = data;
		tramAddr_.nametableX = ctrl_.nametableX;
		tramAddr_.nametableY = ctrl_.nametableY;
		break;
	case PPURegister::PPU_MASK:
		// 只写寄存器
		mask_.reg = data;
		break;
	case PPURegister::PPU_STATUS:
		// 只读状态寄存器
		assert(!"Read Only!");
		break;
	case PPURegister::OAM_ADDR:
		// 只写寄存器
		oamAddr_ = data;
		break;
	case PPURegister::OAM_DATA:
		// 读写寄存器
        oam_[oamAddr_] = data;
		break;
	case PPURegister::PPU_SCROLL:
		// data 低3位是精调X，高5位是粗调X
		// 其中fineX单独一个寄存器: .....xxx
		// ..yyynnYY YYYXXXXX
		if (bFirstWrite) {
			// tramAddr:....... ...XXXXX = XXXXX...
			// fineX:	.....xxx = .....xxx
			fineXScroll_ = data & 0x07;
			tramAddr_.coarseX = data >> 3;
			bFirstWrite = false;
		}
		else { 
			// tramAddr:.....YY YYY..... = YYYYY...
			// tramAddr:..yyy.. ........ = .....yyy
			tramAddr_.fineY = data & 0x07;
			tramAddr_.coarseY = data >> 3;
			bFirstWrite = true;
		}
		break;
	case PPURegister::PPU_ADDR:
		// 双写寄存器
		if (bFirstWrite) {
			// 写入高位地址，但0x3FFF地址中只有14位有效地址
			tramAddr_.upper = data & 0x3F;
			bFirstWrite = false;
		}
		else {
			tramAddr_.lower = data;
			vramAddr_ = tramAddr_;
			bFirstWrite = true;
		}
		break;
	case PPURegister::PPU_DATA:
		// VRAM 读写端口
		bus_->PPUWrite(vramAddr_.reg, data);
		vramAddr_.reg += (uint16_t)((ctrl_.increment == true) ? 0x20 : 0x01);
		break;
	default:
		assert(!"Wrong ppu.cpuWrite addr!");
		break;
	}
}


// 读取一个palette RAM，这里存储了即时像素点的索引
uint8_t PPU::readPalette(uint16_t addr) const{
	assert(((addr >= 0x3F00)  || (addr <= 0x3FFF)) && "Read address out of range.");
	return palette_[addr & static_cast<uint16_t>(0x001F)];
}

// 这里采用了镜像写入，在读取的时候就可以不作区分了
void PPU::writePalette(uint16_t addr, uint8_t data) {
	assert(((addr >= 0x3F00) || (addr <= 0x3FFF)) && "Read address out of range.");
	if (addr & 0x3) {
		// 独立的区域
		palette_[addr & static_cast<uint16_t>(0x001F)] = data;
	}
	else {
		// 背景区，有镜像 0x3F00 0x3F04 0x3F08 0x3F0C
		addr &= static_cast<uint16_t>(0x000F);
		palette_[addr] = data;
		palette_[addr | static_cast<uint16_t>(0x0010)] = data;
	}
}

/*-------------------------------------背景渲染-------------------------------------------
 参考：https://zhuanlan.zhihu.com/p/599870247 ，一定要看
 关于背景渲染
 首先我们需要渲染得到一个256*240像素(pixel)的帧(frame)
 由于早起内存极其有限，这个帧被拆分成了32*30个(8*8)的小块(960个tile)，这些小块被nametable管理
 在BUS::PPURead的注释里写道，名称表有四个，也就是0x2000-0x2FFF
 而32*30 = 960 byte，所以剩下的1024 - 960 = 64成了属性表(attribute table)
 由于64个属性表无法对应960个tile，所以每个属性表需要对应4*4 = 16个tile
 
 总的来说就是先到 nametable 找 tile ，这个 tile 是一个 pattern table 的索引
 再通过更精细的坐标找到8*8 tile 里的像素点，这个点现在颜色还没有确定
 所以通过属性表找到对应的颜色选择子，通过这个选择子去PPU调色板里找对应的颜色
 最后把这个颜色的像素点pixel渲染到屏幕上 */
	
// 获取当前clock状态
PPUClockState PPU::getCurClockState() const {
	if (scanline_ == -1) return PPUClockState::PreRender;
	if (scanline_ < 240) return PPUClockState::Visible;
	if (scanline_ == 240) return PPUClockState::PostRender;
	/*if (241 <= scanline_ && scanline_ <= 260)*/
	return PPUClockState::VBlank;
}

// 渲染使能，如果都是false就不用渲染了
bool PPU::renderingEnabled() const {
	return mask_.showBackground || mask_.showsSprites;
}

// 可视像素，如果是false就不用渲染了(但是私底下还有好多活要做)
bool PPU::isVisiblePixel() const {
	return scanline_ >= 0 && scanline_ < 240 && cycle_ >= 1 && cycle_ <= 256;
}

// 
RGBA PPU::getColorFromPalette(uint8_t paletteSelector, uint8_t paletteIndex) {
	uint16_t addr = kAddrBasePallete | ((paletteSelector << 2) | (paletteIndex & 0x03));
	uint8_t colorIndex = readPalette(addr);
	return kBasePalette_[colorIndex & 0x3F];
}


fc_emulator::RGBA PPU::getColorFromPalette(uint8_t paletteColor) {
    uint16_t addr = kAddrBasePallete | (paletteColor & 0x3F);
    uint8_t colorIndex = readPalette(addr);
    return kBasePalette_[colorIndex & 0x3F];
}

olc::Sprite& PPU::GetPatternTable(uint8_t tableIndex, uint8_t paletteIdx) {
    // 图案表大小为 128x128 像素（ 每个 tile 8x8，共 16x16 个 tile ）
    static olc::Sprite sprite[2]{ {128, 128}, {128, 128} };
    uint16_t baseAddr = (tableIndex == 0) ? 0x0000 : 0x1000;

    for (int ty = 0; ty < 16; ++ty) {
        for (int tx = 0; tx < 16; ++tx) {
            int tileId = ty * 16 + tx;
            // 每个 tile 占 16 字节（8 字节低位 + 8 字节高位）
            uint16_t tileAddr = baseAddr + tileId * 16;

            for (int py = 0; py < 8; ++py) {
                uint8_t lsb = bus_->PPURead(tileAddr + py);
                uint8_t msb = bus_->PPURead(tileAddr + py + 8);
                for (int px = 0; px < 8; ++px) {
                    uint8_t bit = 7 - px;
                    uint8_t pixel = ((lsb >> bit) & 1) | (((msb >> bit) & 1) << 1);
                    // 根据调色板索引获取实际颜色
                    RGBA color = getColorFromPalette(paletteIdx, pixel);
                    sprite[tableIndex].SetPixel(tx * 8 + px, ty * 8 + py,
                        olc::Pixel(color.r, color.g, color.b, 255));
                }
            }
        }
    }
    return sprite[tableIndex];
}

// 预渲染周期
void PPU::handlePreRenderLine() {
    // 清除 VBlank
    if (cycle_ == 1) {
        status_.vblank = 0;
        // 清除帧就绪标记，准备新一轮的渲染
        bFrameComplete_ = false;
    }
    // 取指周期（与可见行相同，但不输出像素）
    if ((cycle_ >= 2 && cycle_ < 258) || (cycle_ >= 321 && cycle_ < 338)) {
        renderCtx_.updateShifters(*this);
        switch ((cycle_ - 1) & 0x7) {
        case 0:
            renderCtx_.loadShifters();
            renderCtx_.fetchTileId(*this);
            break;
        case 2:	renderCtx_.fetchTileAttrib(*this);	break;
        case 4: renderCtx_.fetchTileLsb(*this);		break;
        case 6: renderCtx_.fetchTileMsb(*this);		break;
        case 7: renderCtx_.incrementScrollX(*this); break;
        default: break;
        }
    }

    // 周期 256：垂直滚动递增
    if (cycle_ == 256) {
        renderCtx_.incrementScrollY(*this);
    }

    // 周期 257：加载移位器并水平地址传输
    if (cycle_ == 257) {
        renderCtx_.loadShifters();
        renderCtx_.transferAddressX(*this);
    }

    // 周期 338 和 340：空读取
    if (cycle_ == 338 || cycle_ == 340) {
        renderCtx_.fetchTileIdDummy(*this);
    }

    // 周期 280-305：垂直地址传输
    if (cycle_ >= 280 && cycle_ < 305) {
        renderCtx_.transferAddressY(*this);
    }
}

void PPU::handleVisibleLine() {
    // 取指周期
    if ((cycle_ >= 2 && cycle_ < 258) || (cycle_ >= 321 && cycle_ < 338)) {
        renderCtx_.updateShifters(*this);
        switch ((cycle_ - 1) & 0x7) {
        case 0:
            renderCtx_.loadShifters();
            renderCtx_.fetchTileId(*this);
            break;
        case 2:	renderCtx_.fetchTileAttrib(*this);	break;
        case 4: renderCtx_.fetchTileLsb(*this);		break;
        case 6: renderCtx_.fetchTileMsb(*this);		break;
        case 7: renderCtx_.incrementScrollX(*this); break;
        default: break;
        }
    }

    // 周期 256：垂直滚动递增
    if (cycle_ == 256) {
        renderCtx_.incrementScrollY(*this);
    }

    // 周期 257：加载移位器并水平地址传输
    if (cycle_ == 257) {
        renderCtx_.loadShifters();
        renderCtx_.transferAddressX(*this);
    }

    // 周期 338 和 340：空读取
    if (cycle_ == 338 || cycle_ == 340) {
        renderCtx_.fetchTileIdDummy(*this);
    }
}


void PPU::handlePostRenderLine() {
    // 暂无后渲染行无操作
}

void PPU::handleVBlankLine() {
    if (scanline_ == 241 && cycle_ == 1) {
        status_.vblank = 1;
        if (ctrl_.nmiEnable) {
            //nmiTriggered_ = true;
            // 直接回调，不再设置信号
            bus_->triggerNmi();
        }
    }
}

void PPU::composePixel() {
    if (!isVisiblePixel()) return;

    // ----- 背景部分（原有代码）-----
    uint8_t bgPaletteSelector = 0;
    uint8_t bgColorIndex = 0;
    if (cycle_ <= 8 && !mask_.showBackgroundLeft) {
        bgPaletteSelector = 0;
        bgColorIndex = 0;
    }
    else if (mask_.showBackground) {
        // 选择子选色域，索引选颜色，每个色域四个颜色
		// 背景调色板色域起始地址:（对应地址 $3F00、$3F04、$3F08、$3F0C）
        uint16_t bitMux = 0x8000 >> fineXScroll_;
        bgPaletteSelector = ((renderCtx_.bgShifterAttributeUpper_.data & bitMux) ? 2 : 0)
            | ((renderCtx_.bgShifterAttributeLower_.data & bitMux) ? 1 : 0);
        bgColorIndex = ((renderCtx_.bgShifterPatternUpper_.data & bitMux) ? 2 : 0)
            | ((renderCtx_.bgShifterPatternLower_.data & bitMux) ? 1 : 0);
    }
    bool bgOpaque = (bgColorIndex != 0);

    // ----- 精灵混合 -----
    int xPixel = cycle_ - 1;
    SpriteBlendResult blend = renderCtx_.blendSprites(*this, xPixel, bgOpaque);
	renderCtx_.checkSprite0Hit(*this, blend, bgOpaque);

    // ----- 最终颜色 -----
    uint8_t finalSelector = bgPaletteSelector;
    uint8_t finalIndex = bgColorIndex;
    if (blend.spriteUsed) {
        finalSelector = blend.paletteSelector;
        finalIndex = blend.colorIndex;
    }

    RGBA pixelColor = getColorFromPalette(finalSelector, finalIndex);
    frameBuffer_[(scanline_ * kScreenWidth) + (cycle_ - 1)] = pixelColor;
}

// 下一个时钟周期
void PPU::advanceCycle() {
    cycle_++;
    if (cycle_ > 340) {
        cycle_ = 0;
        scanline_++;
        if (scanline_ > 260) {
            scanline_ = -1;
            bFrameComplete_ = true;
            // 渲染完成发出信号
            if (fnFrameCallback_) fnFrameCallback_();
        }

    }
}

// 水平滚动
// 参考：https://zhuanlan.zhihu.com/p/599870247 视窗位置的计算
// 用于确认下一个pixel的位置，不需要对fineX进行自增，fineX被用作tile内的初始列偏移，像素点偏移通过位移寄存器实现
// 这实现了屏幕的水平移动，一帧(frame)由32clumn*30row个块(tile)组成，每块由(8*8)个像素点(pixel)组成
// 当屏幕横向滚动时，|AAAAAAAA| BBBBBBBB ==> C|AAAAAAA B|BBBBBBB ==> CCC|AACCC BBB|BBBBB ==> CCCCCCCC |BBBBBBBB|
// ==> C|CCCCCC D|BBBBBBB 此时渲染的是 BBBBBBB C，而CCCC是刷新的帧，刷新的 nametable_C 被渲染显示了，而D是新的列
// A 和 B 是两个nametable，它们共存于内存之中，我们横向向右滚动时只需要将起始指针右移一个位即可
// (同时，我们向右移动后，刚被移出屏幕的列就会被刷新，**但不在这个函数里实现**，C: 代表以tile为单位的一列被刷新了)
void PPU::PPURenderContext::incrementScrollX(PPU& ppu) {
	if (ppu.renderingEnabled()) {
		// 0-31 tile 共计32个tile，coarseX=31时自增得 0
		if (ppu.vramAddr_.coarseX < 31) {
			ppu.vramAddr_.coarseX++;
		}
		else {
			ppu.vramAddr_.coarseX = 0;
			ppu.vramAddr_.nametableX = (~ppu.vramAddr_.nametableX) & 1;
		}
		// 这种写法虽然看起来非常简洁高效，但如果出了BUG将非常难查
		//ppu.vramAddr_.coarseX++;
		//if (ppu.vramAddr_.coarseX == 0) {
		//	ppu.vramAddr_.nametableX = (~ppu.vramAddr_.nametableX) & 1;
		//}
	}
}

// 垂直滚动
void PPU::PPURenderContext::incrementScrollY(PPU& ppu) {
	if (ppu.renderingEnabled()) {
		if (ppu.vramAddr_.fineY < 7) {
			ppu.vramAddr_.fineY++;
		}
		else {
			ppu.vramAddr_.fineY = 0;
			if (ppu.vramAddr_.coarseY == 29) {
				ppu.vramAddr_.coarseY = 0;
				ppu.vramAddr_.nametableY = ~ppu.vramAddr_.nametableY & 1;
			}
			else if (ppu.vramAddr_.coarseY == 31) {
				ppu.vramAddr_.coarseY = 0;
			}
			else {
				ppu.vramAddr_.coarseY++;
			}
		}
	}
}

// 硬件换行时调用，载入预渲染的数据到vramAddr_里
void PPU::PPURenderContext::transferAddressX(PPU& ppu) {
	if (ppu.renderingEnabled()) {
		ppu.vramAddr_.nametableX = ppu.tramAddr_.nametableX;
		ppu.vramAddr_.coarseX = ppu.tramAddr_.coarseX;
	}
}

// 硬件换帧时调用，载入预渲染的数据到vramAddr_里
void PPU::PPURenderContext::transferAddressY(PPU& ppu) {
	if (ppu.renderingEnabled()) {
		ppu.vramAddr_.fineY = ppu.tramAddr_.fineY;
		ppu.vramAddr_.nametableY = ppu.tramAddr_.nametableY;
		ppu.vramAddr_.coarseY = ppu.tramAddr_.coarseY;
	}
}


// 移位寄存器现在包含两个 tile 的低位数据：高 8 位是旧 tile, 低 8 位是新 tile
// 后续每个时钟周期左移一位，新 tile 的数据会逐渐移入高 8 位并输出，实现流水线式渲染
void PPU::PPURenderContext::loadShifters() {
	// 移位寄存器现在包含两个 tile 的低位数据：高 8 位是旧 tile, 低 8 位是新 tile
	// 后续每个时钟周期左移一位，新 tile 的数据会逐渐移入高 8 位并输出，实现流水线式渲染
	bgShifterPatternLower_.lower = nextTileLsb_;
	// 与低位寄存器同步，确保每个像素的高低位同时输出，组合成 2 位像素值
	bgShifterPatternUpper_.lower = nextTileMsb_;

	// 属性数据每个 tile 只有 2 位，但移位寄存器需要为每个像素输出一位属性。
	// 通过将属性位扩展到整个 8 位，使得在后续左移过程中，每个时钟周期移出的位正好对应 tile 内每个像素的属性值
	// （全 1 则每个像素的属性低位为 1，全 0 则为 0）。这模拟了硬件中属性在整个 tile 内保持不变的特性
	bgShifterAttributeLower_.lower = (nextTileAttribute_ & 0x01 ? 0xFF : 0x00);
	// 与低位属性寄存器配合，最终每个像素的属性值由这两个移位寄存器当前移出的位组合而成
	// （{ attribHi_bit, attribLo_bit }），用于从调色板中选择颜色。
	bgShifterAttributeUpper_.lower = (nextTileAttribute_ & 0x02 ? 0xFF : 0x00);
}

// 每个时钟周期左移一位，新 tile 的数据会逐渐移入高 8 位并输出，实现流水线式渲染
void PPU::PPURenderContext::updateShifters(const PPU& ppu) {
	if (ppu.mask_.showBackground) {
		bgShifterPatternLower_.data <<= 1;
		bgShifterPatternUpper_.data <<= 1;
		bgShifterAttributeLower_.data <<= 1;
		bgShifterAttributeUpper_.data <<= 1;
	}
}

// 读下一个tile的索引
void PPU::PPURenderContext::fetchTileId(PPU& ppu) {
	nextTileId = ppu.bus_->PPURead(0x2000 | (ppu.vramAddr_.reg & 0x0FFF));
}

// 读一次VRAM，浪费一下时间，保持时序上的正确
void PPU::PPURenderContext::fetchTileIdDummy(PPU& ppu) {
	ppu.bus_->PPURead(0x2000 | (ppu.vramAddr_.reg & 0x0FFF));
}


void PPU::PPURenderContext::evaluateSprites(PPU& ppu)
{
    ppu.renderCtx_.lineSpriteCount = 0;
    ppu.renderCtx_.overflowFlag = false;

    int height = ppu.ctrl_.spriteSize ? 16 : 8;

    for (int i = 0; i < 64; ++i) {
        uint8_t y = ppu.oam_[i * 4 + 0];
        uint8_t tile = ppu.oam_[i * 4 + 1];
        uint8_t attr = ppu.oam_[i * 4 + 2];
        uint8_t x = ppu.oam_[i * 4 + 3];

        // 判断是否在当前扫描线内（scanline_ + 1 与 Y 比较）
        if (ppu.scanline_ + 1 < y || ppu.scanline_ + 1 >= y + height) continue;

        if (ppu.renderCtx_.lineSpriteCount == 8) {
            ppu.renderCtx_.overflowFlag = true;
            continue;
        }

        // 计算行内偏移（考虑垂直翻转）
        int row = (ppu.scanline_ + 1) - y;
        if (attr & 0x80) row = (height - 1) - row;

        uint8_t actualTile = tile;
        uint8_t rowInTile = row;
        if (ppu.ctrl_.spriteSize) { // 8x16 模式
            actualTile = tile & 0xFE;
            if (row >= 8) {
                actualTile |= 1;
                rowInTile = row - 8;
            }
        }

        auto& ls = ppu.renderCtx_.lineSprites[ppu.renderCtx_.lineSpriteCount];
        ls.tileIndex = actualTile;
        ls.rowInTile = rowInTile;
        ls.x = x;
        ls.palette = attr & 0x03;
        ls.hFlip = (attr & 0x40) != 0;
        ls.priority = (attr & 0x20) != 0;
        ls.oamIndex = i;
        ppu.renderCtx_.lineSpriteCount++;
    }

	ppu.status_.spriteOverflow = ppu.renderCtx_.overflowFlag ? 1 : 0;
}


uint8_t PPU::PPURenderContext::fetchSpritePixel(PPU& ppu, const LineSprite& sprite, int offsetX) {
    if (offsetX < 0 || offsetX >= 8) return 0;
    if (sprite.hFlip) offsetX = 7 - offsetX;

    uint16_t tileAddr = (ppu.ctrl_.spriteTable << 12) + (sprite.tileIndex << 4) + sprite.rowInTile;
    uint8_t lsb = ppu.bus_->PPURead(tileAddr);
    uint8_t msb = ppu.bus_->PPURead(tileAddr + 8);

    uint8_t bit = 7 - offsetX;
    return ((lsb >> bit) & 1) | (((msb >> bit) & 1) << 1);
}


fc_emulator::SpriteBlendResult PPU::PPURenderContext::blendSprites(PPU& ppu, int x, bool bgOpaque) {
    SpriteBlendResult result = { false, 0, 0, -1 };

    for (int i = 0; i < ppu.renderCtx_.lineSpriteCount; ++i) {
        const auto& ls = ppu.renderCtx_.lineSprites[i];
        if (x < ls.x || x >= ls.x + 8) continue;

        int offsetX = x - ls.x;
        uint8_t pixel = fetchSpritePixel(ppu, ls, offsetX);
        if (pixel == 0) continue; // 透明

        // 优先级判断
        if (ls.priority && bgOpaque) {
            // 背景后且背景不透明 → 显示背景，不使用该精灵
            continue;
        }

        // 该精灵被采用
        result.spriteUsed = true;
        result.paletteSelector = ls.palette;
        result.colorIndex = pixel;
        result.oamIndex = ls.oamIndex;
        break;
    }
    return result;
}


void PPU::PPURenderContext::checkSprite0Hit(PPU& ppu, const SpriteBlendResult& result, bool bgOpaque) {
    if (result.spriteUsed && result.oamIndex == 0 && bgOpaque) {
        ppu.status_.sprite_0_Hit = 1;
    }
}

// 由于一个attribute需要管理4*4个tile，暂且称为 block(4*4 tile)
// 每个block被分为四个部分右下、左下、右上、左上
// nextTileAttribute_: RDRDLDLD RURULULU (U:up D:down L:left R:right)
// 所以一个属性选择子uint8_t可以被切成四瓣使用
// 四瓣的每一份可以一个四色的调色板使用（0-3 4-7 8-11 12-15）
// 这个后续会被loadShifters扩散到一个位移流水寄存器里
void PPU::PPURenderContext::fetchTileAttrib(PPU& ppu) {
	uint16_t addr = kAddrBaseAttribute
		| (ppu.vramAddr_.nametableY << 11)
		| (ppu.vramAddr_.nametableX << 10)
		| ((ppu.vramAddr_.coarseY >> 2) << 3)
		| (ppu.vramAddr_.coarseX >> 2);
	nextTileAttribute_ = ppu.bus_->PPURead(addr);
	
	// 判断当前 tile 位于块的上半部分（0）还是下半部分（1）
	if (ppu.vramAddr_.coarseY & 0x02) nextTileAttribute_ >>= 4;
	// 判断当前 tile 位于块的左半部分（0）还是右半部分（1）
	if (ppu.vramAddr_.coarseX & 0x02) nextTileAttribute_ >>= 2;
	nextTileAttribute_ &= 0x03;
}
// 每个 tile 的一行 占 16 字节，8 字节低位 + 8 字节高位
// 一高一低的组合，刚好可以选一个四色调色盘里的四种颜色 （0 1 2 3）
// attribute | (Lsb | Msb)正好可以选出一个颜色
void PPU::PPURenderContext::fetchTileLsb(PPU& ppu) {
	uint16_t addr = (ppu.ctrl_.backgroundTable << 12) // 0x0000 或 0x1000
		+ ((uint16_t)nextTileId << 4) //定位到该 tile 在图案表中的起始位置
		+ ppu.vramAddr_.fineY; //加上当前行偏移（0~7），得到该 tile 当前行的低位字节地址
	nextTileLsb_ = ppu.bus_->PPURead(addr);
}

void PPU::PPURenderContext::fetchTileMsb(PPU& ppu) {
	uint16_t addr = (ppu.ctrl_.backgroundTable << 12) 
		+ ((uint16_t)nextTileId << 4)	
		+ ppu.vramAddr_.fineY + 8; // +8跳过nextTileLsb_
	nextTileMsb_ = ppu.bus_->PPURead(addr);
}





} // namespace fc_emulator