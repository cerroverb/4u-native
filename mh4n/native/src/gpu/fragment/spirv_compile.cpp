// GLSL to SPIR-V for the fragment stage with glslang as a library (N2-08; native/third_party/glslang). Not derived from
// Azahar: it is written from glslang's public API, after the vertex-stage compiler of gpu/shader/spirv_compile.cpp (N2-06),
// which compiles for the vertex stage only.
#include <chrono>
#include <mutex>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include "mh4n/spirv_target.h"

#include "gpu/fragment/generator.h"

namespace mh4n::gpu::fragment {

namespace {

// glslang keeps process-wide tables; they are made once and never torn down (the process ends). InitializeProcess counts
// its callers, so the vertex-stage compiler's own call does not matter.
void InitializeGlslang() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

}  // namespace

bool CompileFragmentGlslToSpirv(const std::string& source, std::vector<u32>& spirv, std::string& error, double* milliseconds) {
    const auto start = std::chrono::steady_clock::now();
    InitializeGlslang();
    spirv.clear();
    error.clear();

    glslang::TShader shader(EShLangFragment);
    const char* strings[1] = {source.c_str()};
    shader.setStrings(strings, 1);
    // Vulkan 1.2 and SPIR-V 1.5 (the backend asks for a Vulkan 1.2 instance), as the vertex stage; 1.1 and 1.3 on a device
    // below Vulkan 1.2 (mh4n/spirv_target.h).
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangFragment, glslang::EShClientVulkan, 100);
    const bool spirv13 = ::mh4n::gpu::SpirvTarget13();
    shader.setEnvClient(glslang::EShClientVulkan, spirv13 ? glslang::EShTargetVulkan_1_1 : glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, spirv13 ? glslang::EShTargetSpv_1_3 : glslang::EShTargetSpv_1_5);

    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
        error = std::string(shader.getInfoLog()) + shader.getInfoDebugLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        error = std::string(program.getInfoLog()) + program.getInfoDebugLog();
        return false;
    }
    const glslang::TIntermediate* intermediate = program.getIntermediate(EShLangFragment);
    if (intermediate == nullptr) {
        error = "glslang produced no fragment stage";
        return false;
    }
    glslang::SpvOptions options;
    options.disableOptimizer = true;  // glslang is built without spirv-tools
    options.validate = false;
    std::vector<unsigned int> words;
    glslang::GlslangToSpv(*intermediate, words, &options);
    spirv.assign(words.begin(), words.end());
    if (milliseconds != nullptr) {
        *milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    return !spirv.empty();
}

}  // namespace mh4n::gpu::fragment
