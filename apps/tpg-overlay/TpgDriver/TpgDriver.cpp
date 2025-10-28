#include "TpgDriver.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <cstring>
#include <spdlog/spdlog.h>

namespace Tpg
{

    TpgDriver::TpgDriver(const TpgConfig &config) : m_FileHandle_media(std::make_unique<Utils::FileHandle>(config.media_device, O_RDWR)),
                                                    m_FileHandle_video(std::make_unique<Utils::FileHandle>(config.video_device, O_RDWR)),
                                                    m_FileHandle_subdev(std::make_unique<Utils::FileHandle>(config.subdev_device, O_RDWR)),
                                                    m_Config(config),
                                                    m_IsInitialized(false),
                                                    m_TpgEntityId(0)
    {
        memset(&m_VideoFormat, 0, sizeof(m_VideoFormat));
        memset(&m_SubdevFormat, 0, sizeof(m_SubdevFormat));
    }

    TpgDriver::~TpgDriver()
    {
        if (m_FileHandle_video && m_FileHandle_video->IsOpen())
        {
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(m_FileHandle_video->GetFd(), VIDIOC_STREAMOFF, &type);
        }

        if (m_IsInitialized && m_FileHandle_subdev && m_FileHandle_subdev->IsOpen())
        {
            struct v4l2_control control{};
            control.id = V4L2_CID_TEST_PATTERN;
            control.value = 0;
            ioctl(m_FileHandle_subdev->GetFd(), VIDIOC_S_CTRL, &control);
        }
    }

    bool TpgDriver::Initialize()
    {
        if (m_IsInitialized)
        {
            SPDLOG_DEBUG("TPG Driver already initialized");
            return true;
        }

        if (!m_FileHandle_media->IsOpen() || !m_FileHandle_video->IsOpen() || !m_FileHandle_subdev->IsOpen())
        {
            SPDLOG_ERROR("One or more devices failed to open");
            return false;
        }

        SPDLOG_INFO("Initializing TPG with resolution {}x{}", m_Config.width, m_Config.height);

        // Step 1: Configure media pipeline (equivalent to media-ctl command)
        if (!ConfigureMediaPipeline())
        {
            SPDLOG_ERROR("Failed to configure media pipeline");
            return false;
        }

        // Step 2: Set subdev format (part of media-ctl functionality)
        if (!SetSubdevFormat())
        {
            SPDLOG_ERROR("Failed to set subdev format");
            return false;
        }

        // Step 3: Configure video format (equivalent to v4l2-ctl --set-fmt-video)
        if (!ConfigureVideoFormat())
        {
            SPDLOG_ERROR("Failed to configure video format");
            return false;
        }

        m_IsInitialized = true;
        SPDLOG_INFO("TPG Driver initialized successfully");
        return true;
    }

    bool TpgDriver::Configure(const TpgConfig &config)
    {
        if (!ValidateConfig(config))
        {
            return false;
        }
        m_Config = config;
        m_IsInitialized = false;
        return true;
    }

    bool TpgDriver::SetResolution(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            SPDLOG_ERROR("Invalid resolution: {}x{}", width, height);
            return false;
        }
        m_Config.width = width;
        m_Config.height = height;
        m_IsInitialized = false;
        return true;
    }

    std::pair<uint32_t, uint32_t> TpgDriver::GetResolution()
    {
        if (!m_IsInitialized || !m_FileHandle_video->IsOpen())
        {
            SPDLOG_WARN("Driver not initialized, returning cached resolution");
            return std::make_pair(m_Config.width, m_Config.height);
        }

        struct v4l2_format format;
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(m_FileHandle_video->GetFd(), VIDIOC_G_FMT, &format) == -1)
        {
            SPDLOG_ERROR("Failed to get video format: {}", strerror(errno));
            return std::make_pair(m_Config.width, m_Config.height);
        }

        SPDLOG_DEBUG("Read resolution from driver: {}x{}",
                     format.fmt.pix.width, format.fmt.pix.height);

        return std::make_pair(format.fmt.pix.width, format.fmt.pix.height);
    }

    bool TpgDriver::SetPixelFormat(PixelFormat format)
    {
        m_Config.format = format;
        m_IsInitialized = false;
        return true;
    }

    PixelFormat TpgDriver::GetPixelFormat()
    {
        if (!m_IsInitialized || !m_FileHandle_video->IsOpen())
        {
            SPDLOG_WARN("Driver not initialized, returning cached pixel format");
            return m_Config.format;
        }

        struct v4l2_format format;
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(m_FileHandle_video->GetFd(), VIDIOC_G_FMT, &format) == -1)
        {
            SPDLOG_ERROR("Failed to get video format: {}", strerror(errno));
            return m_Config.format;
        }

        switch (format.fmt.pix.pixelformat)
        {
        case V4L2_PIX_FMT_RGB24:
            return PixelFormat::RGB888;
        case V4L2_PIX_FMT_BGR24:
            return PixelFormat::RBG888;
        case V4L2_PIX_FMT_UYVY:
            return PixelFormat::YUV422;
        default:
            SPDLOG_WARN("Unknown pixel format from driver: 0x{:x}", format.fmt.pix.pixelformat);
            return m_Config.format;
        }
    }

    bool TpgDriver::SetTestPattern(TestPattern pattern)
    {
        m_Config.pattern = pattern;
        if (m_IsInitialized && m_FileHandle_subdev->IsOpen())
        {
            return SetV4LControl(V4L2_CID_TEST_PATTERN, static_cast<uint32_t>(pattern));
        }
        return true;
    }

    TestPattern TpgDriver::GetTestPattern()
    {
        if (!m_IsInitialized || !m_FileHandle_subdev->IsOpen())
        {
            SPDLOG_WARN("Driver not initialized, returning cached test pattern");
            return m_Config.pattern;
        }

        int32_t value;
        if (!GetV4LControl(V4L2_CID_TEST_PATTERN, value))
        {
            SPDLOG_ERROR("Failed to get test pattern from driver");
            return m_Config.pattern;
        }

        SPDLOG_DEBUG("Read test pattern from driver: {}", value);
        return static_cast<TestPattern>(value);
    }

    bool TpgDriver::SetForegroundPattern(uint32_t pattern)
    {
        if (pattern > 2)
        {
            SPDLOG_ERROR("Invalid foreground pattern: {}. Must be between 0 and 2.", pattern);
            return false;
        }

        if (m_IsInitialized && m_FileHandle_subdev->IsOpen())
        {
            auto result = SetV4LControl((V4L2_CID_XILINX_TPG + 18), static_cast<uint32_t>(pattern));
            if (!result)
            {
                SPDLOG_ERROR("Failed to set foreground pattern");
            }
            return result;
        }
        return true;
    }

    uint32_t TpgDriver::GetForegroundPattern()
    {
        if (!m_IsInitialized || !m_FileHandle_subdev->IsOpen())
        {
            SPDLOG_WARN("Driver not initialized, returning cached foreground pattern");
            return m_Config.foreground_pattern;
        }

        int32_t value;
        if (!GetV4LControl((V4L2_CID_XILINX_TPG + 18), value))
        {
            SPDLOG_ERROR("Failed to get foreground pattern from driver");
            return m_Config.foreground_pattern;
        }

        SPDLOG_DEBUG("Read foreground pattern from driver: {}", value);
        return static_cast<uint32_t>(value);
    }

    bool TpgDriver::SetMotionSpeed(uint32_t speed)
    {
        if (speed >= 255)
        {
            SPDLOG_ERROR("Invalid motion speed: {}. Must be between 0 and 255.", speed);
            return false;
        }
        if (m_IsInitialized && m_FileHandle_subdev->IsOpen())
        {
            return SetV4LControl(V4L2_CID_XILINX_TPG_MOTION_SPEED, speed);
        }
        return true;
    }

    uint32_t TpgDriver::GetMotionSpeed()
    {
        if (!m_IsInitialized || !m_FileHandle_subdev->IsOpen())
        {
            SPDLOG_WARN("Driver not initialized, returning default motion speed");
            return 0;
        }

        int32_t value;
        if (!GetV4LControl(V4L2_CID_XILINX_TPG_MOTION_SPEED, value))
        {
            SPDLOG_ERROR("Failed to get motion speed from driver");
            return 0;
        }

        SPDLOG_DEBUG("Read motion speed from driver: {}", value);
        return static_cast<uint32_t>(value);
    }

    bool TpgDriver::SetBoxSize(uint32_t box_size)
    {
        if (box_size > 4095 != 0)
        {
            SPDLOG_ERROR("Invalid box size: {}. Must be a power of two between 4 and 4096.", box_size);
            return false;
        }
        if (m_IsInitialized && m_FileHandle_subdev->IsOpen())
        {
            return SetV4LControl(V4L2_CID_XILINX_TPG_BOX_SIZE, box_size);
        }
        return true;
    }

    uint32_t TpgDriver::GetBoxSize()
    {
        if (!m_IsInitialized || !m_FileHandle_subdev->IsOpen())
        {
            SPDLOG_WARN("Driver not initialized, returning default box size");
            return 0;
        }

        int32_t value;
        if (!GetV4LControl(V4L2_CID_XILINX_TPG_BOX_SIZE, value))
        {
            SPDLOG_ERROR("Failed to get box size from driver");
            return 0;
        }

        SPDLOG_DEBUG("Read box size from driver: {}", value);
        return static_cast<uint32_t>(value);
    }

    bool TpgDriver::ValidateConfig(const TpgConfig &config)
    {
        if (config.width == 0 || config.height == 0)
        {
            SPDLOG_ERROR("Invalid resolution: {}x{}", config.width, config.height);
            return false;
        }

        if (config.media_device.empty() || config.video_device.empty() || config.subdev_device.empty())
        {
            SPDLOG_ERROR("Device paths cannot be empty.");
            return false;
        }
        return true;
    }

    bool TpgDriver::SetV4LControl(uint32_t id, int32_t value)
    {
        struct v4l2_control control{};
        control.id = id;
        control.value = value;

        int fd = (id == V4L2_CID_TEST_PATTERN ||
                  id == V4L2_CID_XILINX_TPG_MOTION_SPEED ||
                  id == V4L2_CID_XILINX_TPG_BOX_SIZE ||
                  id == (V4L2_CID_XILINX_TPG + 18))
                     ? m_FileHandle_subdev->GetFd()
                     : m_FileHandle_video->GetFd();

        if (ioctl(fd, VIDIOC_S_CTRL, &control) == -1)
        {
            SPDLOG_ERROR("Failed to set V4L2 control (id: {}, value: {}): {}", id, value, strerror(errno));
            return false;
        }
        return true;
    }

    bool TpgDriver::GetV4LControl(uint32_t id, int32_t &value)
    {
        struct v4l2_control control{};
        control.id = id;

        int fd = (id == V4L2_CID_TEST_PATTERN ||
                  id == V4L2_CID_XILINX_TPG_MOTION_SPEED ||
                  id == V4L2_CID_XILINX_TPG_BOX_SIZE ||
                  id == (V4L2_CID_XILINX_TPG + 18))
                     ? m_FileHandle_subdev->GetFd()
                     : m_FileHandle_video->GetFd();

        if (ioctl(fd, VIDIOC_G_CTRL, &control) == -1)
        {
            SPDLOG_ERROR("Failed to get V4L2 control (id: {}): {}", id, strerror(errno));
            return false;
        }
        value = control.value;
        return true;
    }

    bool TpgDriver::SetSubdevFormat()
    {
        m_SubdevFormat.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        m_SubdevFormat.pad = 0; // Assuming single pad
        m_SubdevFormat.format.width = m_Config.width;
        m_SubdevFormat.format.height = m_Config.height;
        m_SubdevFormat.format.field = V4L2_FIELD_NONE;
        m_SubdevFormat.format.code = PixelFormatToMediaBus(m_Config.format);

        if (ioctl(m_FileHandle_subdev->GetFd(), VIDIOC_SUBDEV_S_FMT, &m_SubdevFormat) == -1)
        {
            SPDLOG_ERROR("Failed to set sub-device format: {}", strerror(errno));
            return false;
        }
        return true;
    }

    bool TpgDriver::ConfigureVideoFormat()
    {
        m_VideoFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        m_VideoFormat.fmt.pix.width = m_Config.width;
        m_VideoFormat.fmt.pix.height = m_Config.height;
        m_VideoFormat.fmt.pix.pixelformat = PixelFormatToV4L2(m_Config.format);
        m_VideoFormat.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(m_FileHandle_video->GetFd(), VIDIOC_S_FMT, &m_VideoFormat) == -1)
        {
            SPDLOG_ERROR("Failed to set video format: {}", strerror(errno));
            return false;
        }
        return true;
    }

    bool TpgDriver::FindMediaEntity(const std::string &entity_name, uint32_t &entity_id)
    {
        struct media_device_info mdi;
        if (ioctl(m_FileHandle_media->GetFd(), MEDIA_IOC_DEVICE_INFO, &mdi) == -1)
        {
            SPDLOG_ERROR("Failed to get media device info: {}", strerror(errno));
            return false;
        }

        SPDLOG_DEBUG("Media device: {} (driver: {})", mdi.model, mdi.driver);

        // Enumerate entities to find the TPG
        struct media_entity_desc entity;
        memset(&entity, 0, sizeof(entity));

        for (entity.id = MEDIA_ENT_ID_FLAG_NEXT;; entity.id |= MEDIA_ENT_ID_FLAG_NEXT)
        {
            if (ioctl(m_FileHandle_media->GetFd(), MEDIA_IOC_ENUM_ENTITIES, &entity) == -1)
            {
                if (errno == EINVAL)
                {
                    break;
                }
                SPDLOG_ERROR("Failed to enumerate entities: {}", strerror(errno));
                return false;
            }

            SPDLOG_DEBUG("Found entity: {} (id: {})", entity.name, entity.id);

            if (entity_name == std::string(entity.name))
            {
                entity_id = entity.id;
                SPDLOG_INFO("Found TPG entity '{}' with ID: {}", entity_name, entity_id);
                return true;
            }
        }

        SPDLOG_ERROR("Entity '{}' not found", entity_name);
        return false;
    }

    uint32_t TpgDriver::PixelFormatToMediaBus(PixelFormat format)
    {

        switch (format)
        {
        case PixelFormat::RGB888:
            return MEDIA_BUS_FMT_RBG888_1X24;
        case PixelFormat::RBG888:
            return MEDIA_BUS_FMT_RBG888_1X24;
        case PixelFormat::YUV422:
            return MEDIA_BUS_FMT_UYVY8_1X16;
        default:
            SPDLOG_ERROR("Unsupported pixel format.");
            return 0;
        }
    }

    bool TpgDriver::ConfigureMediaPipeline()
    {

        // Find the TPG entity in the media controller
        if (!FindMediaEntity(m_Config.tpg_entity, m_TpgEntityId))
        {
            SPDLOG_ERROR("Failed to find TPG entity: {}", m_Config.tpg_entity);
            return false;
        }

        // Set the format on the TPG's output pad (pad 0)
        if (!SetMediaPadFormat(m_TpgEntityId, 0, m_Config.width, m_Config.height, m_Config.format))
        {
            SPDLOG_ERROR("Failed to set media pad format");
            return false;
        }

        SPDLOG_INFO("Media pipeline configured successfully");
        return true;
    }

    bool TpgDriver::SetMediaPadFormat(uint32_t entity_id, uint32_t pad_index,
                                      uint32_t width, uint32_t height, PixelFormat format)
    {
        struct v4l2_subdev_format pad_format;
        memset(&pad_format, 0, sizeof(pad_format));

        // Configure the format structure
        pad_format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        pad_format.pad = pad_index;
        pad_format.format.width = width;
        pad_format.format.height = height;
        pad_format.format.code = PixelFormatToMediaBus(format);
        pad_format.format.field = V4L2_FIELD_NONE;
        pad_format.format.colorspace = V4L2_COLORSPACE_SRGB;

        if (ioctl(m_FileHandle_subdev->GetFd(), VIDIOC_SUBDEV_S_FMT, &pad_format) == -1)
        {
            SPDLOG_ERROR("Failed to set pad format on entity {} pad {}: {}",
                         entity_id, pad_index, strerror(errno));
            return false;
        }

        SPDLOG_INFO("Set pad format: {}x{} code:0x{:x} on entity {} pad {}",
                    width, height, pad_format.format.code, entity_id, pad_index);

        struct v4l2_subdev_format verify_format;
        memset(&verify_format, 0, sizeof(verify_format));
        verify_format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        verify_format.pad = pad_index;

        if (ioctl(m_FileHandle_subdev->GetFd(), VIDIOC_SUBDEV_G_FMT, &verify_format) == -1)
        {
            SPDLOG_WARN("Failed to verify pad format: {}", strerror(errno));
        }
        else
        {
            SPDLOG_DEBUG("Verified format: {}x{} code:0x{:x}",
                         verify_format.format.width, verify_format.format.height,
                         verify_format.format.code);
        }

        return true;
    }

    uint32_t TpgDriver::PixelFormatToV4L2(PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::RGB888:
            return V4L2_PIX_FMT_RGB24;
        case PixelFormat::RBG888:
            return V4L2_PIX_FMT_BGR24;
        case PixelFormat::YUV422:
            return V4L2_PIX_FMT_UYVY;

        default:
            SPDLOG_ERROR("Unsupported pixel format.");
            return 0;
        }
    }

}