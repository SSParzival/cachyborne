// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the overlay menu's sections (upscaler, reactive mask, output, game effects, frame rate
// limit, shaders); Menu() in bbport_overlay.cpp puts them one under the other.
#include "bbport_overlay_internal.h"
#include "shim/bb_env.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "bbport_frame_rate.h"
#include "bbport_guest_hooks.h"
#include "imgui.h"
#include "render/compile_log.h"

extern "C" void runtime_restart(void); // bb-probe (probe.c)

namespace BbOverlay::detail {

/// The shaders and pipelines compiled in this session, newest first (BbCompileLog): what the
/// background did and what the game had to wait for. A row copies its hash.
void ShadersSection() {
    static const bool unfolded = bb_env("BB_OVERLAY_OPEN") != nullptr;
    if (!ImGui::CollapsingHeader(
            BbSettings::MenuText("Shaders (diagnostics)"),
            unfolded ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
        return;
    }
    std::uint64_t total = 0;
    const auto events = BbCompileLog::Newest(40, total);
    const u32 bg_total = g_bb_background_pipelines_total.load(std::memory_order_relaxed);
    const u32 bg_done = g_bb_background_pipelines_done.load(std::memory_order_relaxed);
    ImGui::Text("%s: %llu", BbSettings::MenuText("Compiled this session"),
                static_cast<unsigned long long>(total));
    ImGui::Text("%s: %u / %u%s", BbSettings::MenuText("Made in the background"),
                bg_done, bg_total,
                g_bb_shader_import_active.load(std::memory_order_relaxed)
                    ? BbSettings::MenuText(", recipes importing")
                    : "");
    if (events.empty()) {
        ImGui::TextDisabled("%s", BbSettings::MenuText("Nothing compiled yet"));
        return;
    }
    static constexpr const char* stages[] = {"FS", "VS", "GS", "ES", "HS", "LS", "CS"};
    const auto now = std::chrono::steady_clock::now();
    if (ImGui::BeginTable("##compiles", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 220.0f * base_scale))) {
        ImGui::TableSetupColumn(BbSettings::MenuText("Ago"));
        ImGui::TableSetupColumn(BbSettings::MenuText("What"));
        ImGui::TableSetupColumn(BbSettings::MenuText("Hash"));
        ImGui::TableSetupColumn("ms");
        ImGui::TableHeadersRow();
        for (const auto& e : events) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%.0f s", std::chrono::duration<double>(now - e.when).count());
            ImGui::TableNextColumn();
            switch (e.kind) {
            case BbCompileLog::Kind::Shader:
                ImGui::Text("%s %s #%u", BbSettings::MenuText("shader"),
                            e.stage < 7 ? stages[e.stage] : "?", unsigned(e.permutation));
                break;
            case BbCompileLog::Kind::Graphics:
                ImGui::TextUnformatted(BbSettings::MenuText("pipeline (waited)"));
                break;
            case BbCompileLog::Kind::Compute:
                ImGui::TextUnformatted(BbSettings::MenuText("compute pipeline"));
                break;
            case BbCompileLog::Kind::Background:
                ImGui::TextUnformatted(BbSettings::MenuText("pipeline (background)"));
                break;
            }
            ImGui::TableNextColumn();
            char hash[24];
            std::snprintf(hash, sizeof(hash), "%#llx", static_cast<unsigned long long>(e.hash));
            ImGui::PushID(&e);
            if (ImGui::Selectable(hash, false)) {
                ImGui::SetClipboardText(hash);
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", e.ms);
        }
        ImGui::EndTable();
    }
}

/// Text about a problem (an upscaler that cannot run, a missing file), in orange.
void ProblemText(const char* text) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", text);
        ImGui::PopTextWrapPos();
}

/// The menu's "Temporal upscaler" section: the upscaler, its preset, sharpness and model; true
/// when the upscaler is TAA (the reactive mask has nothing to do then).
bool UpscalerSection(BbSettings::Values& s) {
    ImGui::SeparatorText(BbSettings::MenuText("Temporal upscaler"));
    const char* upscalers[] = {
        BbSettings::MenuText("Off"), "FSR 3.1", "FSR 4 (INT8)", "FSR 4.1.x",
        BbSettings::MenuText("TAA (native anti-aliasing)"),
        "DLSS (NVIDIA RTX)", BbSettings::MenuText("FSR 1 (spatial, weak GPUs)"),
        BbSettings::MenuText("FSR 4 (AMD's DLL)")};
    static_assert(sizeof(upscalers) / sizeof(upscalers[0]) == BbSettings::UpscalerCount);
    static const char* later[] = {"XeSS"};
    int upscaler = s.upscaler;
    if (ImGui::BeginCombo(BbSettings::MenuText("Upscaler"), upscalers[upscaler])) {
        for (int i = 0; i < BbSettings::UpscalerCount; ++i) {
            const bool supported = i == BbSettings::UpscalerFsr4     ? s.fsr4_supported.load()
                                   : i == BbSettings::UpscalerFsr411 ? s.fsr411_supported.load()
                                   : i == BbSettings::UpscalerDlss   ? s.dlss_supported.load()
                                   : i == BbSettings::UpscalerFsrAmd ? s.fsr_amd_supported.load()
                                                                     : true;
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(upscalers[i], i == upscaler)) {
                Store(s.upscaler, i, true);
            }
            ImGui::EndDisabled();
            if (!supported && i == BbSettings::UpscalerFsrAmd) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", BbSettings::MenuText("— AMD's DLLs not found"));
            } else if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", BbSettings::MenuText("— not supported by this GPU"));
            }
        }
        for (const char* name : later) {
            ImGui::BeginDisabled();
            ImGui::Selectable(name, false);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", BbSettings::MenuText("— in development"));
        }
        ImGui::EndCombo();
    }
    if (s.upscaler == BbSettings::UpscalerFsr411 || s.upscaler == BbSettings::UpscalerFsrAmd) {
        // The FSR 4.1 model: those whose asset sets are built (tools/fsr4cap: the 4.1.1 DLL, AMD's
        // driver DLL for the 4.1.2 ones) and the GPU runs, or those AMD's DLLs hold (fsr_amd: FP8
        // needs FP8 matrices); switched on the fly (FSR 3.1 while the new one is built).
        const bool amd = s.upscaler == BbSettings::UpscalerFsrAmd;
        const char* models[BbSettings::Fsr4ModelCount] = {
            amd ? BbSettings::MenuText("Auto (the DLL's choice)")
                : BbSettings::MenuText("Auto (by the GPU)"),
            "FP8", "INT8", "INT8 RDNA2", "INT8 Lite"};
        const unsigned available = amd ? s.fsr_amd_models.load() : s.fsr4_models_available.load();
        const int model = std::clamp(s.fsr4_model.load(), 0, BbSettings::Fsr4ModelCount - 1);
        if (ImGui::BeginCombo(BbSettings::MenuText("FSR 4 model"), models[model])) {
            for (int i = 0; i < BbSettings::Fsr4ModelCount; ++i) {
                const bool usable = i == BbSettings::Fsr4Auto || (available >> i & 1u);
                ImGui::BeginDisabled(!usable);
                if (ImGui::Selectable(models[i], i == model)) {
                    Store(s.fsr4_model, i, true);
                }
                ImGui::EndDisabled();
                if (!usable) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", amd ? BbSettings::MenuText("— not on this GPU")
                                                  : BbSettings::MenuText("— not built (launcher)"));
                }
            }
            ImGui::EndCombo();
        }
        if (const char* active = s.fsr4_model_active.load(); active && !amd) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", active);
        }
        if (amd) {
            Hint(BbSettings::MenuText("Which of the four FSR 4 models in AMD's DLLs runs: Auto is the one the DLL picks "
                "for this GPU. FP8: RDNA4 only. INT8: RDNA3's. INT8 RDNA2: made for RX 6000. INT8 "
                "Lite: the light one (Steam Deck, APUs), faster and softer."));
        } else Hint(BbSettings::MenuText("FP8: RDNA4's model (emulated, slower, on RDNA3). INT8: FSR 4.1 on any GPU. INT8 RDNA2: "
            "FSR 4.1.2's model made for RX 6000. INT8 Lite: FSR 4.1.2's light model (Steam Deck, "
            "APUs): faster, softer. The 4.1.2 models are built from AMD's driver DLL in the "
            "launcher."));
    }
    if (s.upscaler == BbSettings::UpscalerDlss) {
        Hint(BbSettings::MenuText("NVIDIA DLSS Super Resolution (RTX GPUs; Native AA is DLAA). Needs the DLSS bridge "
            "and NVIDIA's library next to the game; otherwise FSR 3.1 is used."));
    }
    if (const char* problem = s.dlss_problem.load(); problem && s.upscaler == BbSettings::UpscalerDlss) {
        char line[192];
        std::snprintf(line, sizeof(line), "DLSS: %s", problem);
        ProblemText(line);
    }
    if (const char* problem = s.fsr4_problem.load()) {
        char line[192];
        std::snprintf(line, sizeof(line),
                      BbSettings::MenuText("FSR 4 unavailable: %s"), problem);
        ProblemText(line);
        if (!BbSettings::IsFsr4(s.upscaler)) {
            ImGui::PushTextWrapPos();
            ImGui::TextUnformatted(BbSettings::MenuText("The mode selected above is active. You can select FSR 4 again."));
            ImGui::PopTextWrapPos();
        }
    }
    if (BbSettings::IsFsr4(s.upscaler)) {
        if (s.upscaler == BbSettings::UpscalerFsrAmd) {
            if (const char* provider = s.fsr_amd_provider.load()) {
                ImGui::TextDisabled("%s", provider);
            }
            Hint(BbSettings::MenuText("AMD's FSR SDK 2.4 DLLs for Vulkan (amd_fidelityfx_loader_vk.dll and "
                "amd_fidelityfx_upscaler_vk.dll next to the game or in BB_FFX_DIR). They choose the "
                "model themselves (from the AMD driver too); on other GPUs it may be FSR 3.1."));
        } else if (s.upscaler == BbSettings::UpscalerFsr411) {
            Hint(BbSettings::MenuText("FSR 4.1: the model from AMD's 4.1.x DLL, replayed in Vulkan. "
                "Assets are built from your DLL in the launcher."));
        } else {
            Hint(BbSettings::MenuText("FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality "
                "than FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the "
                "model (a brief pause)."));
        }
        Checkbox(BbSettings::MenuText("FSR 4: auto exposure"),
                 s.fsr4_auto_exposure);
        Checkbox(BbSettings::MenuText("FSR 4: invert jitter sign"),
                 s.fsr4_invert_jitter);
        Hint(BbSettings::MenuText("For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses "
            "it to decide when to discard previous frames. Changes apply immediately, without a "
            "restart."));
    }
    const bool upscaler_on = s.upscaler != BbSettings::UpscalerOff;
    const bool taa = s.upscaler == BbSettings::UpscalerTaa;
    ImGui::BeginDisabled(!upscaler_on);
    ImGui::BeginDisabled(taa);
    int preset = taa ? BbSettings::NativeAA : s.preset.load();
    char preset_label[64];
    std::snprintf(preset_label, sizeof(preset_label), "%s (x%.1f)", BbSettings::MenuText(BbSettings::PresetName(preset)),
                  BbSettings::PresetScale(preset));
    if (ImGui::BeginCombo(BbSettings::MenuText("Preset"), preset_label)) {
        for (int i = 0; i < BbSettings::PresetCount; ++i) {
            char label[64];
            const float scale = BbSettings::PresetScale(i);
            const auto output = BbSettings::CurrentOutput();
            std::snprintf(
                label, sizeof(label),
                BbSettings::MenuText("%s (x%.1f, render %dx%d)"),
                BbSettings::MenuText(BbSettings::PresetName(i)), scale, int(std::lround(output.width / scale / 2) * 2),
                int(std::lround(output.height / scale / 2) * 2));
            if (ImGui::Selectable(label, i == preset)) {
                Store(s.preset, i, true);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (taa) {
        ImGui::TextWrapped("%s", BbSettings::MenuText("TAA anti-aliases the scene at output resolution, without an FSR model or upscaling. "
            "Your saved FSR preset is restored when you select FSR."));
    }
    ImGui::Text(
        BbSettings::MenuText("Active scene render: %d x %d"),
        s.active_render_width.load(), s.active_render_height.load());
    if (BbSettings::HybridSession()) {
        ImGui::Text(BbSettings::MenuText("Startup preset: %s"),
                    BbSettings::MenuText(BbSettings::PresetName(s.startup_preset)));
        Hint(BbSettings::MenuText("The game renders at the startup preset's size as if it were its own resolution: "
            "nothing extra a frame. Another preset applies at once (drawn in stand-in targets of "
            "its size). From the next start the chosen preset is the game's own size."));
    } else if (BbSettings::FixedRenderSession()) {
        ImGui::Text(BbSettings::MenuText("Startup preset: %s"),
                    BbSettings::MenuText(BbSettings::PresetName(s.startup_preset)));
        if (bb_env_on("BB_AUTO_RENDER_RES")) {
            Hint(BbSettings::MenuText("When output is not 1080p, the entire game renders at the preset resolution "
                "(startup patch): this is fastest on Steam Deck and weaker GPUs. Preset or output "
                "resolution changes require a restart. Enable Live resolution changes below to "
                "change them without restarting (post-processing then stays at 1080p, which is "
                "slower)."));
        } else {
            Hint(BbSettings::MenuText("BB_RENDER_RES fixes the scene size at startup. Remove this explicit environment "
                "variable to change resolution and presets without restarting the game."));
        }
    } else {
        Hint(BbSettings::MenuText("Native AA: the upscaler acts as anti-aliasing. Other presets reduce the scene render "
            "resolution relative to the output. The UI renders at output resolution. "
            "The preset applies from the next frame without restarting the game."));
    }
    // Sharpening works without an upscaler too (over the game's AA or FXAA: the presenter's
    // encode pass), so it stays enabled when the upscaler is off.
    ImGui::EndDisabled(); // upscaler off
    Checkbox(BbSettings::MenuText("Sharpening (RCAS)"), s.sharpen);
    ImGui::BeginDisabled(!s.sharpen);
    Slider(BbSettings::MenuText("Sharpness"), s.sharpness, 0.0f, 2.0f);
    Hint(BbSettings::MenuText("With an upscaler: up to 1 its own sharpening (RCAS), above 1 another RCAS pass. Without "
        "one: RCAS on the finished frame (over the game's AA or FXAA), up to 1 AMD's strongest. "
        "Ctrl+click the slider to enter an exact value."));
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!upscaler_on);
    // Frame generation: FSR 3.1 on any GPU (experimental); DLSS Frame Generation where it can run
    // (GeForce RTX 40 and newer with NVIDIA's library), and on any RTX GPU the entry says why not.
    {
        const int current = std::clamp(s.frame_generation.load(), 0, BbSettings::FrameGenCount - 1);
        const bool fsr3_shown = true;
        const bool dlss_shown = s.dlss_supported.load() || current == BbSettings::FrameGenDlss;
        const int dlss_max = s.dlssg_max_multiplier.load(std::memory_order_relaxed);
        const bool dlss_available = dlss_max > 1;
        const bool shown = fsr3_shown || dlss_available || s.dlss_supported.load() ||
                           current != BbSettings::FrameGenOff;
        if (shown) {
            const char* names[BbSettings::FrameGenCount] = {
                BbSettings::MenuText("Off"),
                BbSettings::MenuText("FSR 3.1 (experimental)"),
                "DLSS (NVIDIA RTX 40/50)",
                BbSettings::MenuText("FSR 4 (AMD's DLL)")};
            const bool amd_available = s.fg_amd_supported.load(std::memory_order_relaxed);
            if (ImGui::BeginCombo(BbSettings::MenuText("Frame generation"),
                                  names[current])) {
                for (int i = 0; i < BbSettings::FrameGenCount; ++i) {
                    if ((i == BbSettings::FrameGenFsr3 && !fsr3_shown) ||
                        (i == BbSettings::FrameGenDlss && !dlss_shown) ||
                        (i == BbSettings::FrameGenFsrAmd && !amd_available && i != current)) {
                        continue;
                    }
                    const bool supported =
                        i == BbSettings::FrameGenOff ||
                        (i == BbSettings::FrameGenFsr3
                             ? s.frame_generation_supported.load(std::memory_order_relaxed)
                         : i == BbSettings::FrameGenFsrAmd ? amd_available
                                                           : dlss_available);
                    ImGui::BeginDisabled(!supported);
                    if (ImGui::Selectable(names[i], i == current)) {
                        Store(s.frame_generation, i, true);
                    }
                    ImGui::EndDisabled();
                    if (!supported) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", BbSettings::MenuText("— not available here"));
                    }
                }
                ImGui::EndCombo();
            }
            Hint(BbSettings::MenuText("Frames generated between each two of the game's: twice the frames on screen "
                "(DLSS on GeForce RTX 50: up to four times), a little more input lag. DLSS Frame "
                "Generation needs the DLSS bridge, NVIDIA's libnvidia-ngx-dlssg.so next to the "
                "game or in the launcher's DLSS folder and a GeForce RTX 40 or newer. Works with "
                "every upscaler preset except Off; menus and movies are not generated."));
            if (current == BbSettings::FrameGenDlss && dlss_max > 2) {
                int multiplier = std::clamp(s.frame_generation_multiplier.load(), 2, dlss_max);
                const bool changed = ImGui::SliderInt(
                    BbSettings::MenuText("Multiplier"), &multiplier, 2, dlss_max, "%dx");
                Store(s.frame_generation_multiplier, multiplier, changed);
                Hint(BbSettings::MenuText("Frames on screen per game frame: 2x one generated, 4x three. The display "
                    "needs that refresh rate for them to be seen."));
            }
            const char* problem = current == BbSettings::FrameGenDlss
                                      ? s.dlssg_problem.load(std::memory_order_relaxed)
                                  : current == BbSettings::FrameGenFsr3
                                      ? s.frame_generation_problem.load(std::memory_order_relaxed)
                                  : current == BbSettings::FrameGenFsrAmd
                                      ? s.fg_amd_problem.load(std::memory_order_relaxed)
                                      : nullptr;
            if (current == BbSettings::FrameGenFsrAmd) {
                if (const char* provider = s.fg_amd_provider.load(std::memory_order_relaxed)) {
                    ImGui::TextDisabled("%s", provider);
                }
            }
            if (problem) {
                char line[256];
                std::snprintf(line, sizeof(line), BbSettings::MenuText("Frame generation: %s"),
                              problem);
                ProblemText(line);
            }
            // The generation runs on the upscaler's scaled path (TemporalUpscaler::RunScaled).
            if (current != BbSettings::FrameGenOff) {
                const int upscaler = s.upscaler.load(std::memory_order_relaxed);
                if (upscaler == BbSettings::UpscalerOff || upscaler == BbSettings::UpscalerTaa) {
                    ProblemText(BbSettings::MenuText("Frame generation needs an upscaler: FSR or DLSS (Native AA too)"));
                } else if (s.frame_generation_restart.load(std::memory_order_relaxed)) {
                    ProblemText(BbSettings::MenuText("Frame generation starts after a restart of the game"));
                }
            }
        }
    }
    Checkbox(BbSettings::MenuText("Subpixel jitter"), s.jitter);
    Hint(BbSettings::MenuText("Each frame shifts the scene by a fraction of a pixel, letting the upscaler reconstruct "
        "more detail from multiple frames. Without it, only history-based anti-aliasing remains."));
    return taa;
}

/// The menu's "Reactive mask" section.
void ReactiveMaskSection(BbSettings::Values& s, bool taa) {
    ImGui::SeparatorText(BbSettings::MenuText("Reactive mask"));
    ImGui::BeginDisabled(taa);
    Checkbox(BbSettings::MenuText("Enable mask"), s.reactive);
    Hint(BbSettings::IsFsr4(s.upscaler)
             ? BbSettings::MenuText("Marks transparent effects (haze, light, water, particles). FSR 4 takes no "
                   "mask itself: its output is blended with the current frame there, so the "
                   "effects do not drag previous frames along.")
             : BbSettings::MenuText("Marks transparent effects (particles, haze) so the upscaler relies less on "
                   "previous frames. Reduces trails behind effects, but shimmering returns "
                   "underneath them."));
    ImGui::BeginDisabled(!s.reactive);
    Slider(BbSettings::MenuText("Scale"), s.reactive_scale, 0.0f, 4.0f);
    Slider(BbSettings::MenuText("Threshold"), s.reactive_threshold, 0.0f, 1.0f);
    Slider(BbSettings::MenuText("Maximum"), s.reactive_max, 0.0f, 1.0f);
    bool show_mask = s.debug_view == BbSettings::DebugReactive;
    if (ImGui::Checkbox(BbSettings::MenuText("Show mask (debug)"),
                        &show_mask)) {
        s.debug_view = show_mask ? BbSettings::DebugReactive : BbSettings::DebugNone;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    Checkbox(BbSettings::MenuText("Character motion vectors"),
             s.object_motion);
    Hint(BbSettings::MenuText("Accurate vectors for animated objects: clothing and weapons break up less "
        "during movement. The static scene does not get an additional pass. "
        "Changes apply after restarting the game."));
    bool show_motion = s.debug_view == BbSettings::DebugMotion;
    if (ImGui::Checkbox(BbSettings::MenuText("Show motion vectors (debug)"),
                        &show_motion)) {
        s.debug_view = show_motion ? BbSettings::DebugMotion : BbSettings::DebugNone;
    }
    Hint(BbSettings::MenuText("Red/green: horizontal/vertical motion (8 pixels = full brightness). "
        "Blue: the pixel received an accurate object vector, not just camera motion. "
        "The upscaler treats a moving object with no blue or red/green as "
        "stationary, causing trails."));
    ImGui::EndDisabled(); // upscaler off
}

/// The menu's "Output" section: aspect ratio, output resolution, display.
void OutputSection(BbSettings::Values& s) {
    ImGui::SeparatorText(BbSettings::MenuText("Output"));
    // The aspect ratio first; its resolutions follow (another one keeps the height).
    const auto aspect_label = [](int a) {
        if (a != BbSettings::AspectAuto) return std::string(BbSettings::AspectKey(a));
        return std::string(BbSettings::MenuText("Auto")) + " (" +
               BbSettings::AspectKey(BbSettings::ResolvedAspect(a)) + ")";
    };
    const int aspect = s.aspect;
    if (ImGui::BeginCombo(BbSettings::MenuText("Aspect ratio"),
                          aspect_label(aspect).c_str())) {
        for (int a = 0; a < BbSettings::AspectCount; ++a) {
            if (ImGui::Selectable(aspect_label(a).c_str(), a == aspect)) {
                Store(s.output_res, BbSettings::PackSize(BbSettings::NearestOutput(a, BbSettings::CurrentOutput())),
                      true);
                Store(s.aspect, a, true);
            }
        }
        ImGui::EndCombo();
    }
    const auto size_label = [](BbSettings::OutputSize o) {
        return std::to_string(o.width) + " x " + std::to_string(o.height);
    };
    const auto current = BbSettings::CurrentOutput();
    const bool output_auto = s.output_auto.load();
    const std::string auto_label =
        std::string(BbSettings::MenuText("Auto (monitor)")) + " " + size_label(current);
    if (ImGui::BeginCombo(BbSettings::MenuText("Resolution"),
                          output_auto ? auto_label.c_str() : size_label(current).c_str())) {
        if (ImGui::Selectable(auto_label.c_str(), output_auto)) {
            Store(s.output_auto, true, true);
        }
        for (const auto& o : BbSettings::Outputs(s.aspect)) {
            if (ImGui::Selectable(size_label(o).c_str(), !output_auto && o == current)) {
                Store(s.output_res, BbSettings::PackSize(o), true);
                Store(s.output_auto, false, true);
            }
        }
        ImGui::EndCombo();
    }
    if (BbSettings::HybridSession()) {
        Hint(BbSettings::MenuText("Size of the final frame and UI; applies at once. The preset sets the scene size "
            "relative to the output: 4K Performance = 1920x1080. Another aspect ratio widens the "
            "view (the UI keeps to the screen edges)."));
    } else if (BbSettings::FixedRenderSession()) {
        Hint(BbSettings::MenuText("Size of the final frame and UI. The preset sets the scene size relative to "
            "the output: 4K Performance = 1920x1080. Another aspect ratio widens the view (the "
            "UI keeps to the screen edges). Applies after restarting the game."));
    } else {
        Hint(BbSettings::MenuText("The final frame and UI size changes at the next frame boundary. "
            "The preset sets the scene size relative to the output: 4K Performance = 1920x1080. "
            "Another aspect ratio widens the view (the UI keeps to the screen edges). "
            "Changing the size resets the upscaler's history and may cause a brief pause."));
    }
    // Low latency (NVIDIA Reflex through VK_NV_low_latency2, AMD Anti-Lag 2 through
    // VK_AMD_anti_lag): shown where the driver has one, and on any NVIDIA GPU so that the entry
    // says when the driver is too old.
    if (s.low_latency_supported.load(std::memory_order_relaxed) || s.dlss_supported.load()) {
        const char* latency_modes[BbSettings::LowLatencyCount] = {
            BbSettings::MenuText("Off"), BbSettings::MenuText("On"),
            BbSettings::MenuText("On + Boost")};
        const int latency = std::clamp(s.low_latency.load(), 0, BbSettings::LowLatencyCount - 1);
        const bool supported = s.low_latency_supported.load(std::memory_order_relaxed);
        const bool anti_lag = s.low_latency_backend.load(std::memory_order_relaxed) == 2;
        // With DLSS Frame Generation at least On (BbSettings::EffectiveLowLatency): Off is shown
        // as what runs and cannot be chosen.
        const int effective = std::clamp(BbSettings::EffectiveLowLatency(), 0, BbSettings::LowLatencyCount - 1);
        const bool forced = effective != latency;
        ImGui::BeginDisabled(!supported);
        if (ImGui::BeginCombo(anti_lag ? BbSettings::MenuText("Low latency (AMD Anti-Lag 2)")
                                       : BbSettings::MenuText("Low latency (NVIDIA Reflex)"),
                              latency_modes[effective])) {
            for (int i = 0; i < BbSettings::LowLatencyCount; ++i) {
                const bool allowed = i != BbSettings::LowLatencyOff || !forced;
                ImGui::BeginDisabled(!allowed);
                if (ImGui::Selectable(latency_modes[i], i == effective)) {
                    Store(s.low_latency, i, true);
                }
                ImGui::EndDisabled();
                if (!allowed) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", BbSettings::MenuText("— on with DLSS frame generation"));
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        Hint(anti_lag ? BbSettings::MenuText("AMD Anti-Lag 2 (VK_AMD_anti_lag; on Linux Mesa's anti-lag layer): the game's thread "
            "waits before each frame for as long as the GPU still has work queued, so that frames "
            "do not queue up in front of it; less input lag, a little less throughput when "
            "GPU-bound. Boost is Reflex's: here the same as On.")
                      : BbSettings::MenuText("NVIDIA Reflex through the driver's Vulkan low-latency mode: the game's thread waits "
            "for the driver before each frame so that frames do not queue up in front of the GPU; "
            "less input lag, a little less throughput when GPU-bound. Boost keeps the GPU clocks "
            "up. Recommended with frame generation. Needs NVIDIA's driver 550 or newer; off "
            "elsewhere."));
        if (!supported) {
            ProblemText(BbSettings::MenuText("Low latency: the driver has no VK_NV_low_latency2 (NVIDIA 550 or newer)"));
        }
    }
}

/// The menu's "Game effects" section.
void GameEffectsSection(BbSettings::Values& s) {
    ImGui::SeparatorText(BbSettings::MenuText("Game effects"));
    const char* lods[] = {BbSettings::MenuText("Highest (-2)"),
                          BbSettings::MenuText("Game default"),
                          BbSettings::MenuText("Lower (1)"),
                          BbSettings::MenuText("Lowest (2)")};
    static constexpr int lod_values[] = {-2, 0, 1, 2};
    int lod_index = 1;
    for (int i = 0; i < 4; ++i) {
        if (lod_values[i] == s.model_lod) lod_index = i;
    }
    if (ImGui::BeginCombo(BbSettings::MenuText("Model detail"),
                          lods[lod_index])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(lods[i], i == lod_index)) {
                Store(s.model_lod, lod_values[i], true);
            }
        }
        ImGui::EndCombo();
    }
    // Applied each frame (BbEngineFixes): no restart.
    Checkbox(BbSettings::MenuText("Coarse light grid"),
             s.light_grid_opt);
    // Anti-aliasing: one choice instead of the game's AA switch (BbSettings::SyncAntiAliasing).
    {
        const char* modes[BbSettings::AaCount] = {
            BbSettings::MenuText("Auto (the game's without an upscaler)"),
            BbSettings::MenuText("The game's own"),
            BbSettings::MenuText("FXAA2_GS (the game's)"),
            BbSettings::MenuText("Off"),
            BbSettings::MenuText("SMAA (without an upscaler)")};
        // Shown in this order: Off last.
        constexpr int order[BbSettings::AaCount] = {BbSettings::AaAuto, BbSettings::AaGame,
                                                    BbSettings::AaFxaa, BbSettings::AaSmaa,
                                                    BbSettings::AaOff};
        const int aa = std::clamp(s.anti_aliasing.load(), 0, BbSettings::AaCount - 1);
        if (ImGui::BeginCombo(BbSettings::MenuText("Anti-aliasing"), modes[aa])) {
            for (const int i : order) {
                if (ImGui::Selectable(modes[i], i == aa)) {
                    Store(s.anti_aliasing, i, true);
                }
            }
            ImGui::EndCombo();
        }
        Hint(BbSettings::MenuText("The game's anti-aliasing runs before the upscaler and only blurs its input: Auto "
            "switches it off while an upscaler is on. FXAA2_GS (the game's AA step with its "
            "FXAA2_GS technique, after Kyo's patch) applies at once when the game's AA was on, "
            "else at the next load, like the game's AA. SMAA (1x) runs on the finished scene "
            "before the UI, only without an upscaler, at once. Sharpening below works with any of "
            "them."));
    }
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        const auto& effect = BbSettings::Effects[e];
        if (e == BbSettings::GameAaEffect) {
            continue; // the anti-aliasing choice above
        }
        Checkbox(BbSettings::MenuText(effect.label), s.effects[e]);
        if (std::string_view(effect.key) == "debug_camera") {
            Hint(BbSettings::MenuText("Hold Cross and press L3 (keyboard: F + Left Ctrl)."));
        } else if (std::string_view(effect.key) == "debug_menu") {
            Hint(BbSettings::MenuText("Left side of the touchpad (Tab); right side: Backspace. Needs the adhoc folder "
                "from Nexus mod #253 (the fonts in adhoc/font)."));
        }
    }
    BbGuestHooks::ApplyGameEffects();
    Hint(BbSettings::MenuText("Chromatic aberration and depth of field switch immediately; ambient occlusion, motion blur, "
        "the game's AA and dynamic shadows at the next load (a lamp, a death, a save loaded). SSR "
        "reflections, intros, free camera and debug menu apply after a restart. Motion blur and "
        "shadows from dynamic lights place a significant load on the GPU."));
    Checkbox(BbSettings::MenuText("GT-VBAO ambient occlusion"), s.vbao);
    Hint(BbSettings::MenuText("Replaces the game's SSAO with GT-VBAO, at once (needs Ambient occlusion on). "
        "No dark halos around thin objects. Port by redditor69."));
    bool show_ao = s.debug_view == BbSettings::DebugAo;
    if (ImGui::Checkbox(BbSettings::MenuText("Show AO only (debug)"),
                        &show_ao)) {
        s.debug_view = show_ao ? BbSettings::DebugAo : BbSettings::DebugNone;
    }
    Hint(BbSettings::MenuText("The frame shows only the AO the lighting uses (white = unoccluded): VBAO when on, "
        "else the game's SSAO."));

    bool restart =
        s.object_motion != s.startup_object_motion || s.model_lod != s.startup_model_lod ||
        BbSettings::ResolutionNeedsRestart();
    for (int e = 0; e < BbSettings::EffectCount; ++e) {
        restart |= !BbSettings::IsHotEffect(e) && s.effects[e] != s.startup_effects[e];
    }
    if (restart) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                           BbSettings::MenuText("Changes apply after restarting the game"));
        if (ImGui::Button(
                BbSettings::MenuText("Apply and restart game"))) {
            BbSettings::Save();
            runtime_restart();
        }
    }
}

/// The menu's "Frame rate limit (now)" section.
void FrameRateLimitSection(BbSettings::Values& s) {
    ImGui::SeparatorText(BbSettings::MenuText("Frame rate limit (now)"));
    {
        static const int steps[] = {0, 30, 60, 90, 120, 144, 240};
        static const char* labels[] = {"As launched", "30", "60", "90", "120", "144", "240"};
        int index = 0;
        for (int i = 0; i < 7; ++i) {
            if (steps[i] == BbFrameRate::Limit()) index = i;
        }
        if (ImGui::BeginCombo(BbSettings::MenuText("Limit (F3: 30 FPS on and off)"),
                              BbSettings::MenuText(labels[index]))) {
            for (int i = 0; i < 7; ++i) {
                if (ImGui::Selectable(BbSettings::MenuText(labels[i]), i == index)) {
                    BbFrameRate::SetLimit(steps[i]);
                }
            }
            ImGui::EndCombo();
        }
        Hint(BbSettings::MenuText("Not saved. At 30 FPS the cinematic of Laurence (optional boss) does not hang, as it can "
            "above 30 (the community 60 FPS patch has the same problem)."));
    }
}

} // namespace BbOverlay::detail
