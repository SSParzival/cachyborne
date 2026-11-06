// SPDX-License-Identifier: GPL-2.0-or-later
// Exhaustive six-bit ISA encodings and production formatted-buffer lowering.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gcn/regs_targets.h"
#include "gcn/sharps.h"
#include "render/gcn_to_vk.h"
#include "shader/info.h"
#include "shader/ir/ir_emitter.h"
#include "shader/ir/passes/ir_passes.h"
#include "shader/ir/program.h"

using Gcn::DataFormat;

static void CheckBufferLowering(u32 raw, u32 number, bool typed) {
    namespace IR = Shader::IR;
    Shader::Info info{};
    info.buffers.resize(1);
    Gcn::Buffer buffer{};
    buffer.base_address = 0x10000;
    buffer.num_records = 16;
    buffer.data_format = raw;
    buffer.num_format = number;
    std::memcpy(info.buffers[0].sharp_fetch.immediates.data(), &buffer, sizeof(buffer));
    Common::ObjectPool<IR::Inst> pool;
    IR::Block block(pool);
    IR::IREmitter ir(block);
    IR::BufferInstInfo flags{};
    if (typed) {
        flags.inst_data_fmt.Assign(DataFormat(raw));
        flags.inst_num_fmt.Assign(Gcn::NumberFormat(number));
    }
    const auto loaded = ir.LoadBufferFormat(ir.Imm32(0u), ir.Imm32(0u), flags);
    ir.SetAttribute(IR::Attribute::RenderTarget0, IR::F32{ir.CompositeExtract(loaded, 0)}, 0);
    ir.StoreBufferFormat(ir.Imm32(0u), ir.Imm32(0u), loaded, flags);
    IR::Program program(info);
    program.blocks.push_back(&block);
    Shader::Optimization::LowerBufferFormatToRaw(program);
    bool zero = false;
    for (const auto& inst : block.Instructions()) {
        const auto op = inst.GetOpcode();
        assert(op != IR::Opcode::LoadBufferFormatF32 && op != IR::Opcode::StoreBufferFormatF32);
        // No memory operations may remain for an invalid descriptor.
        assert(!(op >= IR::Opcode::LoadBufferU8 && op <= IR::Opcode::StoreBufferF32x4));
        if (op == IR::Opcode::CompositeConstructF32x4) {
            for (unsigned component = 0; component < 4; ++component) {
                assert(inst.Arg(component).IsImmediate() && inst.Arg(component).F32() == 0.f);
            }
            zero = true;
        }
    }
    assert(zero);
}

int main() {
    for (u32 raw = 0; raw < 64; ++raw) {
        const bool reserved = raw == 15 || (raw >= 23 && raw <= 31) || raw == 42 || raw == 43;
        const u32 expected = reserved ? 0 : raw == 7 ? 6 : raw == 8 ? 9 : raw == 18 ? 17 : raw;
        assert(u32(Gcn::RemapDataFormat(DataFormat(raw))) == expected);
        for (u32 number = 0; number < 16; ++number) {
            auto image = Gcn::Image::Null(false);
            image.base_address = 0x100;
            image.data_format = raw;
            image.num_format = number;
            assert(u32(image.GetDataFmt()) == expected);
            if (reserved || raw == 0) {
                assert(Vulkan::GcnToVk::SurfaceFormat(image.GetDataFmt(), image.GetNumberFmt()) ==
                       vk::Format::eUndefined);
                assert(Gcn::NumComponents(image.GetDataFmt()) == 0);
            }
            if (raw < 16) {
                Gcn::Buffer buffer{};
                buffer.data_format = raw;
                buffer.num_format = number & 7;
                assert(u32(buffer.GetDataFmt()) == expected);
                if (raw == 15 && number < 8) {
                    CheckBufferLowering(raw, number, false);
                    CheckBufferLowering(raw, number, true);
                }
            }
            if (raw < 32) {
                Gcn::ColorBuffer color{};
                color.base_address = 0x100;
                color.info.format = raw;
                color.info.number_type = number & 7;
                assert(bool(color) == (expected != 0));
                color.base_address = 0;
                assert(!color);
            }
        }
    }
    std::puts("64 data encodings x 16 number encodings; reserved buffer loads/stores: OK");
}
