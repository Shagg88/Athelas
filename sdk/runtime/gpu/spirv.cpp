#include "gpu/spirv.h"

#include <mutex>

// glslang's headers hide the HLSL calls (setHlslIoMapping) unless this is set; its own build sets it for itself only
#ifndef ENABLE_HLSL
#define ENABLE_HLSL
#endif

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <spirv-tools/optimizer.hpp>

namespace gpu::spirv {

namespace {

// glslang turns an HLSL function's texture or sampler parameter into a Function-storage variable holding it, which
// Vulkan forbids (VUID-StandaloneSpirv-OpTypeImage-06924: the validation layer's error, and a driver may refuse the
// shader or misread it). True when the module has one: a pointer in Function storage to an image, a sampler or a
// sampled image, or an array or struct of them, used by a variable or a parameter. Most shaders have none and skip the pass.
bool NeedsLegalising(const std::vector<uint32_t>& m) {
    constexpr uint32_t kTypeImage = 25, kTypeSampler = 26, kTypeSampledImage = 27, kTypeArray = 28, kTypeRuntimeArray = 29, kTypeStruct = 30, kTypePointer = 32,
                       kFunctionParameter = 55, kVariable = 59, kStorageFunction = 7;
    std::vector<bool> opaque, ptr;  // by id
    auto mark = [](std::vector<bool>& v, uint32_t id) {
        if (id >= v.size()) v.resize(id + 1);
        v[id] = true;
    };
    auto is = [](const std::vector<bool>& v, uint32_t id) { return id < v.size() && v[id]; };
    for (size_t i = 5; i < m.size();) {
        uint32_t op = m[i] & 0xFFFF, n = m[i] >> 16;
        if (n == 0 || i + n > m.size()) break;
        const uint32_t* w = &m[i];
        if ((op == kTypeImage || op == kTypeSampler || op == kTypeSampledImage) && n >= 2) mark(opaque, w[1]);
        else if ((op == kTypeArray || op == kTypeRuntimeArray) && n >= 3 && is(opaque, w[2])) mark(opaque, w[1]);
        else if (op == kTypeStruct && n >= 2) {  // a struct with a texture or sampler member is opaque too
            for (uint32_t k = 2; k < n; k++)
                if (is(opaque, w[k])) {
                    mark(opaque, w[1]);
                    break;
                }
        }
        else if (op == kTypePointer && n >= 4 && w[2] == kStorageFunction && is(opaque, w[3])) mark(ptr, w[1]);
        else if ((op == kVariable || op == kFunctionParameter) && n >= 3 && is(ptr, w[1])) return true;
        i += n;
    }
    return false;
}

}  // namespace

bool Compile(const std::string& hlsl, const char* entry, Stage stage, std::vector<uint32_t>& out, std::string& error) {
    // once per process; glslang's own state is per shader after this, so compiles may run on any thread
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });

    out.clear();
    error.clear();
    const EShLanguage lang = stage == Stage::Vertex ? EShLangVertex : EShLangFragment;
    glslang::TShader shader(lang);
    const char* src = hlsl.c_str();
    const int len = static_cast<int>(hlsl.size());
    shader.setStringsWithLengths(&src, &len, 1);
    shader.setEntryPoint(entry);
    shader.setSourceEntryPoint(entry);
    shader.setEnvInput(glslang::EShSourceHlsl, lang, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    shader.setHlslIoMapping(true);
    shader.setAutoMapBindings(false);
    shader.setAutoMapLocations(true);   // stage in/out locations (not resources): HLSL semantics carry none
    shader.setShiftBinding(glslang::EResUbo, 0);
    shader.setShiftBinding(glslang::EResTexture, 8);
    shader.setShiftBinding(glslang::EResSampler, 16);
    shader.setGlobalUniformSet(0);

    const EShMessages msgs = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);
    if (!shader.parse(GetDefaultResources(), 100, false, msgs)) {
        error = shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs) || !program.mapIO()) {
        error = program.getInfoLog();
        return false;
    }
    glslang::SpvOptions opt;
    opt.validate = false;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(lang), out, &logger, &opt);
    const std::string log = logger.getAllMessages();
    if (out.empty()) {
        error = log.empty() ? "no SPIR-V produced" : log;
        return false;
    }
    // SPIRV-Tools' legalisation (what DXC and glslang's own optimised build run for HLSL): the texture and sampler
    // parameters inlined away. On failure the shader is kept as it was (it ran that way before) and the reason logged.
    if (NeedsLegalising(out)) {
        spvtools::Optimizer opt(SPV_ENV_VULKAN_1_1);
        std::string why;
        opt.SetMessageConsumer([&why](spv_message_level_t level, const char*, const spv_position_t&, const char* m) {
            if (level <= SPV_MSG_ERROR && why.size() < 400) why += std::string(m) + "; ";
        });
        opt.RegisterLegalizationPasses();
        spvtools::OptimizerOptions oo;
        oo.set_run_validator(false);
        std::vector<uint32_t> legal;
        if (opt.Run(out.data(), out.size(), &legal, oo) && !legal.empty()) out.swap(legal);
        else error = "legalising failed (the shader is used as it is): " + why;
    }
    return true;
}

}  // namespace gpu::spirv
