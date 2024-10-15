#include <SDL2/SDL_events.h>
#include <cmath>
#include <cstdint>
#ifdef WIN32
#include <SDL.h>
#undef main
#else
#include <SDL2/SDL.h>
#endif

#include <GL/glew.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

std::string to_string(std::string_view str) {
    return std::string(str.begin(), str.end());
}

void sdl2_fail(std::string_view message) {
    throw std::runtime_error(to_string(message) + SDL_GetError());
}

void glew_fail(std::string_view message, GLenum error) {
    throw std::runtime_error(
        to_string(message) +
        reinterpret_cast<const char *>(glewGetErrorString(error)));
}

const char vertex_shader_source[] =
    R"(#version 330 core

uniform mat4 view;

layout (location = 0) in vec2 in_position;
layout (location = 1) in float in_value;
uniform bool is_iso;

out vec4 color;

float hue2rgb(float f1, float f2, float hue) {
    if (hue < 0.0)
        hue += 1.0;
    else if (hue > 1.0)
        hue -= 1.0;
    float res;
    if ((6.0 * hue) < 1.0)
        res = f1 + (f2 - f1) * 6.0 * hue;
    else if ((2.0 * hue) < 1.0)
        res = f2;
    else if ((3.0 * hue) < 2.0)
        res = f1 + (f2 - f1) * ((2.0 / 3.0) - hue) * 6.0;
    else
        res = f1;
    return res;
}

vec3 hsl2rgb(vec3 hsl) {
    vec3 rgb;
    
    if (hsl.y == 0.0) {
        rgb = vec3(hsl.z); // Luminance
    } else {
        float f2;
        
        if (hsl.z < 0.5)
            f2 = hsl.z * (1.0 + hsl.y);
        else
            f2 = hsl.z + hsl.y - hsl.y * hsl.z;
            
        float f1 = 2.0 * hsl.z - f2;
        
        rgb.r = hue2rgb(f1, f2, hsl.x + (1.0/3.0));
        rgb.g = hue2rgb(f1, f2, hsl.x);
        rgb.b = hue2rgb(f1, f2, hsl.x - (1.0/3.0));
    }   
    return rgb;
}

vec3 hsl2rgb(float h, float s, float l) {
    return hsl2rgb(vec3(h, s, l));
}

void main()
{
    gl_Position = view * vec4(in_position, 0.0, 1.0);
    if (is_iso) {
        color = vec4(1.0, 1.0, 1.0, 1.0);
    } else {
        color = vec4(hsl2rgb(in_value, 1, in_value), 1.0);
    }
}
)";

const char fragment_shader_source[] =
    R"(#version 330 core

in vec4 color;

layout (location = 0) out vec4 out_color;

void main()
{
    out_color = color;
}
)";

GLuint create_shader(GLenum type, const char *source) {
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint status;
    glGetShaderiv(result, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint info_log_length;
        glGetShaderiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetShaderInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Shader compilation failed: " + info_log);
    }
    return result;
}

GLuint create_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint result = glCreateProgram();
    glAttachShader(result, vertex_shader);
    glAttachShader(result, fragment_shader);
    glLinkProgram(result);

    GLint status;
    glGetProgramiv(result, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint info_log_length;
        glGetProgramiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetProgramInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Program linkage failed: " + info_log);
    }

    return result;
}

struct vec2 {
    float x;
    float y;

    inline bool valid() { return x != INFINITY && y != INFINITY; }
};

struct vertex {
    vec2 position;
    std::uint8_t color[4];
};

void sq_mesh(std::vector<vec2> &mesh, int cols, int &rows) {
    float xstep = 2.0 / cols;
    float ystep = xstep / 2 * tan(M_PI / 3);

    rows = cols * xstep / ystep;

    float xsize = (cols - 1) * xstep + 0.5 * xstep;
    float ysize = (rows - 1) * ystep;

    float xoffset = (2 - xsize) / 2;
    float yoffset = (2 - ysize) / 2;

    float xstart = -1 + xoffset;
    float ystart = -1 + yoffset;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            mesh.push_back({xstart + c * xstep + 0.5f * (r % 2) * xstep,
                            ystart + r * ystep});
        }
    }
}

void fill_mesh_indices(std::vector<uint32_t> &indices, std::vector<vec2> &mesh,
                       int cols, int rows) {
    for (int r = 0; r + 1 < rows; r += 2) {
        for (int c = 0; c + 1 < cols; c++) {
            indices.push_back(r * cols + c);
            indices.push_back((r + 1) * cols + c);
            indices.push_back(r * cols + (c + 1));

            indices.push_back((r + 1) * (cols) + (c));
            indices.push_back((r) * (cols) + (c + 1));
            indices.push_back((r + 1) * (cols) + (c + 1));

            if (r + 2 < rows) {
                indices.push_back((r + 2) * cols + c);
                indices.push_back((r + 1) * cols + c);
                indices.push_back((r + 2) * cols + (c + 1));

                indices.push_back((r + 1) * (cols) + (c));
                indices.push_back((r + 2) * (cols) + (c + 1));
                indices.push_back((r + 1) * (cols) + (c + 1));
            }
        }
    }
}

typedef struct ball {
    float c;
    float r;
    float x;
    float y;
} ball_t;

ball_t balls[] = {(ball){.c = -0.8, .r = 0.4, .x = 0.8, .y = -0.2},
                  (ball){.c = -0.4, .r = 0.6, .x = -0.5, .y = 0.7},
                  (ball){.c = 1, .r = 0.2, .x = 0.1, .y = -0.9},
                  (ball){.c = 1, .r = 0.9, .x = -0.3, .y = 0.3},
                  (ball){.c = -2, .r = 0.5, .x = 0.6, .y = 0.4},
                  (ball){.c = 1.2, .r = 0.3, .x = 0.0, .y = -0.5},
                  (ball){.c = 1.5, .r = 0.7, .x = -0.8, .y = 0.1},
                  (ball){.c = -0.3, .r = 0.1, .x = 0.4, .y = -0.6},
                  (ball){.c = 0.4, .r = 0.8, .x = -0.1, .y = 0.9},
                  (ball){.c = -0.9, .r = 0.4, .x = 0.2, .y = -0.3}};

int edge_index(int i, int j, int cols, int rows) {
    int ri = i / cols;
    int rj = j / cols;

    if (ri > rj)
        return edge_index(j, i, cols, rows);

    int ci = i % cols;
    int cj = j % cols;

    int base = ri * cols * 3;

    if (ri == rj) {
        if (ci + 1 == cj)
            return base + ci;

        if (cj + 1 == ci)
            return base + cj;

        return -1;
    }

    if (ri + 1 == rj) {
        if (ci == cj)
            return base + cols + ci * 2;

        if (ri % 2 == 0 && cj + 1 == ci)
            return base + cols + cj * 2 + 1;

        if (ri % 2 == 1 && ci + 1 == cj)
            return base + cols + ci * 2 + 1;

        return -1;
    }

    return -1;
}

vec2 find_point(vec2 a, vec2 b, float va, float vb, float iso_value) {
    if (va > vb)
        return find_point(b, a, vb, va, iso_value);

    if (va >= iso_value || vb <= iso_value)
        return {.x = INFINITY, .y = INFINITY};

    float part = (iso_value - va) / (vb - va);
    float x = a.x + part * (b.x - a.x);
    float y = a.y + part * (b.y - a.y);

    return {.x = x, .y = y};
}

void fill_iso_points(std::vector<vec2> &mesh, std::vector<float> &values,
                     std::vector<uint32_t> &indices, std::vector<vec2> &points,
                     std::vector<uint32_t> &point_indices, int cols, int rows,
                     std::vector<float> iso_values) {
    int layer_size = 6 * cols * rows;
    points.resize(iso_values.size() * layer_size,
                  {.x = INFINITY, .y = INFINITY});

    for (int k = 0; k < iso_values.size(); k++) {
        float iso_value = iso_values[k];

        for (int i = 0; i < indices.size(); i += 3) {
            int ia = indices[i];
            float va = values[ia];
            vec2 a = mesh[ia];

            int ib = indices[i + 1];
            float vb = values[ib];
            vec2 b = mesh[ib];

            int ic = indices[i + 2];
            float vc = values[ic];
            vec2 c = mesh[ic];

            int iab = k * layer_size + edge_index(ia, ib, cols, rows);
            int ibc = k * layer_size + edge_index(ib, ic, cols, rows);
            int ica = k * layer_size + edge_index(ic, ia, cols, rows);

            points[iab] = find_point(a, b, va, vb, iso_value);
            points[ibc] = find_point(b, c, vb, vc, iso_value);
            points[ica] = find_point(c, a, vc, va, iso_value);

            int count = 0;
            if (points[iab].valid())
                count++;
            if (points[ibc].valid())
                count++;
            if (points[ica].valid())
                count++;

            if (count == 2) {
                if (points[iab].valid())
                    point_indices.push_back(iab);
                if (points[ibc].valid())
                    point_indices.push_back(ibc);
                if (points[ica].valid())
                    point_indices.push_back(ica);
            }
        }
    }
}

void fill_values(std::vector<vec2> &mesh, std::vector<float> &values,
                 float delta, float &minv, float &maxv) {
    values.resize(mesh.size());
    maxv = -INFINITY;
    minv = INFINITY;

    for (ball_t &ball : balls) {
        float speed = delta * 0.1f; // Speed of the ball
        ball.x += speed * ball.c *
                  cos(ball.c); // Using color value to alter the trajectory
        ball.y += speed * ball.c *
                  sin(ball.c); // Using color value to alter the trajectory

        if (ball.x > 2.0)
            ball.x = -2.0;
        if (ball.x < -2.0)
            ball.x = 2.0;
        if (ball.y > 2.0)
            ball.y = -2.0;
        if (ball.y < -2.0)
            ball.y = 2.0;
    }

    for (int i = 0; i < mesh.size(); i++) {
        values[i] = 0;

        for (ball_t ball : balls) {
            values[i] +=
                ball.c *
                std::exp(-((mesh[i].x - ball.x) * (mesh[i].x - ball.x) +
                           (mesh[i].y - ball.y) * (mesh[i].y - ball.y)) /
                         (ball.r * ball.r));
        }

        maxv = std::max(maxv, values[i]);
        minv = std::min(minv, values[i]);
    }

    for (int i = 0; i < mesh.size(); i++) {
        values[i] = (values[i] - minv) / (maxv - minv);
    }
}

void fill_iso_values(int isos, std::vector<float> &iso_values) {
    iso_values.resize(isos);

    for (int i = 0; i < isos; i++)
        iso_values[i] = (float)(i + 1) / (isos + 1);
}

int main() try {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        sdl2_fail("SDL_Init: ");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_Window *window = SDL_CreateWindow(
        "Graphics course practice 3", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

    if (!window)
        sdl2_fail("SDL_CreateWindow: ");

    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        sdl2_fail("SDL_GL_CreateContext: ");

    SDL_GL_SetSwapInterval(0);

    if (auto result = glewInit(); result != GLEW_NO_ERROR)
        glew_fail("glewInit: ", result);

    if (!GLEW_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 is not supported");

    glClearColor(0.8f, 0.8f, 1.f, 0.f);

    auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
    auto fragment_shader =
        create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    auto program = create_program(vertex_shader, fragment_shader);

    GLuint view_location = glGetUniformLocation(program, "view");
    GLuint is_iso_location = glGetUniformLocation(program, "is_iso");

    auto last_frame_start = std::chrono::high_resolution_clock::now();

    std::vector<vec2> vertices = {};
    std::vector<float> values = {};
    std::vector<vec2> points = {};
    std::vector<uint32_t> point_indices = {};
    std::vector<uint32_t> indices = {};
    std::vector<float> iso_values = {};

    GLuint vbo_vertices;
    glGenBuffers(1, &vbo_vertices);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void *)(0));

    GLuint vbo_values;
    glGenBuffers(1, &vbo_values);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_values);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void *)(0));

    GLuint vao_iso;
    glGenVertexArrays(1, &vao_iso);
    glBindVertexArray(vao_iso);

    GLuint vbo_iso_points;
    glGenBuffers(1, &vbo_iso_points);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_iso_points);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vec2), (void *)(0));

    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    GLuint ebo_points;
    glGenBuffers(1, &ebo_points);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_points);

    int cols = 10;
    int rows;
    int isos = 10;
    bool first_time = true;

    float time = 0.f;
    float minv, maxv;

    bool running = true;
    while (running) {
        bool mesh_changed = false, isos_changed = false;
        for (SDL_Event event; SDL_PollEvent(&event);)
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                    width = event.window.data1;
                    height = event.window.data2;
                    glViewport(0, 0, width, height);
                    break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mouse_x = event.button.x;
                    int mouse_y = event.button.y;

                    mesh_changed = true;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                }

                break;
            }
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_LEFT) {
                    if (cols > 1) {
                        cols--;
                        mesh_changed = true;
                    }
                } else if (event.key.keysym.sym == SDLK_RIGHT) {
                    cols++;
                    mesh_changed = true;
                } else if (event.key.keysym.sym == SDLK_DOWN) {
                    if (isos > 1) {
                        isos--;
                        isos_changed = true;
                    }
                } else if (event.key.keysym.sym == SDLK_UP) {
                    isos++;
                    isos_changed = true;
                }
                break;
            }

        if (mesh_changed || first_time) {
            vertices.clear();
            indices.clear();
            sq_mesh(vertices, cols, rows);
            fill_mesh_indices(indices, vertices, cols, rows);

            glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vec2),
                         vertices.data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         indices.size() * sizeof(uint32_t), indices.data(),
                         GL_STATIC_DRAW);
        }

        if (isos_changed || first_time) {
            iso_values.clear();
            fill_iso_values(isos, iso_values);
        }

        first_time = false;

        if (!running)
            break;

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::duration<float>>(
                       now - last_frame_start)
                       .count();
        last_frame_start = now;
        time += dt;

        glClear(GL_COLOR_BUFFER_BIT);

        float factorw = (float)height / std::max(width, height);
        float factorh = (float)width / std::max(width, height);

        float view[16] = {
            factorw, 0.f, 0.f, 0.f, 0.f, factorh, 0.f, 0.f,
            0.f,     0.f, 1.f, 0.f, 0.f, 0.f,     0.f, 1.f,
        };

        glUseProgram(program);
        glUniformMatrix4fv(view_location, 1, GL_TRUE, view);

        glBindVertexArray(vao);
        glUniform1i(is_iso_location, false);

        fill_values(vertices, values, dt, minv, maxv);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_values);
        glBufferData(GL_ARRAY_BUFFER, values.size() * sizeof(float),
                     values.data(), GL_STATIC_DRAW);

        glPointSize(10);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

        points.clear();
        point_indices.clear();
        fill_iso_points(vertices, values, indices, points, point_indices, cols,
                        rows, iso_values);

        glBindVertexArray(vao_iso);
        glUniform1i(is_iso_location, true);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_iso_points);
        glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(vec2),
                     points.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_points);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     point_indices.size() * sizeof(uint32_t),
                     point_indices.data(), GL_STATIC_DRAW);

        glLineWidth(3.f);
        glDrawElements(GL_LINES, point_indices.size(), GL_UNSIGNED_INT, 0);

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
} catch (std::exception const &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
