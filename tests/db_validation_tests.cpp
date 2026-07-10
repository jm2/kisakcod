#include <database/db_validation.h>

#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;

void Expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    Expect(db::validation::CanInternString(1), "empty terminated string can be interned");
    Expect(db::validation::CanInternString(65531), "maximum script-memory string can be interned");
    Expect(!db::validation::CanInternString(0), "zero-byte string extent rejected");
    Expect(!db::validation::CanInternString(65532), "script-memory allocation ceiling enforced");
    Expect(db::validation::PointerCountConsistent(false, 0), "null zero-count span is consistent");
    Expect(db::validation::PointerCountConsistent(true, 0), "present zero-count span is consistent");
    Expect(db::validation::PointerCountConsistent(true, 1), "present nonempty span is consistent");
    Expect(!db::validation::PointerCountConsistent(false, 1), "missing nonempty span rejected");
    Expect(!db::validation::PointerCountConsistent(false, -1), "negative span count rejected");
    Expect(!db::validation::PointerCountConsistent(true, -1), "present negative span count rejected");
    Expect(db::validation::CountInRange(96, 96, 65536), "minimum font glyph count accepted");
    Expect(db::validation::CountInRange(65536, 96, 65536), "maximum font glyph count accepted");
    Expect(!db::validation::CountInRange(95, 96, 65536), "short font glyph table rejected");
    Expect(!db::validation::CountInRange(65537, 96, 65536), "oversized font glyph table rejected");
    Expect(!db::validation::CountInRange(1, 2, 1), "invalid count range rejected");
    Expect(db::validation::OptionalMirroredCountInRange(0, 0, 2, 16), "empty mirrored graph accepted");
    Expect(db::validation::OptionalMirroredCountInRange(2, 2, 2, 16), "minimum mirrored graph accepted");
    Expect(db::validation::OptionalMirroredCountInRange(16, 16, 2, 16), "maximum mirrored graph accepted");
    Expect(!db::validation::OptionalMirroredCountInRange(1, 1, 2, 16), "short mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(17, 17, 2, 16), "oversized mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(4, 5, 2, 16), "mismatched mirrored graph rejected");
    Expect(!db::validation::OptionalMirroredCountInRange(-1, -1, 2, 16), "negative mirrored graph rejected");

    const float minimumGraph[][2] = {{0.0f, 0.2f}, {1.0f, 0.4f}};
    const float validGraph[][2] = {{0.0f, 0.2f}, {0.5f, 0.8f}, {1.0f, 0.4f}};
    const float missingStart[][2] = {{0.1f, 0.2f}, {1.0f, 0.4f}};
    const float missingEnd[][2] = {{0.0f, 0.2f}, {0.9f, 0.4f}};
    const float duplicateX[][2] = {{0.0f, 0.2f}, {0.5f, 0.8f}, {0.5f, 0.4f}, {1.0f, 0.3f}};
    const float decreasingX[][2] = {{0.0f, 0.2f}, {0.7f, 0.8f}, {0.5f, 0.4f}, {1.0f, 0.3f}};
    const float invalidY[][2] = {{0.0f, -0.1f}, {1.0f, 0.4f}};
    const float nanGraph[][2] = {
        {0.0f, 0.2f},
        {(std::numeric_limits<float>::quiet_NaN)(), 0.8f},
        {1.0f, 0.4f}};
    const float infiniteGraph[][2] = {
        {0.0f, 0.2f},
        {0.5f, (std::numeric_limits<float>::infinity)()},
        {1.0f, 0.4f}};
    float maximumGraph[16][2] = {};
    for (std::uint32_t index = 0; index < 16; ++index)
    {
        maximumGraph[index][0] = static_cast<float>(index) / 15.0f;
        maximumGraph[index][1] = 0.5f;
    }
    Expect(db::validation::NormalizedGraphKnots(minimumGraph, 2), "two-knot graph accepted");
    Expect(db::validation::NormalizedGraphKnots(validGraph, 3), "normalized graph accepted");
    Expect(db::validation::NormalizedGraphKnots(maximumGraph, 16), "sixteen-knot graph accepted");
    Expect(!db::validation::NormalizedGraphKnots(nullptr, 3), "missing graph rejected");
    Expect(!db::validation::NormalizedGraphKnots(validGraph, 1), "single-knot graph rejected");
    Expect(!db::validation::NormalizedGraphKnots(missingStart, 2), "graph missing zero endpoint rejected");
    Expect(!db::validation::NormalizedGraphKnots(missingEnd, 2), "graph missing one endpoint rejected");
    Expect(!db::validation::NormalizedGraphKnots(duplicateX, 4), "duplicate graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(decreasingX, 4), "decreasing graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(invalidY, 2), "out-of-range graph value rejected");
    Expect(!db::validation::NormalizedGraphKnots(nanGraph, 3), "NaN graph coordinate rejected");
    Expect(!db::validation::NormalizedGraphKnots(infiniteGraph, 3), "infinite graph value rejected");

    Expect(db::validation::MaterialVertexRoutingValid(0, 0), "minimum material vertex route accepted");
    Expect(db::validation::MaterialVertexRoutingValid(8, 11), "maximum material vertex route accepted");
    Expect(!db::validation::MaterialVertexRoutingValid(9, 0), "invalid material vertex source rejected");
    Expect(!db::validation::MaterialVertexRoutingValid(0, 12), "invalid material vertex destination rejected");
    Expect(db::validation::CountInRange(1, 1, 12), "minimum material vertex route count accepted");
    Expect(db::validation::CountInRange(12, 1, 12), "maximum unique material vertex routes accepted");
    Expect(!db::validation::CountInRange(0, 1, 12), "empty material vertex route table rejected");
    Expect(!db::validation::CountInRange(13, 1, 12), "oversized material vertex route table rejected");
    Expect(db::validation::CountInRange(1, 1, 4), "minimum material technique pass count accepted");
    Expect(db::validation::CountInRange(4, 1, 4), "maximum material technique pass count accepted");
    Expect(!db::validation::CountInRange(0, 1, 4), "empty material technique rejected");
    Expect(!db::validation::CountInRange(5, 1, 4), "oversized material technique rejected");
    std::uint32_t techniqueBytes = UINT32_MAX;
    Expect(
        db::validation::MaterialTechniqueDiskBytes(1, &techniqueBytes)
            && techniqueBytes == 28,
        "one-pass material technique disk extent accepted");
    Expect(
        db::validation::MaterialTechniqueDiskBytes(4, &techniqueBytes)
            && techniqueBytes == 88,
        "four-pass material technique disk extent accepted");
    Expect(
        !db::validation::MaterialTechniqueDiskBytes(0, &techniqueBytes)
            && techniqueBytes == 0,
        "empty material technique disk extent rejected");
    techniqueBytes = UINT32_MAX;
    Expect(
        !db::validation::MaterialTechniqueDiskBytes(5, &techniqueBytes)
            && techniqueBytes == 0,
        "oversized material technique disk extent rejected");
    Expect(
        !db::validation::MaterialTechniqueDiskBytes(1, nullptr),
        "null material technique disk extent output rejected");

    uint32_t textureTableBytes = UINT32_C(0xFFFFFFFF);
    Expect(
        db::validation::MaterialTextureTableDiskBytes(1, &textureTableBytes)
            && textureTableBytes == disk32::kMaterialTextureDefBytes,
        "single-entry material texture-table extent accepted");
    Expect(
        db::validation::MaterialTextureTableDiskBytes(255, &textureTableBytes)
            && textureTableBytes == 255 * disk32::kMaterialTextureDefBytes,
        "maximum material texture-table extent accepted");
    Expect(
        !db::validation::MaterialTextureTableDiskBytes(0, &textureTableBytes)
            && textureTableBytes == 0,
        "empty completed material texture table rejected");
    Expect(
        !db::validation::MaterialTextureTableDiskBytes(256, &textureTableBytes)
            && textureTableBytes == 0,
        "oversized material texture table rejected");
    Expect(
        !db::validation::MaterialTextureTableDiskBytes(1, nullptr),
        "null material texture-table extent output rejected");
    Expect(
        db::validation::MaterialTextureHeaderValid(1, 0, true, 'a', 'z'),
        "minimum material texture header accepted");
    Expect(
        db::validation::MaterialTextureHeaderValid(23, 11, true, '$', 'p'),
        "maximum decoded material sampler and semantic accepted");
    Expect(
        !db::validation::MaterialTextureHeaderValid(0, 0, true, 'a', 'z'),
        "material texture without a filter rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(8, 0, true, 'a', 'z'),
        "material texture mipmap state without a filter rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(25, 0, true, 'a', 'z'),
        "out-of-range decoded material sampler rejected");
    Expect(
        db::validation::MaterialTextureHeaderValid(1, 3, true, 'a', 'z'),
        "reserved in-range material texture semantic accepted");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 12, true, 'a', 'z'),
        "out-of-range material texture semantic rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 0, false, 'a', 'z'),
        "material texture without a payload rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 0, true, 0, 'z'),
        "material texture without a name start rejected");
    Expect(
        !db::validation::MaterialTextureHeaderValid(1, 0, true, 'a', 0),
        "material texture without a name end rejected");

    Expect(db::validation::WaterGridValid(4, 4), "minimum water FFT grid accepted");
    Expect(db::validation::WaterGridValid(8, 8), "power-of-two water FFT grid accepted");
    Expect(db::validation::WaterGridValid(64, 64), "maximum water FFT grid accepted");
    Expect(!db::validation::WaterGridValid(4, 8), "non-square water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(3, 3), "undersized water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(65, 65), "oversized water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(6, 6), "non-power-of-two water FFT grid rejected");
    Expect(!db::validation::WaterGridValid(-4, -4), "negative water FFT grid rejected");

    Expect(
        db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            800.0f,
            10.0f,
            1.0f,
            0.0f,
            0.5f),
        "finite positive water parameters accepted");
    Expect(
        !db::validation::WaterParametersValid(
            0.0f,
            1.0f,
            800.0f,
            10.0f,
            1.0f,
            0.0f,
            0.5f),
        "zero water world length rejected");
    Expect(
        !db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            800.0f,
            10.0f,
            0.0f,
            0.0f,
            0.5f),
        "zero water wind direction rejected");
    Expect(
        !db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            800.0f,
            10.0f,
            (std::numeric_limits<float>::quiet_NaN)(),
            1.0f,
            0.5f),
        "NaN water parameter rejected");
    Expect(
        !db::validation::WaterParametersValid(
            1.0f,
            1.0f,
            (std::numeric_limits<float>::infinity)(),
            10.0f,
            1.0f,
            0.0f,
            0.5f),
        "infinite water parameter rejected");

    struct ComplexPair
    {
        float real;
        float imag;
    };
    const ComplexPair finiteComplex[] = {{0.0f, 1.0f}, {-2.0f, 3.0f}};
    const ComplexPair nanComplex[] = {
        {0.0f, (std::numeric_limits<float>::quiet_NaN)()}};
    const float finiteFrequencies[] = {0.0f, 1.0f, 100.0f};
    const float negativeFrequencies[] = {0.0f, -1.0f};
    const float infiniteFrequencies[] = {
        0.0f,
        (std::numeric_limits<float>::infinity)()};
    Expect(
        db::validation::FiniteComplexArray(finiteComplex, 2),
        "finite water complex samples accepted");
    Expect(
        !db::validation::FiniteComplexArray(nanComplex, 1),
        "non-finite water complex sample rejected");
    Expect(
        db::validation::FiniteComplexArray<ComplexPair>(nullptr, 0),
        "empty water complex sample array accepted");
    Expect(
        !db::validation::FiniteComplexArray<ComplexPair>(nullptr, 1),
        "missing water complex sample array rejected");
    Expect(
        db::validation::FiniteNonnegativeFloatArray(finiteFrequencies, 3),
        "finite nonnegative water frequencies accepted");
    Expect(
        !db::validation::FiniteNonnegativeFloatArray(negativeFrequencies, 2),
        "negative water frequency rejected");
    Expect(
        !db::validation::FiniteNonnegativeFloatArray(infiniteFrequencies, 2),
        "infinite water frequency rejected");
    Expect(
        db::validation::FiniteNonnegativeFloatArray(nullptr, 0),
        "empty water frequency array accepted");
    Expect(
        !db::validation::FiniteNonnegativeFloatArray(nullptr, 1),
        "missing water frequency array rejected");
    const float finiteConstants[] = {0.0f, -1.0f, 2.0f, 3.0f};
    const float nanConstants[] = {
        0.0f,
        (std::numeric_limits<float>::quiet_NaN)()};
    Expect(
        db::validation::FiniteFloatArray(finiteConstants, 4),
        "finite water code constants accepted");
    Expect(
        !db::validation::FiniteFloatArray(nanConstants, 2),
        "non-finite water code constant rejected");
    Expect(
        db::validation::WaterPicmipDimension(8, 0) == 8,
        "water picmip zero preserves its dimension");
    Expect(
        db::validation::WaterPicmipDimension(8, 1) == 4,
        "water picmip one halves its dimension");
    Expect(
        db::validation::WaterPicmipDimension(4, 1) == 4,
        "water picmip preserves the minimum dimension");
    Expect(
        db::validation::WaterPicmipDimension(64, 1) == 32,
        "water picmip halves the maximum dimension");
    Expect(
        db::validation::WaterPicmipDimension(8, 2) == 0,
        "invalid water picmip rejected");
    Expect(
        db::validation::WaterPicmipDimension(6, 1) == 0,
        "non-power-of-two water picmip rejected");

    int amplitudeGrid[64];
    int frequencyGrid[64];
    for (int index = 0; index < 64; ++index)
    {
        amplitudeGrid[index] = index;
        frequencyGrid[index] = 1000 + index;
    }
    Expect(
        db::validation::DownsampleWaterGridInPlace(amplitudeGrid, 8, 4),
        "water amplitude grid downsamples in place");
    Expect(
        db::validation::DownsampleWaterGridInPlace(frequencyGrid, 8, 4),
        "water frequency grid downsamples in place");
    const int expectedPicmipIndices[] = {
        0, 2, 4, 6,
        16, 18, 20, 22,
        32, 34, 36, 38,
        48, 50, 52, 54};
    for (int index = 0; index < 16; ++index)
    {
        Expect(
            amplitudeGrid[index] == expectedPicmipIndices[index],
            "water amplitude picmip selects the expected source sample");
        Expect(
            frequencyGrid[index] == 1000 + expectedPicmipIndices[index],
            "water frequency picmip selects the matching source sample");
    }
    int minimumGrid[16];
    for (int index = 0; index < 16; ++index)
        minimumGrid[index] = index;
    Expect(
        db::validation::DownsampleWaterGridInPlace(minimumGrid, 4, 4),
        "minimum water grid accepts identity picmip");
    Expect(
        minimumGrid[15] == 15,
        "identity water picmip preserves samples");
    Expect(
        !db::validation::DownsampleWaterGridInPlace(minimumGrid, 4, 8),
        "water grid upsampling rejected");
    Expect(
        !db::validation::DownsampleWaterGridInPlace<int>(nullptr, 8, 4),
        "missing water grid rejected before picmip");
    Expect(
        !db::validation::DownsampleWaterGridInPlace(minimumGrid, 6, 4),
        "non-power-of-two source grid rejected before picmip");

    int maximumGrid[4096];
    for (int index = 0; index < 4096; ++index)
        maximumGrid[index] = index;
    Expect(
        db::validation::DownsampleWaterGridInPlace(maximumGrid, 64, 32),
        "maximum water grid downsamples one picmip level");
    Expect(
        maximumGrid[0] == 0
            && maximumGrid[1] == 2
            && maximumGrid[32] == 128
            && maximumGrid[1023] == 4030,
        "maximum water grid selects the expected source samples");

    Expect(
        db::validation::MaterialShaderLoadDefValid(true, 2, 0),
        "minimum shader load definition accepted");
    Expect(
        db::validation::MaterialShaderLoadDefValid(true, UINT16_MAX, 1),
        "maximum shader load definition accepted");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(false, 2, 0),
        "missing shader program rejected");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(true, 1, 0),
        "one-DWORD shader program rejected");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(
            true,
            UINT32_C(65536),
            0),
        "oversized shader program rejected");
    Expect(
        !db::validation::MaterialShaderLoadDefValid(true, 2, 2),
        "unknown shader renderer rejected");

    using db::validation::D3D9ShaderStage;
    const std::uint32_t validVertexShader[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x02000001),
        UINT32_C(0x800F0000),
        UINT32_C(0x90E40000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t validPixelShader[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x0001FFFE),
        UINT32_C(0x0000FFFF),
        UINT32_C(0x0000FFFF)};
    Expect(
        db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Vertex,
            0),
        "bounded shader-model-2 vertex bytecode accepted");
    Expect(
        db::validation::D3D9ShaderBytecodeValid(
            validPixelShader,
            4,
            D3D9ShaderStage::Pixel,
            1),
        "bounded shader-model-3 pixel bytecode and comment accepted");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Pixel,
            0),
        "vertex bytecode rejected for pixel stage");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validPixelShader,
            4,
            D3D9ShaderStage::Vertex,
            1),
        "pixel bytecode rejected for vertex stage");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Vertex,
            1),
        "shader-model-2 bytecode rejected for renderer 1");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validPixelShader,
            4,
            D3D9ShaderStage::Pixel,
            0),
        "shader-model-3 bytecode rejected for renderer 0");

    const std::uint32_t invalidMajor[] = {
        UINT32_C(0xFFFE0400),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t invalidMinor[] = {
        UINT32_C(0xFFFE0201),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t shaderMissingEnd[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x00000000)};
    const std::uint32_t earlyEnd[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x0000FFFF),
        UINT32_C(0x00000000)};
    const std::uint32_t truncatedInstruction[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x03000001),
        UINT32_C(0x800F0000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t truncatedComment[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x0002FFFE),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t nonExactEnd[] = {
        UINT32_C(0xFFFE0200),
        UINT32_C(0x0100FFFF),
        UINT32_C(0x00000000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t reservedInstruction[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x80000000),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t reservedComment[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x8000FFFE),
        UINT32_C(0x0000FFFF)};
    const std::uint32_t unknownOpcode[] = {
        UINT32_C(0xFFFF0300),
        UINT32_C(0x00000031),
        UINT32_C(0x0000FFFF)};
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            invalidMajor,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "unsupported shader model rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            invalidMinor,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "unsupported shader minor version rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            shaderMissingEnd,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "shader missing END token rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            earlyEnd,
            3,
            D3D9ShaderStage::Vertex,
            0),
        "shader trailing data after END rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            truncatedInstruction,
            4,
            D3D9ShaderStage::Vertex,
            0),
        "truncated shader instruction rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            truncatedComment,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "truncated shader comment rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            nonExactEnd,
            4,
            D3D9ShaderStage::Vertex,
            0),
        "END opcode with instruction bits rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            reservedInstruction,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "reserved shader instruction bits rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            reservedComment,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "reserved shader comment bit rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            unknownOpcode,
            3,
            D3D9ShaderStage::Pixel,
            1),
        "unknown shader opcode rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            nullptr,
            2,
            D3D9ShaderStage::Vertex,
            0),
        "null shader bytecode rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            static_cast<D3D9ShaderStage>(2),
            0),
        "unknown shader stage rejected");
    Expect(
        !db::validation::D3D9ShaderBytecodeValid(
            validVertexShader,
            5,
            D3D9ShaderStage::Vertex,
            2),
        "unknown shader renderer rejected by bytecode validation");
    Expect(db::validation::MaterialVertexRoutingFollows(0, 1, 0, 2), "ordered material vertex destination accepted");
    Expect(db::validation::MaterialVertexRoutingFollows(0, 11, 1, 0), "ordered material vertex source accepted");
    Expect(!db::validation::MaterialVertexRoutingFollows(1, 0, 0, 11), "decreasing material vertex route rejected");
    Expect(!db::validation::MaterialVertexRoutingFollows(1, 1, 1, 1), "duplicate material vertex route rejected");
    Expect(db::validation::MaterialPassLayoutValid(16, 16, 32, true, 0), "maximum material argument layout accepted");
    Expect(db::validation::MaterialPassLayoutValid(16, 16, 29, true, 7), "material layout with custom samplers accepted");
    Expect(!db::validation::MaterialPassLayoutValid(0, 0, 0, false, 0), "empty material argument layout rejected");
    Expect(!db::validation::MaterialPassLayoutValid(0, 0, 0, true, 0), "present empty material argument layout rejected");
    Expect(!db::validation::MaterialPassLayoutValid(1, 0, 0, false, 0), "missing material argument array rejected");
    Expect(!db::validation::MaterialPassLayoutValid(32, 32, 1, true, 0), "oversized material argument layout rejected");
    Expect(!db::validation::MaterialPassLayoutValid(16, 16, 32, true, 1), "custom sampler beyond argument limit rejected");
    Expect(!db::validation::MaterialPassLayoutValid(0, 0, 0, false, 8), "unknown custom sampler flag rejected");

    using db::validation::MaterialArgumentSegment;
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(3, MaterialArgumentSegment::PerPrimitive), "per-primitive code constant accepted");
    Expect(!db::validation::MaterialArgumentTypeAllowedInSegment(4, MaterialArgumentSegment::PerPrimitive), "per-primitive sampler rejected");
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(3, MaterialArgumentSegment::PerObject), "per-object code constant accepted");
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(4, MaterialArgumentSegment::PerObject), "per-object sampler accepted");
    Expect(!db::validation::MaterialArgumentTypeAllowedInSegment(5, MaterialArgumentSegment::PerObject), "per-object pixel constant rejected");
    Expect(db::validation::MaterialArgumentTypeAllowedInSegment(7, MaterialArgumentSegment::Stable), "stable literal accepted");
    Expect(!db::validation::MaterialArgumentTypeAllowedInSegment(8, MaterialArgumentSegment::Stable), "unknown material argument rejected");

    Expect(db::validation::MaterialArgumentShapeValid(0, 31, 0, 0, 0), "named vertex constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(1, 32, 0, 0, 0), "vertex constant destination rejected");
    Expect(db::validation::MaterialArgumentShapeValid(2, 15, 0, 0, 0), "named pixel sampler accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(2, 16, 0, 0, 0), "named pixel sampler destination rejected");
    Expect(db::validation::MaterialArgumentShapeValid(3, 31, 57, 0, 1), "scalar code vertex constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 31, 57, 1, 1), "scalar code vertex row rejected");
    Expect(db::validation::MaterialArgumentShapeValid(3, 28, 89, 0, 4), "code vertex matrix accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 29, 89, 0, 4), "vertex destination span rejected");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 28, 90, 0, 4), "vertex code source rejected");
    Expect(!db::validation::MaterialArgumentShapeValid(3, 28, 89, 1, 4), "vertex matrix row span rejected");
    Expect(db::validation::MaterialArgumentShapeValid(4, 15, 26, 0, 0), "code pixel sampler accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(4, 15, 27, 0, 0), "pixel sampler source rejected");
    Expect(db::validation::MaterialArgumentShapeValid(5, 255, 50, 0, 1), "code pixel constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(5, 256, 50, 0, 1), "pixel constant destination rejected");
    Expect(!db::validation::MaterialArgumentShapeValid(5, 255, 51, 0, 1), "pixel constant source rejected");
    Expect(db::validation::MaterialArgumentShapeValid(6, 255, 0, 0, 0), "named pixel constant accepted");
    Expect(db::validation::MaterialArgumentShapeValid(7, 255, 0, 0, 0), "literal pixel constant accepted");
    Expect(!db::validation::MaterialArgumentShapeValid(8, 0, 0, 0, 0), "unknown material argument shape rejected");
    Expect(db::validation::MaterialCodeConstantAllowedInSegment(50, MaterialArgumentSegment::Stable), "stable code constant accepted");
    Expect(!db::validation::MaterialCodeConstantAllowedInSegment(51, MaterialArgumentSegment::Stable), "non-stable code constant rejected");
    Expect(db::validation::MaterialCodeConstantAllowedInSegment(51, MaterialArgumentSegment::PerObject), "per-object code constant accepted");
    Expect(!db::validation::MaterialCodeConstantAllowedInSegment(57, MaterialArgumentSegment::PerObject), "per-primitive constant in object segment rejected");
    Expect(db::validation::MaterialCodeConstantAllowedInSegment(89, MaterialArgumentSegment::PerPrimitive), "per-primitive code constant accepted");
    Expect(!db::validation::MaterialCodeConstantAllowedInSegment(90, MaterialArgumentSegment::PerPrimitive), "out-of-range code constant rejected");
    Expect(db::validation::MaterialCodeSamplerAllowedInSegment(0, MaterialArgumentSegment::Stable), "stable code sampler accepted");
    Expect(db::validation::MaterialCodeSamplerAllowedInSegment(25, MaterialArgumentSegment::PerObject), "per-object code sampler accepted");
    Expect(!db::validation::MaterialCodeSamplerAllowedInSegment(4, MaterialArgumentSegment::Stable), "custom code sampler in stable args rejected");
    Expect(!db::validation::MaterialCodeSamplerAllowedInSegment(26, MaterialArgumentSegment::PerObject), "custom code sampler in object args rejected");
    Expect(!db::validation::MaterialCodeSamplerAllowedInSegment(0, MaterialArgumentSegment::PerPrimitive), "code sampler in primitive args rejected");

    const std::uint16_t validIndices[] = {0, 2, 3};
    const std::uint16_t invalidIndices[] = {0, 4};
    Expect(db::validation::AllU16Below(validIndices, 3, 4), "bounded uint16 indices accepted");
    Expect(!db::validation::AllU16Below(invalidIndices, 2, 4), "out-of-range uint16 index rejected");
    Expect(db::validation::AllU16Below(nullptr, 0, 0), "empty uint16 index list accepted");
    Expect(!db::validation::AllU16Below(nullptr, 1, 4), "missing uint16 index list rejected");

    std::uint32_t spanBytes = UINT32_MAX;
    Expect(db::validation::CheckedSpanBytes(0, 20, &spanBytes) && spanBytes == 0, "zero span size");
    Expect(db::validation::CheckedSpanBytes(12, 20, &spanBytes) && spanBytes == 240, "direct span size");
    Expect(db::validation::CheckedSpanBytes(UINT64_C(214748364), 20, &spanBytes)
        && spanBytes == UINT32_C(4294967280), "maximum direct span product");
    Expect(!db::validation::CheckedSpanBytes(UINT64_C(214748365), 20, &spanBytes), "direct span overflow rejected");
    Expect(!db::validation::CheckedSpanBytes((std::numeric_limits<std::uint64_t>::max)(), 1, &spanBytes), "oversized direct span count rejected");
    Expect(!db::validation::CheckedSpanBytes(1, 0, &spanBytes), "zero direct span stride rejected");
    Expect(!db::validation::CheckedSpanBytes(1, 1, nullptr), "null direct span result rejected");

    std::int32_t bytes = -1;
    Expect(db::validation::CheckedArrayBytes(0, 8, &bytes) && bytes == 0, "zero array size");
    Expect(db::validation::CheckedArrayBytes(32768, 8, &bytes) && bytes == 262144, "asset array size");
    Expect(!db::validation::CheckedArrayBytes(-1, 8, &bytes), "negative count rejected");
    Expect(!db::validation::CheckedArrayBytes(1, 0, &bytes), "zero stride rejected");
    Expect(!db::validation::CheckedArrayBytes((std::numeric_limits<std::int32_t>::max)(), 8, &bytes), "multiplication overflow rejected");
    Expect(!db::validation::CheckedArrayBytes(1, 8, nullptr), "null byte result rejected");

    std::int32_t count = -1;
    Expect(db::validation::CheckedCountSum(12, 30, &count) && count == 42, "count sum");
    Expect(!db::validation::CheckedCountSum(-1, 1, &count), "negative sum operand rejected");
    Expect(!db::validation::CheckedCountSum(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count), "count sum overflow rejected");
    Expect(!db::validation::CheckedCountSum(1, 1, nullptr), "null count sum result rejected");
    Expect(db::validation::CheckedCountProduct(6, 7, &count) && count == 42, "count product");
    Expect(db::validation::CheckedCountProduct(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count product");
    Expect(db::validation::CheckedCountProduct(0, 42, &count) && count == 0, "zero product");
    Expect(!db::validation::CheckedCountProduct(
        0, (std::numeric_limits<std::uint32_t>::max)(), &count), "oversized zero-product operand rejected");
    Expect(!db::validation::CheckedCountProduct(-1, 0, &count), "negative product operand rejected");
    Expect(!db::validation::CheckedCountProduct(
        (std::numeric_limits<std::uint32_t>::max)(), 2, &count), "count product overflow rejected");
    Expect(!db::validation::CheckedCountProduct(1, 1, nullptr), "null count product result rejected");
    Expect(db::validation::CheckedCountDifference(10, 3, &count) && count == 7, "count difference");
    Expect(db::validation::CheckedCountDifference(
        (std::numeric_limits<std::int32_t>::max)(), 0, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count difference");
    Expect(!db::validation::CheckedCountDifference(3, 10, &count), "count subtraction underflow rejected");
    Expect(!db::validation::CheckedCountDifference(
        (std::numeric_limits<std::uint32_t>::max)(),
        (std::numeric_limits<std::uint32_t>::max)(),
        &count), "oversized difference operands rejected");
    Expect(!db::validation::CheckedCountDifference(1, 0, nullptr), "null count difference result rejected");
    Expect(db::validation::CheckedCountCeilDiv(33, 32, &count) && count == 2, "count ceiling division");
    Expect(db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::int32_t>::max)(), 32, &count)
        && count == 67108864, "large count ceiling division");
    Expect(db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::int32_t>::max)(), 1, &count)
        && count == (std::numeric_limits<std::int32_t>::max)(), "maximum count ceiling division");
    Expect(!db::validation::CheckedCountCeilDiv(1, 0, &count), "zero divisor rejected");
    Expect(!db::validation::CheckedCountCeilDiv(1, 1, nullptr), "null ceiling-division result rejected");
    Expect(!db::validation::CheckedCountCeilDiv(
        (std::numeric_limits<std::uint32_t>::max)(), 32, &count), "oversized dividend rejected");

    Expect(db::validation::CanAppendBytes(0x40000, 0x40000, 0x80000), "double-buffer append");
    Expect(db::validation::CanAppendBytes(0x80000, 0, 0x80000), "zero append at capacity");
    Expect(!db::validation::CanAppendBytes(0x80001, 0, 0x80000), "current bytes over capacity rejected");
    Expect(!db::validation::CanAppendBytes(0x40001, 0x40000, 0x80000), "append over capacity rejected");

    Expect(db::validation::IsAlignmentMask(0), "zero alignment mask");
    Expect(db::validation::IsAlignmentMask(1), "two-byte alignment mask");
    Expect(db::validation::IsAlignmentMask(15), "sixteen-byte alignment mask");
    Expect(!db::validation::IsAlignmentMask(5), "noncontiguous alignment mask rejected");
    Expect(!db::validation::IsAlignmentMask((std::numeric_limits<std::uintptr_t>::max)()), "maximum mask rejected");

    std::uintptr_t aligned = 0;
    Expect(db::validation::AlignUp(0x1001, 15, &aligned) && aligned == 0x1010, "alignment rounds up");
    Expect(db::validation::AlignUp(0x1010, 15, &aligned) && aligned == 0x1010, "aligned value preserved");
    Expect(!db::validation::AlignUp((std::numeric_limits<std::uintptr_t>::max)() - 7, 15, &aligned), "alignment overflow rejected");

    constexpr std::uintptr_t base = 0x1000;
    Expect(db::validation::SpanWithinBlock(base, 0x100, base, 0x100), "whole block span");
    Expect(db::validation::SpanWithinBlock(base, 0x100, base + 0x100, 0), "exact end empty span");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base - 1, 1), "position before block rejected");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base + 0x100, 1), "span past end rejected");
    Expect(!db::validation::SpanWithinBlock(base, 0x100, base + 0x80, 0x81), "oversized tail span rejected");

    std::uint32_t remaining = 0;
    Expect(db::validation::RemainingInBlock(base, 0x100, base + 0x40, &remaining) && remaining == 0xC0, "remaining bytes computed");
    Expect(db::validation::RemainingInBlock(base, 0x100, base + 0x100, &remaining) && remaining == 0, "zero bytes at block end");
    Expect(!db::validation::RemainingInBlock(base, 0x100, base + 0x101, &remaining), "position after block rejected");

    return failures == 0 ? 0 : 1;
}
