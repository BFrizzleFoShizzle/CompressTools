#include "WaveletLayerCommon.h"


WaveletLayerSize::WaveletLayerSize(uint32_t width, uint32_t height)
    : width(width), height(height)
{

}

uint32_t WaveletLayerSize::GetHeight() const
{
    return height;
}

uint32_t  WaveletLayerSize::GetWidth() const
{
    return width;
}


uint32_t WaveletLayerSize::GetPixelCount() const
{
    return width * height;
}

uint32_t WaveletLayerSize::GetWaveletCount() const
{
    return GetPixelCount() - (GetParentSize().GetPixelCount());
}

WaveletLayerSize WaveletLayerSize::GetParentSize() const
{
    return WaveletLayerSize(GetParentWidth(), GetParentHeight());
}

uint32_t WaveletLayerSize::GetParentHeight() const
{
    return (height + 1) / 2;
}

uint32_t  WaveletLayerSize::GetParentWidth() const
{
    return (width + 1) / 2;
}

bool WaveletLayerSize::IsRoot() const
{
    return !(GetParentWidth() > 1 && GetParentHeight() > 1);
}