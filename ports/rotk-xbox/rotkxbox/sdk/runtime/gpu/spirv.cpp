#include "gpu/spirv.h"

#include <mutex>

// glslang's headers hide the HLSL calls (setHlslIoMapping) unless this is set; its own build sets it for itself only
#ifndef ENABLE_HLSL
#define ENABLE_HLSL
#endif

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

namespace gpu::spirv {

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
    return true;
}

}  // namespace gpu::spirv
