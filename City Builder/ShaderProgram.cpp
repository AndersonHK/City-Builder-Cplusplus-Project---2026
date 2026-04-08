#include "ShaderProgram.h"

#include <GL/glew.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
struct ShaderSources {
    std::string vertexSource;
    std::string fragmentSource;
};

ShaderSources LoadShaderSources(const std::string& shaderFilePath) {
    std::ifstream shaderStream(shaderFilePath.c_str());
    if (!shaderStream.is_open()) {
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
            sourceBuilders[static_cast<int>(shaderType)] << line << '\n';
        }
    }

    ShaderSources shaderSources;
    shaderSources.vertexSource = sourceBuilders[0].str();
    shaderSources.fragmentSource = sourceBuilders[1].str();
    return shaderSources;
}
}

ShaderProgram::ShaderProgram()
    : programId_(0) {
}

ShaderProgram::~ShaderProgram() {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }
}

bool ShaderProgram::loadFromFile(const std::string& shaderFilePath) {
    const ShaderSources shaderSources = LoadShaderSources(shaderFilePath);
    const unsigned int vertexShaderId = compileShader(GL_VERTEX_SHADER, shaderSources.vertexSource);
    const unsigned int fragmentShaderId = compileShader(GL_FRAGMENT_SHADER, shaderSources.fragmentSource);
    return linkProgram(vertexShaderId, fragmentShaderId);
}

void ShaderProgram::bind() const {
    glUseProgram(programId_);
}

unsigned int ShaderProgram::programId() const {
    return programId_;
}

unsigned int ShaderProgram::compileShader(unsigned int shaderType, const std::string& source) const {
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
    std::cerr << "Failed to compile " << shaderName << " shader: " << compileLog << std::endl;
    glDeleteShader(shaderId);
    return 0;
}

bool ShaderProgram::linkProgram(unsigned int vertexShaderId, unsigned int fragmentShaderId) {
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
        std::cerr << "Failed to link shader program: " << linkLog << std::endl;
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
