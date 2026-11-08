/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "OpenGLSupport.h"

#include <unordered_map>
#include <cstring>
#include <vector>

#include <assert.h>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"

namespace melonDS
{

using Platform::Log;
using Platform::LogLevel;

namespace OpenGL
{

struct ShaderCacheEntry
{
    u32 Length;
    u8* Data;
    u32 BinaryFormat;

    ShaderCacheEntry(u8* data, u32 length, u32 binaryFmt)
        : Length(length), Data(data), BinaryFormat(binaryFmt)
    {
        assert(data != nullptr);
    }

    ShaderCacheEntry(const ShaderCacheEntry&) = delete;
    ShaderCacheEntry(ShaderCacheEntry&& other)
    {
        Data = other.Data;
        Length = other.Length;
        BinaryFormat = other.BinaryFormat;

        other.Data = nullptr;
        other.Length = 0;
        other.BinaryFormat = 0;
    }

    ~ShaderCacheEntry()
    {
        if (Data) // check whether it was moved
            delete[] Data;
    }
};

std::unordered_map<u64, ShaderCacheEntry> ShaderCache;
std::vector<u64> NewShaders;

constexpr u32 ShaderCacheMagic = 0x11CAC4E1;
constexpr u32 ShaderCacheVersion = 1;

void LoadShaderCache()
{
    // for now the shader cache only contains only compute shaders
    // because they take the longest to compile
    Platform::FileHandle* file = Platform::OpenLocalFile("shadercache", Platform::FileMode::Read);
    if (file == nullptr)
    {
        Log(LogLevel::Error, "Could not find shader cache\n");
        return;
    }

    u32 magic, version, numPrograms;
    if (Platform::FileRead(&magic, 4, 1, file) != 1 || magic != ShaderCacheMagic)
    {
        Log(LogLevel::Error, "Shader cache file has invalid magic\n");
        goto fileInvalid;
    }

    if (Platform::FileRead(&version, 4, 1, file) != 1 || version != ShaderCacheVersion)
    {
        Log(LogLevel::Error, "Shader cache file has bad version\n");
        goto fileInvalid;
    }

    if (Platform::FileRead(&numPrograms, 4, 1, file) != 1)
    {
        Log(LogLevel::Error, "Shader cache file invalid program count\n");
        goto fileInvalid;
    }

    // not the best approach, because once changes pile up
    // we read and overwrite the old files
    for (u32 i = 0; i < numPrograms; i++)
    {
        int error = 3;

        u32 length, binaryFormat;
        u64 sourceHash;
        error -= Platform::FileRead(&sourceHash, 8, 1, file);
        error -= Platform::FileRead(&length, 4, 1, file);
        error -= Platform::FileRead(&binaryFormat, 4, 1, file);

        if (error != 0)
        {
            Log(LogLevel::Error, "Invalid shader cache entry\n");
            goto fileInvalid;
        }

        u8* data = new u8[length];
        if (Platform::FileRead(data, length, 1, file) != 1)
        {
            Log(LogLevel::Error, "Could not read shader cache entry data\n");
            delete[] data;
            goto fileInvalid;
        }

        ShaderCache.erase(sourceHash);
        ShaderCache.emplace(sourceHash, ShaderCacheEntry(data, length, binaryFormat));
    }

fileInvalid:
    Platform::CloseFile(file);
}

void SaveShaderCache()
{
    Platform::FileHandle* file = Platform::OpenLocalFile("shadercache", Platform::FileMode::ReadWrite);

    if (file == nullptr)
    {
        Log(LogLevel::Error, "Could not open or create shader cache file\n");
        return;
    }

    int written = 3;
    u32 magic = ShaderCacheMagic, version = ShaderCacheVersion, numPrograms = ShaderCache.size();
    written -= Platform::FileWrite(&magic, 4, 1, file);
    written -= Platform::FileWrite(&version, 4, 1, file);
    written -= Platform::FileWrite(&numPrograms, 4, 1, file);

    if (written != 0)
    {
        Log(LogLevel::Error, "Could not write shader cache header\n");
        goto writeError;
    }

    Platform::FileSeek(file, 0, Platform::FileSeekOrigin::End);

    printf("new shaders %zu\n", NewShaders.size());

    for (u64 newShader : NewShaders)
    {
        int error = 4;
        auto it = ShaderCache.find(newShader);

        error -= Platform::FileWrite(&it->first, 8, 1, file);
        error -= Platform::FileWrite(&it->second.Length, 4, 1, file);
        error -= Platform::FileWrite(&it->second.BinaryFormat, 4, 1, file);
        error -= Platform::FileWrite(it->second.Data, it->second.Length, 1, file);

        if (error != 0)
        {
            Log(LogLevel::Error, "Could not insert new shader cache entry\n");
            goto writeError;
        }
    }

writeError:
    Platform::CloseFile(file);

    NewShaders.clear();
}

bool CompilerShader(GLuint& id, const std::string& source, const std::string& name, const std::string& type)
{
    int res;

    if (!glCreateShader)
    {
        Log(LogLevel::Error, "OpenGL: Cannot build shader program, OpenGL hasn't been loaded\n");
        return false;
    }

    const char* sourceC = source.c_str();
    int len = source.length();
    glShaderSource(id, 1, &sourceC, &len);

    glCompileShader(id);

    glGetShaderiv(id, GL_COMPILE_STATUS, &res);
    if (res != GL_TRUE)
    {
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &res);
        if (res < 1) res = 1024;
        char* log = new char[res+1];
        glGetShaderInfoLog(id, res+1, NULL, log);
        Log(LogLevel::Error, "OpenGL: failed to compile %s shader %s: %s\n", type.c_str(), name.c_str(), log);
        Log(LogLevel::Debug, "shader source:\n--\n%s\n--\n", source.c_str());
        delete[] log;

        glDeleteShader(id);

        return false;
    }

    return true;
}

bool LinkProgram(GLuint& result, GLuint* ids, int numIds)
{
    int res;

    if (!glLinkProgram)
    {
        Log(LogLevel::Error, "OpenGL: Cannot link shader program, OpenGL hasn't been loaded\n");
        return false;
    }

    for (int i = 0; i < numIds; i++)
    {
        glAttachShader(result, ids[i]);
    }

    glLinkProgram(result);

    for (int i = 0; i < numIds; i++)
        glDetachShader(result, ids[i]);

    glGetProgramiv(result, GL_LINK_STATUS, &res);
    if (res != GL_TRUE)
    {
        glGetProgramiv(result, GL_INFO_LOG_LENGTH, &res);
        if (res < 1) res = 1024;
        char* log = new char[res+1];
        glGetProgramInfoLog(result, res+1, NULL, log);
        Log(LogLevel::Error, "OpenGL: failed to link shader program: %s\n", log);
        delete[] log;

        return false;
    }

    return true;
}

bool CompileComputeProgram(GLuint& result, const std::string& source, const std::string& name)
{
    result = glCreateProgram();

    /*u64 sourceHash = XXH64(source.data(), source.size(), 0);
    auto it = ShaderCache.find(sourceHash);
    if (it != ShaderCache.end())
    {
        glProgramBinary(result, it->second.BinaryFormat, it->second.Data, it->second.Length);

        GLint linkStatus;
        glGetProgramiv(result, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_TRUE)
        {
            Log(LogLevel::Info, "Restored shader %s from cache\n", name.c_str());
            return true;
        }
        else
        {
        }
    }*/
    Log(LogLevel::Error, "Shader %s from cache was rejected\n", name.c_str());

    GLuint shader;
    bool linkingSucess = false;

    if (!glCreateShader || !glDeleteShader)
        goto error;

    shader = glCreateShader(GL_COMPUTE_SHADER);

    if (!CompilerShader(shader, source, name, "compute"))
        goto error;

    linkingSucess = LinkProgram(result, &shader, 1);

error:
    glDeleteShader(shader);

    if (!linkingSucess)
    {
        glDeleteProgram(result);
    }
    /*else
    {
        GLint length;
        GLenum format;
        glGetProgramiv(result, GL_PROGRAM_BINARY_LENGTH, &length);

        u8* buffer = new u8[length];
        glGetProgramBinary(result, length, nullptr, &format, buffer);

        ShaderCache.emplace(sourceHash, ShaderCacheEntry(buffer, length, format));
        NewShaders.push_back(sourceHash);
    }*/

    return linkingSucess;
}

#ifdef MELONDS_GLES
// WC09 fork: the renderer's GLSL 1.40 shaders, adapted to GLSL ES 3.00 (version line, default
// precisions, explicit output locations instead of glBindFragDataLocation)
#ifdef MELONDS_GLES_30
static bool FetchDisabled = false;   // set when a shader using framebuffer fetch didn't compile
static bool HasFramebufferFetch()
{
    if (FetchDisabled) return false;
    static int has = -1;
    if (has < 0)
    {
        has = 0;
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; i++)
        {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i);
            if (e && !strcmp(e, "GL_EXT_shader_framebuffer_fetch")) has = 1;
        }
        Log(LogLevel::Info, "GLES 3.0: framebuffer fetch %s\n", has ? "available" : "MISSING (attribute masks off)");
    }
    return has == 1;
}
#endif
static std::string ToES(const std::string& src, bool fragment, const std::initializer_list<AttributeTarget>& outs)
{
    std::string s = src;
    size_t v = s.find("#version");
    size_t eol = v == std::string::npos ? std::string::npos : s.find('\n', v);
    std::string head = "#version 300 es\n"
        "precision highp float;\nprecision highp int;\n"
        "precision highp sampler2D;\nprecision highp sampler2DArray;\n"
        "precision highp isampler2D;\nprecision highp usampler2D;\n"
        "precision highp isampler2DArray;\nprecision highp usampler2DArray;\n";
    if (v != std::string::npos) s = s.substr(0, v) + head + s.substr(eol + 1);
    else s = head + s;
#ifdef MELONDS_GLES_30
    // no per-attachment colour masks in ES 3.0: a second output is masked in the shader (it reads
    // the current value with framebuffer fetch and keeps the masked channels, see GLES_Compat.h)
    // both outputs are fetched and masked, so the renderer never has to switch draw buffers
    std::string maskOut, maskOut0;
    if (fragment && HasFramebufferFetch())
        for (const AttributeTarget& t : outs)
            if (t.Location == 1) maskOut = t.Name;
    if (!maskOut.empty())
        for (const AttributeTarget& t : outs)
            if (t.Location == 0) maskOut0 = t.Name;
#endif
    if (fragment)
        for (const AttributeTarget& t : outs)
        {
            // "out <type> <name>;" at global scope
            std::string nm = t.Name;
            size_t pos = 0;
            while ((pos = s.find("out ", pos)) != std::string::npos)
            {
                bool lineStart = pos == 0 || s[pos - 1] == '\n';
                size_t semi = s.find(';', pos);
                if (lineStart && semi != std::string::npos)
                {
                    std::string decl = s.substr(pos, semi - pos);
                    size_t sp = decl.find_last_of(" \t");
                    if (sp != std::string::npos && decl.substr(sp + 1) == nm)
                    {
                        std::string lay = "layout(location = " + std::to_string(t.Location) + ") ";
#ifdef MELONDS_GLES_30
                        if (nm == maskOut || (!maskOut0.empty() && nm == maskOut0)) lay += "in";   // "inout"
#endif
                        s.insert(pos, lay);
                        pos += lay.size();
                        break;
                    }
                }
                pos += 4;
            }
        }
    // "smooth" is the default anyway, and Apple's ES compiler rejects it as a qualifier
    for (size_t at = 0; (at = s.find("smooth ", at)) != std::string::npos; )
    {
        bool word = at == 0 || s[at - 1] == ' ' || s[at - 1] == '\n' || s[at - 1] == '\t';
        if (word) s.erase(at, 7); else at += 7;
    }
#ifdef MELONDS_GLES_30
    if (!maskOut.empty())
    {
        size_t m = s.find("void main()");
        if (m != std::string::npos)
        {
            s.replace(m, 11, "void wc09_main()");
            if (maskOut0.empty())
                s += "\nuniform vec4 uWC09Mask1;\nvoid main()\n{\n    vec4 wc09prev = " + maskOut + ";\n    wc09_main();\n    "
                     + maskOut + " = mix(wc09prev, " + maskOut + ", uWC09Mask1);\n}\n";
            else
                s += "\nuniform vec4 uWC09Mask0;\nuniform vec4 uWC09Mask1;\nvoid main()\n{\n    vec4 wc09prev0 = " + maskOut0 + ";\n    vec4 wc09prev = " + maskOut + ";\n    wc09_main();\n    "
                     + maskOut0 + " = mix(wc09prev0, " + maskOut0 + ", uWC09Mask0);\n    "
                     + maskOut + " = mix(wc09prev, " + maskOut + ", uWC09Mask1);\n}\n";
            size_t nl = s.find('\n');
            s.insert(nl + 1, "#extension GL_EXT_shader_framebuffer_fetch : require\n");
        }
    }
#endif
    return s;
}
#endif

static bool CompileVertexFragmentProgramOnce(GLuint& result,
    const std::string& vs_, const std::string& fs_,
    const std::string& name,
    const std::initializer_list<AttributeTarget>& vertexInAttrs,
    const std::initializer_list<AttributeTarget>& fragmentOutAttrs);

bool CompileVertexFragmentProgram(GLuint& result,
    const std::string& vs_, const std::string& fs_,
    const std::string& name,
    const std::initializer_list<AttributeTarget>& vertexInAttrs,
    const std::initializer_list<AttributeTarget>& fragmentOutAttrs)
{
    if (CompileVertexFragmentProgramOnce(result, vs_, fs_, name, vertexInAttrs, fragmentOutAttrs)) return true;
#ifdef MELONDS_GLES_30
    if (!FetchDisabled)
    {
        // retry without framebuffer fetch (attachment 1 is then written whole)
        Log(LogLevel::Warn, "%s: retrying without framebuffer fetch\n", name.c_str());
        FetchDisabled = true;
        return CompileVertexFragmentProgramOnce(result, vs_, fs_, name, vertexInAttrs, fragmentOutAttrs);
    }
#endif
    return false;
}

static bool CompileVertexFragmentProgramOnce(GLuint& result,
    const std::string& vs_, const std::string& fs_,
    const std::string& name,
    const std::initializer_list<AttributeTarget>& vertexInAttrs,
    const std::initializer_list<AttributeTarget>& fragmentOutAttrs)
{
#ifdef MELONDS_GLES
    const std::string vs = ToES(vs_, false, fragmentOutAttrs), fs = ToES(fs_, true, fragmentOutAttrs);
    if (const char* dump = getenv("WC09_DUMP_SHADERS"))   // for checking the ES sources offline
    {
        for (int k = 0; k < 2; k++)
            if (FILE* f = fopen((std::string(dump) + "/" + name + (k ? ".frag" : ".vert")).c_str(), "wb"))
            { const std::string& src = k ? fs : vs; fwrite(src.data(), 1, src.size(), f); fclose(f); }
    }
#else
    const std::string& vs = vs_; const std::string& fs = fs_;
#endif
    GLuint shaders[2] =
    {
        glCreateShader(GL_VERTEX_SHADER),
        glCreateShader(GL_FRAGMENT_SHADER)
    };
    result = glCreateProgram();

    bool linkingSucess = false;

    if (!CompilerShader(shaders[0], vs, name, "vertex"))
        goto error;

    if (!CompilerShader(shaders[1], fs, name, "fragment"))
        goto error;


    for (const AttributeTarget& target : vertexInAttrs)
    {
        glBindAttribLocation(result, target.Location, target.Name);
    }
    for (const AttributeTarget& target : fragmentOutAttrs)
    {
        glBindFragDataLocation(result, target.Location, target.Name);
    }

    linkingSucess = LinkProgram(result, shaders, 2);

error:
    glDeleteShader(shaders[1]);
    glDeleteShader(shaders[0]);

    if (!linkingSucess)
        glDeleteProgram(result);

    return linkingSucess;
}

}

}