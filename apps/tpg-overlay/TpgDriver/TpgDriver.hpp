#pragma once

#include "FileHandle.hpp"
#include <string>
#include <memory>
#include <vector>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/xilinx-v4l2-controls.h>
#include <linux/media.h>
#include "FileHandle.hpp"

namespace Tpg
{
    enum class TestPattern : uint32_t
    {
        PASSTHROUGH = 0,
        HORIZONTAL_RAMP = 1,
        VERTICAL_RAMP = 2,
        TEMPORAL_RAMP = 3,
        SOLID_RED = 4,
        SOLID_GREEN = 5,
        SOLID_BLUE = 6,
        SOLID_BLACK = 7,
        SOLID_WHITE = 8,
        COLOR_BARS = 9,
        ZONE_PLATE = 10,
        TARTAN_COLOR_BARS = 11,
        CROSS_HATCH = 12,
        COLOR_SWEEP = 13,
        HORIZONTAL_VERTICAL_RAMP = 14,
        BLACK_WHITE_CHECKER = 15,
        CUSTOM_PATTERN = 16
    };

    enum class PixelFormat
    {
        RGB888,
        RBG888,
        YUV422
    };

    struct TpgConfig
    {
        uint32_t width = 640;
        uint32_t height = 480;
        PixelFormat format = PixelFormat::RGB888;
        TestPattern pattern = TestPattern::COLOR_BARS;
        uint32_t foreground_pattern = 1;
        std::string media_device = "/dev/media0";
        std::string video_device = "/dev/video0";
        std::string subdev_device = "/dev/v4l-subdev0";
        std::string tpg_entity = "43c00000.v_tpg";
    };

    class TpgDriver
    {
    public:
        explicit TpgDriver(const TpgConfig &config = TpgConfig{});
        ~TpgDriver();

        bool Configure(const TpgConfig &config);

        bool SetResolution(uint32_t width, uint32_t height);
        std::pair<uint32_t, uint32_t> GetResolution();

        bool SetPixelFormat(PixelFormat format);
        PixelFormat GetPixelFormat();

        bool SetTestPattern(TestPattern pattern);
        TestPattern GetTestPattern();

        bool SetForegroundPattern(uint32_t pattern);
        uint32_t GetForegroundPattern();

        bool SetMotionSpeed(uint32_t speed);
        uint32_t GetMotionSpeed();

        bool SetBoxSize(uint32_t box_size);
        uint32_t GetBoxSize();

        bool Initialize();

    private:
        std::unique_ptr<Utils::FileHandle> m_FileHandle_media;
        std::unique_ptr<Utils::FileHandle> m_FileHandle_video;
        std::unique_ptr<Utils::FileHandle> m_FileHandle_subdev;

        TpgConfig m_Config;
        bool m_IsInitialized;

        uint32_t m_TpgEntityId;

        struct v4l2_format m_VideoFormat;
        struct v4l2_subdev_format m_SubdevFormat;

        bool ConfigureVideoFormat();
        bool ConfigureMediaPipeline();
        bool FindMediaEntity(const std::string &entity_name, uint32_t &entity_id);
        bool SetMediaPadFormat(uint32_t entity_id, uint32_t pad_index,
                               uint32_t width, uint32_t height, PixelFormat format);
        bool ValidateConfig(const TpgConfig &config);

        bool SetV4LControl(uint32_t id, int32_t value);
        bool GetV4LControl(uint32_t id, int32_t &value);
        bool SetSubdevFormat();

        uint32_t PixelFormatToMediaBus(PixelFormat format);
        uint32_t PixelFormatToV4L2(PixelFormat format);
    };
} // namespace Tpg