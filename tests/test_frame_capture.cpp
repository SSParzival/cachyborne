// SPDX-License-Identifier: GPL-2.0-or-later
// Public capture API: transfers remain ordered and cannot be lost by the
// diagnostic buffer-sample limit after a pass's first four draws.
#include "render/vk_frame_capture.h"
#include "textures/image_info.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

int main() {
    using Vulkan::FrameCapture;
    char folder[]="/tmp/cachyborne-frame-capture-XXXXXX";
    assert(mkdtemp(folder));
    const std::filesystem::path root{folder},trigger=root/"trigger";
    setenv("BB_CAPTURE_TRIGGER",trigger.c_str(),1);
    setenv("BB_CAPTURE_DIR",root.c_str(),1);
    FrameCapture::Transfer("idle-copy-must-not-appear");
    VideoCore::ImageInfo display{},scene{};
    display.guest_address=0x1000;scene.guest_address=0x2000;
    display.size.width=scene.size.width=2560;
    display.size.height=scene.size.height=1440;
    display.pixel_format=scene.pixel_format=vk::Format::eR8G8B8A8Unorm;
    FrameCapture::AddDisplayBuffer(display.guest_address);
    { std::ofstream touch{trigger}; }
    FrameCapture::OnFlip(display.guest_address);
    assert(FrameCapture::Active());
    FrameCapture::Transfer("armed-copy-must-not-appear");
    const VideoCore::ImageInfo* colors[]={&display};
    FrameCapture::BeginPass(colors,1,nullptr);
    for (unsigned i=0;i<8;++i) FrameCapture::Draw(1,2,3,1);
    FrameCapture::Transfer("first-copy src uid 1 dst uid 2");
    colors[0]=&scene;
    FrameCapture::BeginPass(colors,1,nullptr);
    for (unsigned i=0;i<8;++i) FrameCapture::Draw(3,4,3,1);
    FrameCapture::Transfer("second-copy src uid 2 dst uid 3");
    FrameCapture::Dispatch(5,1,1,1);
    colors[0]=&display;
    FrameCapture::BeginPass(colors,1,nullptr);
    assert(!FrameCapture::Active());
    std::string report;
    for (const auto& path:std::filesystem::directory_iterator(root)) {
        if (path.path().extension()==".txt") {
            std::ifstream file{path.path()};
            report.assign(std::istreambuf_iterator<char>{file},{});
        }
    }
    assert(!report.empty());
    assert(report.find("idle-copy")==std::string::npos);
    assert(report.find("armed-copy")==std::string::npos);
    const auto first=report.find("#0 PASS draws 8");
    const auto copy1=report.find("#1 TRANSFER first-copy src uid 1 dst uid 2");
    const auto next=report.find("#2 PASS draws 8");
    const auto copy2=report.find("#3 TRANSFER second-copy src uid 2 dst uid 3");
    const auto dispatch=report.find("#4 COMPUTE dispatches 1");
    assert(first!=std::string::npos && copy1>first && next>copy1 && copy2>next && dispatch>copy2);
    assert(copy1!=std::string::npos && next!=std::string::npos && copy2!=std::string::npos && dispatch!=std::string::npos);
    std::filesystem::remove_all(root);
    std::puts("PASS: ordered standalone image-transfer records, after eight draws, interleaved graphics/compute and idle/armed exclusion");
}
