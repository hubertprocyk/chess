#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <fstream>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ======================
// STRUKTURY DANYCH
// ======================

struct Mesh {
  unsigned int VAO;
  unsigned int vertexCount;
};

enum PieceType { PAWN, ROOK, KNIGHT, BISHOP, QUEEN, KING };

struct Piece {
  PieceType type;
  bool isWhite;
  glm::vec3 worldPos;
  glm::vec2 boardCoords;

  void updateWorldPos() {
    worldPos.x = boardCoords.x - 3.5f;
    worldPos.z = boardCoords.y - 3.5f;
    // Zakładamy, że modele mają swój środek ciężkości (origin) na samym dole
    // podstawy
    worldPos.y = 0.0f;
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

Mesh createBoardMesh();
void checkCompileErrors(unsigned int shader, std::string type);

// Prosty parser plików OBJ
Mesh loadOBJ(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cout << "ERROR: Cannot open model file: " << path << std::endl;
    return {0, 0};
  }

  std::vector<glm::vec3> temp_positions;
  std::vector<glm::vec2> temp_uvs;
  std::vector<float> vertexData;
  unsigned int vertexCount = 0;

  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string prefix;
    iss >> prefix;

    if (prefix == "v") {
      glm::vec3 pos;
      iss >> pos.x >> pos.y >> pos.z;
      temp_positions.push_back(pos);
    } else if (prefix == "vt") {
      glm::vec2 uv;
      iss >> uv.x >> uv.y;
      temp_uvs.push_back(uv);
    } else if (prefix == "f") {
      // Wczytaj wszystkie tokeny (wierzchołki) dla tej ściany
      std::vector<std::string> tokens;
      std::string token;
      while (iss >> token) {
        tokens.push_back(token);
      }

      // Triangulacja wielokątów (Triangle Fan) - działa dla trójkątów,
      // czworokątów itd.
      for (size_t i = 1; i < tokens.size() - 1; ++i) {
        size_t faceIndices[3] = {0, i, i + 1};

        for (int j = 0; j < 3; j++) {
          std::istringstream viss(tokens[faceIndices[j]]);
          std::string indexStr;

          // Pozycja
          std::getline(viss, indexStr, '/');
          int posIndex = std::stoi(indexStr) - 1;
          glm::vec3 pos = temp_positions[posIndex];

          vertexData.push_back(pos.x);
          vertexData.push_back(pos.y);
          vertexData.push_back(pos.z);

          // UV (z zabezpieczeniem przed brakiem danych)
          glm::vec2 uv(0.0f, 0.0f);
          if (std::getline(viss, indexStr, '/') && !indexStr.empty()) {
            int uvIndex = std::stoi(indexStr) - 1;
            if (uvIndex >= 0 && uvIndex < (int)temp_uvs.size()) {
              uv = temp_uvs[uvIndex];
            }
          }
          vertexData.push_back(uv.x);
          vertexData.push_back(uv.y);

          vertexCount++;
        }
      }
    }
  }

  unsigned int VAO, VBO;
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float),
               vertexData.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  std::cout << "Successfully loaded: " << path << " (" << vertexCount
            << " vertices)\n";
  return {VAO, vertexCount};
}

// ======================
// MAIN
// ======================

int main() {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow *window =
      glfwCreateWindow(1200, 720, "Szachy 3D - Modele OBJ", nullptr, nullptr);
  glfwMakeContextCurrent(window);
  gladLoadGL(glfwGetProcAddress);
  glEnable(GL_DEPTH_TEST);

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

  // ======================
  // ŁADOWANIE MODELI
  // ======================
  Mesh boardMesh = createBoardMesh();

  // Tablica mapująca PieceType na konkretny Mesh
  Mesh pieceMeshes[6];
  pieceMeshes[PAWN] = loadOBJ("models/Pawn.obj");
  pieceMeshes[ROOK] = loadOBJ("models/Rook.obj");
  pieceMeshes[KNIGHT] = loadOBJ("models/Knight.obj");
  pieceMeshes[BISHOP] = loadOBJ("models/Bishop.obj");
  pieceMeshes[QUEEN] = loadOBJ("models/Queen.obj");
  pieceMeshes[KING] = loadOBJ("models/King.obj");

  // Tekstura planszy
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
  // USTAWIENIE FIGUR NA PLANSZY
  // ======================
  std::vector<Piece> pieces;

  auto setupRow = [&](int row, bool white) {
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
  // PĘTLA GŁÓWNA
  // ======================
  while (!glfwWindowShouldClose(window)) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, true);

    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shaderProgram);

    glm::mat4 projection =
        glm::perspective(glm::radians(45.0f), 1200.0f / 720.0f, 0.1f, 100.0f);
    float time = (float)glfwGetTime();
    glm::mat4 view = glm::lookAt(
        glm::vec3(sin(time * 0.2f) * 12.0f, 8.0f, cos(time * 0.2f) * 12.0f),
        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                       glm::value_ptr(view));

    // 1. Płaszczyzna
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1,
                       GL_FALSE, glm::value_ptr(model));
    glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 1);
    glBindTexture(GL_TEXTURE_2D, boardTexture);
    glBindVertexArray(boardMesh.VAO);
    glDrawElements(GL_TRIANGLES, boardMesh.vertexCount, GL_UNSIGNED_INT, 0);

    // ==============================================================================
    // 2. Rysowanie figur (Indywidualnie z kontrolą orientacji i skali)
    // ==============================================================================
    glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 0);

    // --- GLOBALNE PARAMETRY DLA Armii ---
    // Zwiększamy skalę o 20% (1.2 zamiast 1.0) jak prosiłeś
    float globalObjScale = 1.2f;

    for (auto &p : pieces) {
      model = glm::mat4(1.0f);

      // Najpierw ustawiamy figurę na polu
      model = glm::translate(model, p.worldPos);

      // Zastosuj skalę ogólną (powiększenie)
      model = glm::scale(model, glm::vec3(globalObjScale));

      // --- KONTROLA ORIENTACJI (Rotacja) ---
      // Jeśli twoje figury leżą na boku (standard .obj często to ma na Z-up),
      // odkomentuj poniższą linię, aby je podnieść przed rotacją na osi Y:
      // model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f,
      // 0.0f));

      // Główna logika rotacji: figury patrzą na siebie
      // Zakładając, że model standardowo patrzy w stronę -Z (na zewnątrz od
      // strony kamery), musimy obrócić białe figury, aby patrzyły "na północ" -
      // na czarne (w stronę +Z). Czarne figury zostawiamy bez dodatkowej
      // rotacji (patrzą "na południe" - na białe).
      if (p.isWhite) {
        // Białe figury obracamy o 180 stopni, by stawiły czoła przeciwnikowi
        model = glm::rotate(model, glm::radians(180.0f),
                            glm::vec3(0.0f, 1.0f, 0.0f));
      } else {
        // Czarne figury zostawiamy tak, jak są (0 stopni rotacji Y)
        // Usunęliśmy stąd wcześniejszą rotację 180, bo sprawiała, że patrzą
        // "tyłem do walki".
      }

      // Ustaw kolor (Białe vs Czarne)
      glm::vec3 color = p.isWhite ? glm::vec3(0.9f, 0.9f, 0.8f)
                                  : glm::vec3(0.1f, 0.1f, 0.15f);

      // Przekaż macierze do shadera
      glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1,
                         GL_FALSE, glm::value_ptr(model));
      glUniform3fv(glGetUniformLocation(shaderProgram, "objectColor"), 1,
                   glm::value_ptr(color));

      // Pobierz i narysuj odpowiedni mesh dla typu figury
      Mesh currentMesh = pieceMeshes[p.type];
      if (currentMesh.vertexCount > 0) {
        glBindVertexArray(currentMesh.VAO);
        glDrawArrays(GL_TRIANGLES, 0, currentMesh.vertexCount);
      }
    }

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glfwTerminate();
  return 0;
}

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