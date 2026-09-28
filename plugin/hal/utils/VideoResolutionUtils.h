#pragma once

#include "dsVideoDeviceTypes.h"

#include <cstdint>

namespace hal {
namespace video {

    struct ResolutionInfo {
        dsVideoResolution_t resolution;
        uint16_t width;
        uint16_t height;
        const char* resolutionStr;
    };

    struct FrameRateInfo {
        dsVideoFrameRate_t frameRate;
        const char* frameRateStr;
        uint16_t nominalFrameRate;
    };

    inline const ResolutionInfo* getResolutionInfo(dsVideoResolution_t resolution)
    {
        static const ResolutionInfo resolutionInfo[] = {
            { dsVIDEO_PIXELRES_720x480,   720,  480,  "480" },
            { dsVIDEO_PIXELRES_720x576,   720,  576,  "576" },
            { dsVIDEO_PIXELRES_1280x720,  1280, 720,  "720" },
            { dsVIDEO_PIXELRES_1366x768,  1366, 768,  "1366x768" },
            { dsVIDEO_PIXELRES_1920x1080, 1920, 1080, "1080" },
            { dsVIDEO_PIXELRES_3840x2160, 3840, 2160, "3840x2160" },
            { dsVIDEO_PIXELRES_4096x2160, 4096, 2160, "4096x2160" }
        };

        for (const auto& info : resolutionInfo) {
            if (info.resolution == resolution) {
                return &info;
            }
        }

        return nullptr;
    }

    inline const FrameRateInfo* getFrameRateInfo(dsVideoFrameRate_t frameRate)
    {
        static const FrameRateInfo frameRateInfo[] = {
            { dsVIDEO_FRAMERATE_24,       "24",     24 },
            { dsVIDEO_FRAMERATE_25,       "25",     25 },
            { dsVIDEO_FRAMERATE_30,       "30",     30 },
            { dsVIDEO_FRAMERATE_60,       "60",     60 },
            { dsVIDEO_FRAMERATE_23dot98,  "23.98",  24 },
            { dsVIDEO_FRAMERATE_29dot97,  "29.97",  30 },
            { dsVIDEO_FRAMERATE_50,       "50",     50 },
            { dsVIDEO_FRAMERATE_59dot94,  "59.94",  60 },
            { dsVIDEO_FRAMERATE_100,      "100",   100 },
            { dsVIDEO_FRAMERATE_119dot88, "119.88", 120 },
            { dsVIDEO_FRAMERATE_120,      "120",   120 },
            { dsVIDEO_FRAMERATE_200,      "200",   200 },
            { dsVIDEO_FRAMERATE_239dot76, "239.76", 240 },
            { dsVIDEO_FRAMERATE_240,      "240",   240 }
        };

        for (const auto& info : frameRateInfo) {
            if (info.frameRate == frameRate) {
                return &info;
            }
        }

        return nullptr;
    }

    inline constexpr const char* getInterlacedStr(bool interlaced)
    {
        return interlaced ? "i" : "p";
    }

} // namespace video
} // namespace hal