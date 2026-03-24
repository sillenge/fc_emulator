#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

#include "cpu.h"
#include "ppu.h"
#include "emulator.h"
#include "logger.h"

class FCDebugApp : public olc::PixelGameEngine {
public:
	FCDebugApp() { 
		sAppName = "FC Emulator Debugger";
		LOG_SET_LEVEL(fc_emulator::LogLevel::LevelInfo);
		bEmulationRun = true;
	}

	bool OnUserCreate() override {
		// 加载 ROM（可以从命令行参数或固定路径）
		std::string romPath = "C:\\Users\\sillengar\\Desktop\\code\\CPP_FC\\demo\\mxt.nes"; // 修改为你的 ROM 路径
		if (!emu.insertCartridge(romPath)) {
			std::cerr << "Failed to load ROM: " << romPath << std::endl;
			return false;
		}
		emu.reset();
		return true;
	}
	void DebugGetKey(bool dbg) {
        auto* cpu = emu.getCPU();

        if (dbg) {
            // 单步控制
            if (GetKey(olc::Key::C).bPressed) {
                emu.stepInstruction();
            }
            else if (GetKey(olc::Key::F).bPressed) {
                emu.stepFrame();
            }
            else if (GetKey(olc::Key::B).bPressed) {
                uint16_t pc = cpu->getPC();
                do {
                    emu.stepInstruction();
                    if (GetKey(olc::Key::C).bPressed) break;
                } while (pc <= cpu->getPC());
            }
            else if (GetKey(olc::Key::N).bPressed) {
                uint16_t pc = cpu->getPC();
                do {
                    emu.stepInstruction();
                    if (GetKey(olc::Key::C).bPressed) break;
                } while (pc >= cpu->getPC());
            }
            else if (GetKey(olc::Key::Z).bPressed) {
                uint16_t pc = cpu->getPC();
                do {
                    emu.stepInstruction();
                    if (GetKey(olc::Key::C).bPressed) break;
                } while (cpu->getPC() >= pc - 0x10);
            }
            else if (GetKey(olc::Key::X).bPressed) {
                uint16_t pc = cpu->getPC();
                do {
                    emu.stepInstruction();
                    if (GetKey(olc::Key::C).bPressed) break;
                } while (cpu->getPC() <= pc + 0x10);
            }
        }
	}

	bool OnUserUpdate(float fElapsedTime) override {
        // 清屏
        Clear(olc::DARK_BLUE);
		//DebugGetKey(false);
        if (GetKey(olc::Key::SPACE).bPressed) {
            bEmulationRun = !bEmulationRun;
        }
        if (GetKey(olc::Key::R).bPressed) {
            emu.reset();
        }
        if (GetKey(olc::Key::P).bPressed) {
            selectedPalette = (selectedPalette + 1) % 8;
        }

		// FPS: 60
        if (bEmulationRun) {
            if (fResidualTime > 0.0f)
                fResidualTime -= fElapsedTime;
            else {
                fResidualTime += (1.0f / 60.0f) - fElapsedTime;
				emu.stepFrame();
            }
        }

		// 处理手柄输入
		HandleControllerInput();

		static olc::Sprite screenSprite(256, 240);
		// 渲染一帧
		std::optional<fc_emulator::FrameBuffer> fb = emu.getFrameBuffer();
		if (fb != std::nullopt) {
			// 由于我们每帧 stepFrame 后都会生成新帧，可以直接显示
			// 将帧缓冲区转换为 olc::Sprite
			memcpy(screenSprite.GetData(), fb.value().data(), 256 * 240 * sizeof(uint32_t));
		}
		DrawSprite({ 0, 0 }, &screenSprite, 2);

		// 绘制调试信息（右侧）
		DrawCpu(516, 2);
		DrawCode(516, 72, 26);

		// 绘制调色板
		DrawPalettes(516, 340);
		// 绘制图案表
		auto* ppu = emu.getPPU();
		DrawSprite(516, 348, &ppu->GetPatternTable(0, selectedPalette));
		DrawSprite(648, 348, &ppu->GetPatternTable(1, selectedPalette));
		

		return true;
	}

private:
	fc_emulator::Emulator emu;
	bool bEmulationRun = false;
    float fResidualTime = 0.0f;

	uint8_t selectedPalette = 0;

	// 辅助函数：格式化十六进制
	std::string hex(uint32_t n, uint8_t d) {
		std::string s(d, '0');
		for (int i = d - 1; i >= 0; i--, n >>= 4)
			s[i] = "0123456789ABCDEF"[n & 0xF];
		return s;
	}

	void DrawCpu(int x, int y) {
		const auto* cpu = emu.getCPU();
		// 获取寄存器
		uint16_t pc = cpu->getRegPC();
		uint8_t a = cpu->getRegA();
		uint8_t xr = cpu->getRegX();
		uint8_t yr = cpu->getRegY();
		uint8_t sp = cpu->getRegSP();
		uint8_t flags = cpu->getRegFlags();

		DrawString(x, y, "STATUS:", olc::WHITE);
		DrawString(x + 64, y, "N", (flags & 0x80) ? olc::GREEN : olc::RED);
		DrawString(x + 80, y, "V", (flags & 0x40) ? olc::GREEN : olc::RED);
		DrawString(x + 96, y, "-", (flags & 0x20) ? olc::GREEN : olc::RED);
		DrawString(x + 112, y, "B", (flags & 0x10) ? olc::GREEN : olc::RED);
		DrawString(x + 128, y, "D", (flags & 0x08) ? olc::GREEN : olc::RED);
		DrawString(x + 144, y, "I", (flags & 0x04) ? olc::GREEN : olc::RED);
		DrawString(x + 160, y, "Z", (flags & 0x02) ? olc::GREEN : olc::RED);
		DrawString(x + 178, y, "C", (flags & 0x01) ? olc::GREEN : olc::RED);

		DrawString(x, y + 10, "PC: $" + hex(pc, 4));
		DrawString(x, y + 20, "A: $" + hex(a, 2) + "  [" + std::to_string(a) + "]");
		DrawString(x, y + 30, "X: $" + hex(xr, 2) + "  [" + std::to_string(xr) + "]");
		DrawString(x, y + 40, "Y: $" + hex(yr, 2) + "  [" + std::to_string(yr) + "]");
		DrawString(x, y + 50, "Stack P: $" + hex(sp, 4));
	}


    void DrawCode(int x, int y, int nLines) {
		const auto* cpu = emu.getCPU();
		static std::map<uint16_t, std::string> mapAsm = cpu->disassemble(0x8000, 0xFFFF);

        uint16_t pc = cpu->getRegPC();
        auto it = mapAsm.find(pc);
        if (it == mapAsm.end()) return; // 若 PC 未命中映射表，则无法显示

        // 计算中间行的垂直位置（使 PC 行居中）
        int midY = y + (nLines >> 1) * 10;

        // 绘制 PC 行（高亮）
        DrawString(x, midY, it->second, olc::CYAN);

        // 向下绘制（从 PC 下一行开始）
        auto downIt = it;
        int downY = midY;
        while (downY < y + nLines * 10 - 10) { // 保留底部边距
            ++downIt;
            if (downIt == mapAsm.end()) break;
            downY += 10;
            DrawString(x, downY, downIt->second);
        }

        // 向上绘制（从 PC 上一行开始）
        auto upIt = it;
        int upY = midY;
        while (upY > y) {
            if (upIt == mapAsm.begin()) break;
            --upIt;
            upY -= 10;
            DrawString(x, upY, upIt->second);
        }
    }

	void DrawPalettes(int x, int y) {
		constexpr int swatchSize = 6;
		auto* ppu = emu.getPPU();
		for (int p = 0; p < 8; ++p) {
			for (int s = 0; s < 4; ++s) {
				fc_emulator::RGBA col = ppu->getColorFromPalette(p, s);
				FillRect(x + p * (swatchSize * 5) + s * swatchSize, y,
					swatchSize, swatchSize, olc::Pixel(col.r, col.g, col.b, 255));
			}
		}
		// 绘制选中框
		DrawRect(x + selectedPalette * (swatchSize * 5) - 1, y - 1,
			swatchSize * 4, swatchSize, olc::WHITE);
	}


	void HandleControllerInput() {
		// 玩家1控制
		auto set = [&](olc::Key key, fc_emulator::CtrlButton btn) {
			if (GetKey(key).bHeld) 
				emu.setButton(1, btn, true);
			else
				emu.setButton(1, btn, false);
			};
		set(olc::Key::J, fc_emulator::CtrlButton::A);
		set(olc::Key::K, fc_emulator::CtrlButton::B);
		set(olc::Key::G, fc_emulator::CtrlButton::SELECT);
		set(olc::Key::H, fc_emulator::CtrlButton::START);
		set(olc::Key::W, fc_emulator::CtrlButton::UP);
		set(olc::Key::S, fc_emulator::CtrlButton::DOWN);
		set(olc::Key::A, fc_emulator::CtrlButton::LEFT);
		set(olc::Key::D, fc_emulator::CtrlButton::RIGHT);
	}


};

int main() {
	FCDebugApp app;
	
	// 窗口大小：左侧 256*2=512 像素显示画面，右侧留出 268 像素宽度用于调试信息
	if (app.Construct(780, 480, 2, 2))  // 与示例类似 780x480
		app.Start();
	
	return 0;
}