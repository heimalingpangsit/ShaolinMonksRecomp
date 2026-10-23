#pragma once
#include <string>
namespace gpu { struct Assembled; }
namespace shaolin {
bool UnitSpriteTexcoords(const gpu::Assembled& assembled);
std::string AtlasQuadHlsl(std::string source);
std::string AtlasBoundsHlsl();
}
