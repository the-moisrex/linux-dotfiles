#include "./pch.h"

using namespace std;

int main() {

    if (!glfwInit()) {
        return -1;
    }

    GLFWwindow *window = glfwCreateWindow(640, 480, "Hello World", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (glewInit() != GLEW_OK) {
        cerr << "Error GLEW" << endl;
        return -1;
    }

    cout << "OpenGL version: " << glGetString(GL_VERSION) << endl;

    mt19937 prng{random_device()()};
    uniform_real_distribution<GLfloat> dist{-1.0f, 1.0f};


    auto current = dist(prng);
    auto purpose = dist(prng);
    const GLfloat step = 0.01f;

    array<float, 6> positions{};

    unsigned int buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        if (abs(purpose - current) <= step) {
            // change
            purpose = dist(prng);
        } else {
            current += (purpose - current > 0.0f ? 1.0f : -1.0f) * step;
        }
        positions = array<GLfloat, 6>{{
                                              -current, -current,
                                              0.0f, current,
                                              current, -current
                                      }};
        glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(float), positions.data(), GL_STATIC_READ);

        glDrawArrays(GL_TRIANGLES, 0, positions.size() / 2);


        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 0;
}

