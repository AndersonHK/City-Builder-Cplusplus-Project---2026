#pragma once

#include <string>

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    bool loadFromFile(const std::string& shaderFilePath);
    void bind() const;
    unsigned int programId() const;

private:
    unsigned int compileShader(unsigned int shaderType, const std::string& source) const;
    bool linkProgram(unsigned int vertexShaderId, unsigned int fragmentShaderId);

    unsigned int programId_;
};
