// SPDX-License-Identifier: GPL-2.0-or-later
// Regression cases from 8bc0ce68, 55f9db99 and the phi worklist port.
#include <cassert>
#include <cstring>
#include <limits>
#include "render/permutation_indices.h"
#include "render/pipeline.h"
#include "render/pipeline_cache.h"
#include "common/serdes.h"
#include "shader/info.h"
#include "shader/ir/program.h"
#include "shader/ir/ir_emitter.h"
#include "shader/ir/passes/ir_passes.h"

int main() {
    // The cache reads each stage into a temporary Program. A second stage
    // with the same program hash is adopted into the already resident Program;
    // destroying the temporary must not leave a dangling specialization Info.
    {
        Vulkan::Program resident;
        resident.info.pgm_hash=0x12345678;
        for (bool append : {false,true}) {
            auto temporary=std::make_unique<Vulkan::Program>();
            temporary->info.pgm_hash=resident.info.pgm_hash;
            Shader::StageSpecialization spec{};
            spec.info=&temporary->info;
            if (append) resident.AddPermut({},std::move(spec));
            else resident.InsertPermut({},std::move(spec),3);
            temporary.reset();
            const auto& stored=resident.modules.back().spec;
            assert(stored.Valid() && stored.info==&resident.info);
            assert(stored.info->pgm_hash==0x12345678);
            Shader::StageSpecialization expected{};
            expected.info=&resident.info;
            assert(stored==expected);
        }
        assert(!resident.modules[0].spec.Valid()); // sparse slots stay empty
    }
    // Execute the production-generated walker: nested null tables zero the whole subtree,
    // preserve adjacent user data, and do not poison the next non-null invocation.
    {
        Shader::Info walk_info;
        std::array<u32, Shader::NUM_USER_DATA_REGS> ud{};
        walk_info.user_data = ud;
        Common::ObjectPool<Shader::IR::Inst> pool;
        Shader::IR::Block block(pool);
        Shader::IR::IREmitter ir(block);
        const auto base = ir.CompositeConstruct(ir.GetUserData(Shader::IR::ScalarReg(0)),
                                                ir.GetUserData(Shader::IR::ScalarReg(1)));
        const auto low = ir.ReadConst(base, ir.Imm32(0u));
        const auto high = ir.ReadConst(base, ir.Imm32(1u));
        const auto child = ir.CompositeConstruct(low, high);
        const auto value = ir.ReadConst(child, ir.Imm32(2u));
        (void)value;
        Shader::IR::Program program(walk_info);
        program.blocks.push_back(&block);
        program.post_order_blocks.push_back(&block);
        Shader::Optimization::FlattenExtendedUserdataPass(program);
        assert(walk_info.srt_info.walker_func);
        std::array<u32, 3> leaf{11, 22, 0x1234abcd};
        std::array<u64, 1> root{reinterpret_cast<u64>(leaf.data())};
        const auto run = [&](u64 address) {
            std::memcpy(ud.data(), &address, sizeof(address));
            std::vector<u32> flat(walk_info.srt_info.flattened_bufsize_dw, 0xfeedface);
            walk_info.srt_info.walker_func(ud.data(), flat.data());
            assert(flat[Shader::NUM_USER_DATA_REGS - 1] == 0xfeedface);
            return flat;
        };
        auto flat = run(0);
        for (size_t i = Shader::NUM_USER_DATA_REGS; i < flat.size(); ++i) assert(flat[i] == 0);
        root[0] = 0;
        flat = run(reinterpret_cast<u64>(root.data()));
        for (size_t i = Shader::NUM_USER_DATA_REGS; i < flat.size(); ++i) assert(flat[i] == 0);
        root[0] = reinterpret_cast<u64>(leaf.data());
        flat = run(reinterpret_cast<u64>(root.data()));
        assert(flat.back() == leaf[2]);
    }
    Vulkan::PermutationIndices indices;
    // Only two modules were loaded, but the disk still holds index 7.
    assert(indices.Observe(0x1234, 7));
    assert(indices.Observe(0x1234, 2));
    assert(indices.Next(0x1234, 2) == 8);
    assert(indices.Next(0x1234, 2) == 9); // an imported recipe must not overwrite 8
    assert(indices.Next(0x9876, 0) == 0); // separate program namespace
    assert(indices.Next(0x1234, 20) == 20);
    assert(!indices.Observe(0x1234, std::numeric_limits<size_t>::max()));
    indices.Clear();
    assert(indices.Next(0x1234, 0) == 0);

    // Real pipeline state round-trip, including reuse and corrupt element counts.
    Vulkan::GraphicsPipeline::SerializationSupport source{}, restored{};
    source.vertex_attributes.emplace_back(2, 1, vk::Format::eR32Sfloat, 12);
    source.vertex_bindings.emplace_back(1, 16, vk::VertexInputRate::eVertex);
    source.divisors.emplace_back(1, 3);
    source.tcs = {1, 2};
    source.tes = {3};
    source.fragment = {4, 5};
    Serialization::Archive encoded;
    source.Serialize(encoded);
    const auto bytes = encoded.TakeOff();
    for (unsigned i = 0; i < 2; ++i) {
        Serialization::Archive input{std::vector<u8>(bytes)};
        restored.tcs.clear(); restored.tes.clear(); restored.fragment.clear();
        assert(restored.Deserialize(input));
        assert(restored.vertex_attributes == source.vertex_attributes);
        assert(restored.vertex_bindings == source.vertex_bindings);
        assert(restored.divisors == source.divisors);
        assert(restored.tcs == source.tcs && restored.tes == source.tes);
        assert(restored.fragment == source.fragment);
        assert(input.IsEoS());
    }
    for (bool truncated : {false, true}) {
        Serialization::Archive invalid;
        Serialization::Writer writer{invalid};
        writer.Write(size_t(truncated ? 1 : source.vertex_attributes.capacity() + 1));
        Serialization::Archive input{invalid.TakeOff()};
        bool rejected = false;
        try { restored.Deserialize(input); }
        catch (const Serialization::CorruptData&) { rejected = true; }
        assert(rejected);
    }

    Shader::Info info{};
    info.buffers.resize(2);
    info.samplers.resize(1);
    info.images.resize(2);
    auto sharp = Gcn::Image::Null(false);
    sharp.base_level = 1;
    sharp.last_level = 4;
    auto& dynamic = info.images[0];
    dynamic.mip_fallback_mode = Shader::MipStorageFallbackMode::DynamicIndex;
    std::memcpy(dynamic.sharp_fetch.immediates.data(), &sharp, sizeof(sharp));
    assert(info.ImageBindingCount() == 5); // four mip bindings, one ordinary image
    Shader::Backend::Bindings binding{7, 3, 2};
    info.AddBindings(binding);
    assert(binding.unified == 15 && binding.buffer == 5 && binding.user_data == 2);
    dynamic.mip_fallback_mode = Shader::MipStorageFallbackMode::None;
    assert(info.ImageBindingCount() == 2);

    // Duplicate cyclic phis must collapse without merging different immediates.
    for (unsigned run = 0; run < 64; ++run) {
        Common::ObjectPool<Shader::IR::Inst> pool;
        Shader::IR::Block block(pool);
        block.po_index = 3;
        Shader::IR::Inst* phis[4];
        for (auto& phi : phis) {
            phi = &*block.PrependNewInst(block.end(), Shader::IR::Opcode::Phi, {},
                                         static_cast<u32>(Shader::IR::Type::U32));
        }
        for (unsigned i = 0; i < 4; ++i) {
            phis[i]->AddPhiOperand(&block, Shader::IR::Value{phis[i]});
            phis[i]->AddPhiOperand(&block, Shader::IR::Value{u32(i < 2 ? 42 : 47)});
        }
        auto a = block.PrependNewInst(block.end(), Shader::IR::Opcode::IAdd32,
                                      {Shader::IR::Value{phis[0]}, Shader::IR::Value{phis[1]}});
        auto b = block.PrependNewInst(block.end(), Shader::IR::Opcode::IAdd32,
                                      {Shader::IR::Value{phis[2]}, Shader::IR::Value{phis[3]}});
        Shader::IR::Program program(info);
        program.blocks.push_back(&block);
        Shader::Optimization::PhiSimplificationPass(program);
        assert(a->Arg(0) == a->Arg(1) && b->Arg(0) == b->Arg(1));
        assert(a->Arg(0) != b->Arg(0));
    }
}
