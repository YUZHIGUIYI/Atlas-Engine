#include "ShaderStage.h"
#include "../loader/AssetLoader.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace Atlas {

    namespace Shader {

        std::string ShaderStage::sourceDirectory = "";

        ShaderStage::ShaderStage(int32_t type, std::string filename) : type(type), filename(filename) {

            auto path = sourceDirectory.length() != 0 ? sourceDirectory + "/" : "";
            path += filename;

            code = ReadShaderFile(path, true);

            lastModified = GetLastModified();

            ID = glCreateShader(type);

#ifdef AE_SHOW_LOG
            AtlasLog("Loaded shader file %s", filename.c_str());
#endif

        }

        ShaderStage::ShaderStage(ShaderStage* source) {

            code = source->code;
            macros = source->macros;
            filename = source->filename;
            lastModified = source->lastModified;

            for (std::list<ShaderConstant*>::iterator iterator = source->constants.begin(); iterator != source->constants.end(); iterator++) {
                ShaderConstant* constant = new ShaderConstant((*iterator)->GetValuedString().c_str());
                constants.push_back(constant);
            }

            ID = glCreateShader(source->type);

        }

        ShaderStage::~ShaderStage() {

            for (auto& constant : constants) {
                delete constant;
            }

            glDeleteShader(type);

        }

        bool ShaderStage::Reload() {

            time_t comp = GetLastModified();

            if (comp == lastModified) {
                return false;
            }

            lastModified = comp;

            auto path = sourceDirectory.length() != 0 ? sourceDirectory + "/" : "";
            path += filename;

            constants.clear();
            code = ReadShaderFile(path, true);

            return true;

        }

        void ShaderStage::AddMacro(std::string macro) {

            macros.push_back(macro);

        }

        void ShaderStage::RemoveMacro(std::string macro) {

            macros.remove(macro);

        }

        ShaderConstant* ShaderStage::GetConstant(std::string constant) {

            for (std::list<ShaderConstant*>::iterator iterator = constants.begin(); iterator != constants.end(); iterator++) {
                if ((*iterator)->GetName() == constant) {
                    return *iterator;
                }
            }

            return nullptr;

        }

        bool ShaderStage::Compile() {

            std::string composedCode;

#ifdef AE_API_GL
            composedCode.append("#version 430\n\n#define ENGINE_GL\n");
#elif AE_API_GLES
            composedCode.append("#version 320 es\n\nprecision highp float;\nprecision highp sampler2D;\
		\nprecision highp samplerCube;\nprecision highp sampler2DArrayShadow;\nprecision highp sampler2DArray;\
		\nprecision highp samplerCubeShadow;\nprecision highp int;\n#define ENGINE_GLES\n");
#endif

            for (auto& macro : macros) {
                composedCode.append("#define " + macro + "\n");
            }

            for (auto& constant : constants) {
                composedCode.append(constant->GetValuedString() + "\n");
            }

            composedCode.append(code);

            const char* convertedCode = composedCode.c_str();

            int compiled = 0;

            glShaderSource(ID, 1, &convertedCode, 0);
            glCompileShader(ID);
            glGetShaderiv(ID, GL_COMPILE_STATUS, &compiled);

            if (!compiled) {
#ifdef AE_SHOW_LOG
                int32_t shaderLogLength, length;
                glGetShaderiv(ID, GL_INFO_LOG_LENGTH, &shaderLogLength);
                auto shaderLog = std::vector<char>(shaderLogLength);
                glGetShaderInfoLog(ID, shaderLogLength, &length, shaderLog.data());

                if (type == AE_VERTEX_STAGE) {
                    AtlasLog("\n\nCompiling vertex stage failed:");
                }
                else if (type == AE_FRAGMENT_STAGE) {
					AtlasLog("\n\nCompiling fragment stage failed:");
                }
                else if (type == AE_GEOMETRY_STAGE) {
					AtlasLog("\n\nCompiling geometry stage failed:");
                }
                else if (type == AE_TESSELLATION_CONTROL_STAGE) {
					AtlasLog("\n\nCompiling tessellation control stage failed:");
                }
                else if (type == AE_TESSELLATION_EVALUATION_STAGE) {
					AtlasLog("\n\nCompiling tessellation evaluation stage failed:");
                }
                else if (type == AE_COMPUTE_STAGE) {
					AtlasLog("\n\nCompiling compute stage failed:");
                }

				AtlasLog("Compilation failed: %s\nError: %s", filename.c_str(), shaderLog.data());
#endif

                return false;

            }

            return true;

        }

        void ShaderStage::SetSourceDirectory(std::string directory) {

            sourceDirectory = directory;

        }

        std::string ShaderStage::ReadShaderFile(std::string filename, bool mainFile) {

            std::string shaderCode;
            std::ifstream shaderFile;
            std::stringstream shaderStream;

            shaderFile = Loader::AssetLoader::ReadFile(filename, std::ios::in);

            if (!shaderFile.is_open()) {
#ifdef AE_SHOW_LOG
				AtlasLog("Shader file %s not found", filename.c_str());
#endif
                throw AtlasException("Couldn't open shader file");
            }

            shaderStream << shaderFile.rdbuf();
            shaderFile.close();
            shaderCode = shaderStream.str();

            size_t filePathPosition = filename.find_last_of("/");
            filename.erase(filePathPosition + 1, filename.length() - 1);

            // Copy all includes into the code
            while (shaderCode.find("#include ") != std::string::npos) {

                size_t includePosition = shaderCode.find("#include ");
                size_t lineBreakPosition = shaderCode.find("\n", includePosition);

                size_t filenamePosition = shaderCode.find_first_of("\"<", includePosition) + 1;
                size_t filenameEndPosition = shaderCode.find_first_of("\">", filenamePosition);

                auto includeFilename = shaderCode.substr(filenamePosition, filenameEndPosition - filenamePosition);

                auto includeCode = ReadShaderFile(filename + includeFilename, false);

                auto codeBeforeInclude = shaderCode.substr(0, includePosition);
                auto codeAfterInclude = shaderCode.substr(lineBreakPosition, shaderCode.length() - 1);

                shaderCode = codeBeforeInclude + includeCode + codeAfterInclude;

            }

            // Find constants in the code (we have to consider that we don't want to change the constants in functions)
            if (mainFile) {

                int32_t openedCurlyBrackets = 0;

                for (size_t i = 0; i < shaderCode.length(); i++) {
                    if (shaderCode[i] == '{') {
                        openedCurlyBrackets++;
                    }
                    else if (shaderCode[i] == '}') {
                        openedCurlyBrackets--;
                    }
                    else if (shaderCode[i] == 'c' && openedCurlyBrackets == 0) {
                        // Check if its a constant
                        size_t position = shaderCode.find("const ", i);
                        if (position == i) {
                            // Create a new constant
                            size_t constantEndPosition = shaderCode.find(";", i);
                            auto constantString = shaderCode.substr(i, constantEndPosition - i + 1);
                            ShaderConstant* constant = new ShaderConstant(constantString);
                            constants.push_back(constant);
                            // Remove the constant expression from the code and reduce i
                            shaderCode.erase(i, constantEndPosition - i + 1);
                            i--;
                        }
                    }
                }

            }

            return shaderCode;

        }

        time_t ShaderStage::GetLastModified() {

            auto path = sourceDirectory.length() != 0 ? sourceDirectory + "/" : "";
            path += filename;

            struct stat result;
            if (stat(path.c_str(), &result) == 0) {
                auto mod_time = result.st_mtime;
                return mod_time;
            }
            return 0;

        }

    }

}