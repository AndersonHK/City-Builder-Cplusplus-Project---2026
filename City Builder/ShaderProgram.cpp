#include "ShaderProgram.h"
#include "LotMaterialShader.h"

#include "CrashLogger.h"

#include <GL/glew.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
struct ShaderSources {
    std::string vertexSource;
    std::string fragmentSource;
};

// Splits the combined shader file into vertex and fragment source strings.
ShaderSources LoadShaderSources(const std::string& shaderFilePath) {
    std::ifstream shaderStream(shaderFilePath.c_str());
    if (!shaderStream.is_open()) {
        LogError("ShaderProgram::LoadShaderSources", "Unable to open shader file: " + shaderFilePath);
        throw std::runtime_error("Unable to open shader file: " + shaderFilePath);
    }

    enum class ShaderType {
        None = -1,
        Vertex = 0,
        Fragment = 1
    };

    ShaderType shaderType = ShaderType::None;
    std::stringstream sourceBuilders[2];
    std::string line;
    while (std::getline(shaderStream, line)) {
        if (line.find("#shader") != std::string::npos) {
            if (line.find("vertex") != std::string::npos) {
                shaderType = ShaderType::Vertex;
            } else if (line.find("fragment") != std::string::npos) {
                shaderType = ShaderType::Fragment;
            }

            continue;
        }

        if (shaderType != ShaderType::None) {
            if (line.find("// LOT_MATERIAL_SHADER") != std::string::npos) {
                sourceBuilders[static_cast<int>(shaderType)] << LotMaterialShaderSource() << '\n';
                continue;
            }
            sourceBuilders[static_cast<int>(shaderType)] << line << '\n';
        }
    }

    ShaderSources shaderSources;
    shaderSources.vertexSource = sourceBuilders[0].str();
    shaderSources.fragmentSource = sourceBuilders[1].str();
    return shaderSources;
}
}

// Starts with no OpenGL program object.
ShaderProgram::ShaderProgram()
    : programId_(0) {
}

// Releases the linked OpenGL program if one was created.
ShaderProgram::~ShaderProgram() {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }
}

// Compiles and links a shader program from the combined shader file.
bool ShaderProgram::loadFromFile(const std::string& shaderFilePath) {
    CrashScope crashScope("ShaderProgram::loadFromFile");
    const ShaderSources shaderSources = LoadShaderSources(shaderFilePath);
    const unsigned int vertexShaderId = compileShader(GL_VERTEX_SHADER, shaderSources.vertexSource);
    const unsigned int fragmentShaderId = compileShader(GL_FRAGMENT_SHADER, shaderSources.fragmentSource);
    return linkProgram(vertexShaderId, fragmentShaderId);
}

// Binds the linked shader program for subsequent draw calls.
void ShaderProgram::bind() const {
    glUseProgram(programId_);
}

// Returns the raw OpenGL program id for uniform lookups.
unsigned int ShaderProgram::programId() const {
    return programId_;
}

// Compiles one shader stage and prints compiler diagnostics on failure.
unsigned int ShaderProgram::compileShader(unsigned int shaderType, const std::string& source) const {
    CrashScope crashScope("ShaderProgram::compileShader");
    const unsigned int shaderId = glCreateShader(shaderType);
    const char* sourcePointer = source.c_str();
    glShaderSource(shaderId, 1, &sourcePointer, 0);
    glCompileShader(shaderId);

    int compileSucceeded = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &compileSucceeded);
    if (compileSucceeded == GL_TRUE) {
        return shaderId;
    }

    int logLength = 0;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    std::string compileLog(static_cast<std::size_t>(logLength), '\0');
    glGetShaderInfoLog(shaderId, logLength, &logLength, &compileLog[0]);

    const char* shaderName = shaderType == GL_VERTEX_SHADER ? "vertex" : "fragment";
    LogError("ShaderProgram::compileShader", std::string("Failed to compile ") + shaderName + " shader: " + compileLog);
    glDeleteShader(shaderId);
    return 0;
}

// Links compiled shader stages into the owned program object.
bool ShaderProgram::linkProgram(unsigned int vertexShaderId, unsigned int fragmentShaderId) {
    CrashScope crashScope("ShaderProgram::linkProgram");
    if (vertexShaderId == 0 || fragmentShaderId == 0) {
        return false;
    }

    if (programId_ != 0) {
        glDeleteProgram(programId_);
        programId_ = 0;
    }

    programId_ = glCreateProgram();
    glAttachShader(programId_, vertexShaderId);
    glAttachShader(programId_, fragmentShaderId);
    glLinkProgram(programId_);
    glValidateProgram(programId_);

    int linkSucceeded = GL_FALSE;
    glGetProgramiv(programId_, GL_LINK_STATUS, &linkSucceeded);
    if (linkSucceeded != GL_TRUE) {
        int logLength = 0;
        glGetProgramiv(programId_, GL_INFO_LOG_LENGTH, &logLength);
        std::string linkLog(static_cast<std::size_t>(logLength), '\0');
        glGetProgramInfoLog(programId_, logLength, &logLength, &linkLog[0]);
        LogError("ShaderProgram::linkProgram", "Failed to link shader program: " + linkLog);
        glDeleteProgram(programId_);
        programId_ = 0;
    }

    if (programId_ != 0) {
        glDetachShader(programId_, vertexShaderId);
        glDetachShader(programId_, fragmentShaderId);
    }

    glDeleteShader(vertexShaderId);
    glDeleteShader(fragmentShaderId);
    return programId_ != 0;
}
