#include <realtime_perception/shader_wrapper.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <resource_retriever/retriever.h>

namespace realtime_perception
{
// named constructor to compile from source
template <int L1, int L2>
ShaderWrapper ShaderWrapper::fromSource (GLchar const * (&v_source) [L1], GLchar const * (&f_source) [L2])
{
  return ShaderWrapper (v_source, f_source);
}

// named constructor to compile from files
ShaderWrapper ShaderWrapper::fromFiles (const std::string vertex_file, const std::string fragment_file)
{
  return fromFiles (vertex_file.c_str (), fragment_file.c_str ());
}
ShaderWrapper ShaderWrapper::fromFiles (const char* vertex_file, const char* fragment_file)
{
  std::string v_source = load_text_file (vertex_file);
  std::string f_source = load_text_file (fragment_file);

  const GLchar* vs[1] = {v_source.c_str () };
  const GLchar* fs[1] = {f_source.c_str () };
  return ShaderWrapper (vs, fs);
}

// make sure we delete everything upon deconstruction
ShaderWrapper::~ShaderWrapper()
{
  glDeleteProgram (prog);
  glDeleteShader (vertex_shader);
  glDeleteShader (fragment_shader);
}

// operator to get back the encapsulated program handle
ShaderWrapper::operator GLuint ()
{
  return prog;
}

// call operator enables the shader to be used in gl drawing calls
void ShaderWrapper::operator() ()
{
  glUseProgram (prog);
}

// templated constructor takes two char* arrays for vertex and fragment shader source code
template <int L1, int L2>
ShaderWrapper::ShaderWrapper (GLchar const * (&v_source) [L1], GLchar const * (&f_source) [L2])
{
  // compile shaders
  vertex_shader = compile (GL_VERTEX_SHADER, v_source);
  fragment_shader = compile (GL_FRAGMENT_SHADER, f_source);
  // link vertex and fragment shaders together
  prog = glCreateProgram();
  glAttachShader (prog, vertex_shader);
  glAttachShader (prog, fragment_shader);
  glLinkProgram (prog);
}

// compile function is templated on the number of lines in the shader
template <int L>
GLuint ShaderWrapper::compile (GLuint type, char const * (&shader_source) [L])
{
  GLuint shader = glCreateShader (type);
  glShaderSource (shader, L, shader_source, NULL);
  glCompileShader (shader);

  GLint compiled = GL_TRUE;
  glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled)
  {
    for (unsigned int i = 0; i < L; ++i)
      std::cerr << shader_source[i] << std::endl;
    GLint length = 0;
    glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &length);
    std::string log (length, ' ');
    glGetShaderInfoLog (shader, length, &length, &log[0]);
    if (type == GL_VERTEX_SHADER)
      throw std::logic_error (std::string("compiling vertex shader :").append(log));
    else if (type == GL_FRAGMENT_SHADER)
      throw std::logic_error (std::string("compiling fragment shader :").append(log));
    return -1;
  }
  return shader;
}

// loads a text file as a string
std::string ShaderWrapper::load_text_file (std::string file_name)
{
  resource_retriever::Retriever retriever;
  resource_retriever::MemoryResource res;
  try 
  {   
    res = retriever.get(file_name);
  }
  catch (resource_retriever::Exception& e)
  {   
    throw std::logic_error (std::string("could not open shader file: ").append(file_name));
    return "";
  }
  
  char* buf = (char*) malloc (sizeof(char) * (res.size + 1));
  memcpy (buf, res.data.get(), res.size);
  buf[res.size] = 0;

  return std::string (buf);
  
  //std::ifstream ifs (file_name.c_str());
  //std::string str ( (std::istreambuf_iterator<char> (ifs)),
  //                  std::istreambuf_iterator<char>());
  //return str;
}

} // end namespace


