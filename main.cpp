#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <vector>

// ======================
// STRUKTURY DANYCH
// ======================

// Reprezentuje model 3D (w przyszłości tutaj trafią dane z .obj)
struct Mesh {
  unsigned int VAO;
  unsigned int vertexCount;
};

enum PieceType { PAWN, ROOK, KNIGHT, BISHOP, QUEEN, KING };

// Reprezentuje konkretną figurę na planszy
struct Piece {
  PieceType type;
  bool isWhite;
  glm::vec3 worldPos;    // Aktualna pozycja w świecie (do animacji)
  glm::vec2 boardCoords; // Pozycja na szachownicy (np. 0-7, 0-7)

  // Metoda do aktualizacji pozycji na podstawie koordynatów szachownicy
  void updateWorldPos() {
    worldPos.x = boardCoords.x - 3.5f;
    worldPos.z = boardCoords.y - 3.5f;
    worldPos.y = 0.0f; // Podstawa figury na planszy
  }
};

// ======================
// SHADERY
// ======================

const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)";

const char *fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D texture1;
uniform vec3 objectColor;
uniform bool useTexture;
void main() {
    if(useTexture) FragColor = texture(texture1, TexCoord);
    else FragColor = vec4(objectColor, 1.0);
}
)";

// ======================
// FUNKCJE POMOCNICZE
// ======================

Mesh createCubeMesh();
Mesh createBoardMesh();
void checkCompileErrors(unsigned int shader, std::string type);

// ======================
// MAIN
// ======================

int main() {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow *window =
      glfwCreateWindow(1200, 720, "Chess 3D", nullptr, nullptr);
  glfwMakeContextCurrent(window);
  gladLoadGL(glfwGetProcAddress);
  glEnable(GL_DEPTH_TEST);

  // Kompilacja shaderów
  unsigned int vertex = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex, 1, &vertexShaderSource, NULL);
  glCompileShader(vertex);
  unsigned int fragment = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragment);
  unsigned int shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertex);
  glAttachShader(shaderProgram, fragment);
  glLinkProgram(shaderProgram);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  // Inicjalizacja Meshy
  Mesh boardMesh = createBoardMesh();
  Mesh piecePlaceholderMesh =
      createCubeMesh(); // W przyszłości tu będą różne meshe dla King.obj itd.

  // Inicjalizacja tekstury
  unsigned int boardTexture;
  glGenTextures(1, &boardTexture);
  glBindTexture(GL_TEXTURE_2D, boardTexture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  unsigned char pixels[] = {200, 200, 200, 50,  50,  50,
                            50,  50,  50,  200, 200, 200};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE,
               pixels);

  // ======================
  // INICJALIZACJA FIGUR (Addressable Pieces)
  // ======================
  std::vector<Piece> pieces;

  auto setupRow = [&](int row, bool white) {
    // Główna linia
    PieceType types[] = {ROOK, KNIGHT, BISHOP, QUEEN,
                         KING, BISHOP, KNIGHT, ROOK};
    for (int i = 0; i < 8; i++) {
      Piece p;
      p.type = types[i];
      p.isWhite = white;
      p.boardCoords = glm::vec2(i, row);
      p.updateWorldPos();
      pieces.push_back(p);
    }
    // Pionki
    int pawnRow = (row == 0) ? 1 : 6;
    for (int i = 0; i < 8; i++) {
      Piece p;
      p.type = PAWN;
      p.isWhite = white;
      p.boardCoords = glm::vec2(i, pawnRow);
      p.updateWorldPos();
      pieces.push_back(p);
    }
  };

  setupRow(0, true);  // Białe
  setupRow(7, false); // Czarne

  // ======================
  // PĘTLA RENDEROWANIA
  // ======================

  while (!glfwWindowShouldClose(window)) {
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shaderProgram);

    // Kamery
    glm::mat4 projection =
        glm::perspective(glm::radians(45.0f), 1200.0f / 720.0f, 0.1f, 100.0f);
    float time = (float)glfwGetTime();
    glm::mat4 view = glm::lookAt(
        glm::vec3(sin(time * 0.3f) * 12.0f, 10.0f, cos(time * 0.3f) * 12.0f),
        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                       glm::value_ptr(view));

    // 1. Rysuj planszę
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1,
                       GL_FALSE, glm::value_ptr(model));
    glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 1);
    glBindTexture(GL_TEXTURE_2D, boardTexture);
    glBindVertexArray(boardMesh.VAO);
    glDrawElements(GL_TRIANGLES, boardMesh.vertexCount, GL_UNSIGNED_INT, 0);

    // 2. Rysuj figury (Indywidualnie)
    glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 0);
    for (auto &p : pieces) {
      // Skalowanie zależne od typu (tymczasowe, póki nie mamy .obj)
      float h = 1.0f;
      if (p.type == PAWN)
        h = 0.7f;
      else if (p.type == KING)
        h = 2.0f;
      else if (p.type == QUEEN)
        h = 1.7f;

      model = glm::mat4(1.0f);
      model =
          glm::translate(model, p.worldPos + glm::vec3(0.0f, h / 2.0f, 0.0f));
      model = glm::scale(model, glm::vec3(0.6f, h, 0.6f));

      glm::vec3 color = p.isWhite ? glm::vec3(0.9f, 0.9f, 0.8f)
                                  : glm::vec3(0.1f, 0.1f, 0.15f);

      glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1,
                         GL_FALSE, glm::value_ptr(model));
      glUniform3fv(glGetUniformLocation(shaderProgram, "objectColor"), 1,
                   glm::value_ptr(color));

      glBindVertexArray(piecePlaceholderMesh.VAO);
      glDrawArrays(GL_TRIANGLES, 0, piecePlaceholderMesh.vertexCount);
    }

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwTerminate();
  return 0;
}

// ======================
// IMPLEMENTACJE FUNKCJI
// ======================

Mesh createBoardMesh() {
  float vertices[] = {-4.0f, 0.0f,  -4.0f, 0.0f, 4.0f, 4.0f, 0.0f,
                      -4.0f, 4.0f,  4.0f,  4.0f, 0.0f, 4.0f, 4.0f,
                      0.0f,  -4.0f, 0.0f,  4.0f, 0.0f, 0.0f};
  unsigned int indices[] = {0, 1, 2, 2, 3, 0};
  unsigned int VAO, VBO, EBO;
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glGenBuffers(1, &EBO);
  glBindVertexArray(VAO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  return {VAO, 6};
}

Mesh createCubeMesh() {
  float vertices[] = {
      -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 0.0f,
      0.5f,  0.5f,  -0.5f, 1.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
      -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,
      -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
      0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
      -0.5f, 0.5f,  0.5f,  0.0f, 1.0f, -0.5f, -0.5f, 0.5f,  0.0f, 0.0f,
      -0.5f, 0.5f,  0.5f,  1.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 1.0f, 1.0f,
      -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
      -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  0.5f,  1.0f, 0.0f,
      0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
      0.5f,  -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 0.0f, 1.0f,
      0.5f,  -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
      -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 1.0f,
      0.5f,  -0.5f, 0.5f,  1.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
      -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
      -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
      0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
      -0.5f, 0.5f,  0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f};
  unsigned int VAO, VBO;
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  return {VAO, 36};
}