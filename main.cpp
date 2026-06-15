#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "pgn.h" // <-- moduł parsera (klasa ChessGame, MoveDelta)

// ======================
// STRUKTURY DANYCH
// ======================

struct Mesh {
  unsigned int VAO;
  unsigned int vertexCount;
};

enum PieceType { PAWN, ROOK, KNIGHT, BISHOP, QUEEN, KING };

// Czasy trwania animacji (w sekundach).
static const float MOVE_DURATION = 0.45f; // przesuniecie figury
static const float DEATH_DURATION = 0.35f; // zanikanie zbitej figury
static const float HOP_HEIGHT = 0.6f;      // wysokosc luku podczas ruchu

struct Piece {
  PieceType type;
  bool isWhite;
  glm::vec2 boardCoords;  // logiczne pole (kolumna, rzad)
  glm::vec3 worldPos;     // aktualna pozycja renderowana

  // --- animacja ruchu ---
  bool moving = false;
  glm::vec3 startPos = glm::vec3(0.0f);
  glm::vec3 endPos = glm::vec3(0.0f);
  float moveT = 1.0f;     // 0..1 (1 = w spoczynku)
  char promoteTo = 0;     // jesli != 0, zmien typ po dotarciu na pole

  // --- animacja bicia ---
  bool dying = false;
  float deathT = 0.0f;    // 0..1 (1 = zniknieta)

  // Pole planszy -> pozycja w swiecie (origin pola, srodek u podstawy).
  static glm::vec3 squareToWorld(glm::vec2 c) {
    return glm::vec3(c.x - 3.5f, 0.0f, c.y - 3.5f);
  }

  // Natychmiastowe ustawienie na polu (bez animacji).
  void placeAt(glm::vec2 c) {
    boardCoords = c;
    worldPos = squareToWorld(c);
    moving = false;
    moveT = 1.0f;
    dying = false;
    deathT = 0.0f;
    promoteTo = 0;
  }

  // Rozpoczyna animacje przesuniecia na nowe pole.
  void startMove(glm::vec2 c) {
    startPos = worldPos;
    endPos = squareToWorld(c);
    boardCoords = c;
    moveT = 0.0f;
    moving = true;
  }
};

// ======================
// SHADERY
// ======================

const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
out vec2 TexCoord;
out vec3 FragPos;
out vec3 Normal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    vec4 world = model * vec4(aPos, 1.0);
    FragPos = world.xyz;
    // Skala jest jednorodna (powiekszenie + zanikanie), wiec do normalnych
    // wystarczy mat3(model); normalize() naprawia dlugosc.
    Normal = mat3(model) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * world;
}
)";

const char *fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;
uniform sampler2D texture1;
uniform vec3 objectColor;
uniform bool useTexture;
uniform vec3 keyDir;       // do swiatla glownego (stale)
uniform vec3 fillDir;      // do swiatla wypelniajacego (od kamery)
uniform vec3 viewPos;      // pozycja kamery
uniform bool shadowMode;   // true = rysujemy plaski cien
uniform float shadowAlpha; // krycie cienia
void main() {
    // Tryb cienia: jednolity, polprzezroczysty ciemny ksztalt.
    if (shadowMode) {
        FragColor = vec4(0.0, 0.0, 0.0, shadowAlpha);
        return;
    }

    vec3 base = useTexture ? texture(texture1, TexCoord).rgb : objectColor;
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    vec3 K = normalize(keyDir);
    vec3 F = normalize(fillDir);

    float kd = max(dot(N, K), 0.0);                 // rozproszenie glowne
    float fd = max(dot(N, F), 0.0);                 // rozproszenie wypelniajace
    vec3 H = normalize(K + V);
    float spec = (kd > 0.0) ? pow(max(dot(N, H), 0.0), 48.0) : 0.0;
    if (useTexture) spec = 0.0;                      // plansza matowa

    // Obrys (rim) - podswietla krawedzie figur na ciemnym tle.
    float rim = useTexture ? 0.0
                           : pow(1.0 - max(dot(N, V), 0.0), 3.0);

    vec3 ambient  = 0.30 * base;
    vec3 diffuse  = (0.60 * kd + 0.35 * fd) * base;
    vec3 specular = 0.35 * spec * vec3(1.0);
    vec3 rimCol   = 0.25 * rim * vec3(1.0);

    FragColor = vec4(ambient + diffuse + specular + rimCol, 1.0);
}
)";

// ======================
// FUNKCJE POMOCNICZE
// ======================

Mesh createBoardMesh();

// Prosty parser plików OBJ. Wczytuje pozycje, UV oraz normalne (vn).
// Jesli plik nie zawiera normalnych, liczy plaskie normalne z trojkatow.
// Uklad wierzcholka w VBO: pos(3) + uv(2) + normal(3) = 8 floatow.
Mesh loadOBJ(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cout << "ERROR: Cannot open model file: " << path << std::endl;
    return {0, 0};
  }

  std::vector<glm::vec3> temp_positions;
  std::vector<glm::vec2> temp_uvs;
  std::vector<glm::vec3> temp_normals;
  std::vector<float> vertexData;
  unsigned int vertexCount = 0;

  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string prefix;
    iss >> prefix;

    if (prefix == "v") {
      glm::vec3 p;
      iss >> p.x >> p.y >> p.z;
      temp_positions.push_back(p);
    } else if (prefix == "vt") {
      glm::vec2 uv;
      iss >> uv.x >> uv.y;
      temp_uvs.push_back(uv);
    } else if (prefix == "vn") {
      glm::vec3 n;
      iss >> n.x >> n.y >> n.z;
      temp_normals.push_back(n);
    } else if (prefix == "f") {
      std::vector<std::string> tokens;
      std::string t;
      while (iss >> t)
        tokens.push_back(t);

      // Triangulacja wielokątów (Triangle Fan).
      for (size_t i = 1; i + 1 < tokens.size(); ++i) {
        size_t idx[3] = {0, i, i + 1};
        glm::vec3 pos[3];
        glm::vec2 uv[3] = {glm::vec2(0.0f), glm::vec2(0.0f), glm::vec2(0.0f)};
        glm::vec3 nrm[3];
        bool hasNormals = true;

        for (int j = 0; j < 3; j++) {
          std::istringstream viss(tokens[idx[j]]);
          std::string s;

          // Pozycja (wymagana).
          std::getline(viss, s, '/');
          int pi = std::stoi(s) - 1;
          pos[j] = temp_positions[pi];

          // UV (opcjonalne, moze byc puste przy zapisie "v//vn").
          if (std::getline(viss, s, '/') && !s.empty()) {
            int ui = std::stoi(s) - 1;
            if (ui >= 0 && ui < (int)temp_uvs.size())
              uv[j] = temp_uvs[ui];
          }

          // Normalna (opcjonalna).
          if (std::getline(viss, s, '/') && !s.empty()) {
            int ni = std::stoi(s) - 1;
            if (ni >= 0 && ni < (int)temp_normals.size())
              nrm[j] = temp_normals[ni];
            else
              hasNormals = false;
          } else {
            hasNormals = false;
          }
        }

        // Brak normalnych w pliku -> plaska normalna z trojkata.
        if (!hasNormals) {
          glm::vec3 fn =
              glm::normalize(glm::cross(pos[1] - pos[0], pos[2] - pos[0]));
          nrm[0] = nrm[1] = nrm[2] = fn;
        }

        for (int j = 0; j < 3; j++) {
          vertexData.push_back(pos[j].x);
          vertexData.push_back(pos[j].y);
          vertexData.push_back(pos[j].z);
          vertexData.push_back(uv[j].x);
          vertexData.push_back(uv[j].y);
          vertexData.push_back(nrm[j].x);
          vertexData.push_back(nrm[j].y);
          vertexData.push_back(nrm[j].z);
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

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(5 * sizeof(float)));
  glEnableVertexAttribArray(2);

  std::cout << "Successfully loaded: " << path << " (" << vertexCount
            << " vertices)\n";
  return {VAO, vertexCount};
}

// Mapuje znak figury z planszy parsera (P,R,N,B,Q,K) na typ siatki 3D.
PieceType charToType(char c) {
  switch (toupper((unsigned char)c)) {
  case 'R':
    return ROOK;
  case 'N':
    return KNIGHT;
  case 'B':
    return BISHOP;
  case 'Q':
    return QUEEN;
  case 'K':
    return KING;
  default:
    return PAWN; // 'P'
  }
}

// Przebudowuje liste figur 3D z aktualnego stanu planszy (uzywane do "skoku"
// na dana pozycje - bez animacji, np. cofanie / reset).
void syncPieces(const ChessGame &game, std::vector<Piece> &pieces) {
  pieces.clear();
  for (int r = 0; r < 8; r++)
    for (int f = 0; f < 8; f++) {
      char c = game.board[r][f];
      if (c == '.')
        continue;
      Piece p;
      p.type = charToType(c);
      p.isWhite = isupper((unsigned char)c);
      p.placeAt(glm::vec2(f, r));
      pieces.push_back(p);
    }
}

// Zwraca indeks figury stojacej na polu (file,rank); pomija figury ginace.
int findPieceAt(std::vector<Piece> &pieces, int file, int rank) {
  for (size_t i = 0; i < pieces.size(); i++) {
    if (pieces[i].dying)
      continue;
    if ((int)pieces[i].boardCoords.x == file &&
        (int)pieces[i].boardCoords.y == rank)
      return (int)i;
  }
  return -1;
}

// Uruchamia animacje dla pojedynczego ruchu opisanego przez MoveDelta.
void startMoveAnimation(const MoveDelta &d, std::vector<Piece> &pieces) {
  if (!d.ok)
    return;

  // 1. Bicie - oznaczamy zbita figure jako ginaca (zanim wejdzie bijacy).
  if (d.isCapture) {
    int vi = findPieceAt(pieces, d.capF, d.capR);
    if (vi >= 0) {
      pieces[vi].dying = true;
      pieces[vi].deathT = 0.0f;
    }
  }

  // 2. Ruch glownej figury.
  int mi = findPieceAt(pieces, d.fromF, d.fromR);
  if (mi >= 0) {
    if (d.isPromotion)
      pieces[mi].promoteTo = d.promoPiece;
    pieces[mi].startMove(glm::vec2(d.toF, d.toR));
  }

  // 3. Roszada - dodatkowo przesuwa sie wieza.
  if (d.isCastle) {
    int ri = findPieceAt(pieces, d.rookFromF, d.rookFromR);
    if (ri >= 0)
      pieces[ri].startMove(glm::vec2(d.rookToF, d.rookToR));
  }
}

// Krok animacji. Zwraca true, jesli cokolwiek nadal sie animuje.
bool updateAnimations(std::vector<Piece> &pieces, float dt) {
  bool busy = false;
  for (auto &p : pieces) {
    if (p.moving) {
      busy = true;
      p.moveT += dt / MOVE_DURATION;
      if (p.moveT >= 1.0f) {
        p.moveT = 1.0f;
        p.moving = false;
        p.worldPos = p.endPos;
        if (p.promoteTo) { // promocja po dotarciu na pole
          p.type = charToType(p.promoteTo);
          p.promoteTo = 0;
        }
      } else {
        float t = p.moveT;
        float s = t * t * (3.0f - 2.0f * t); // smoothstep
        p.worldPos = p.startPos * (1.0f - s) + p.endPos * s;
        p.worldPos.y = sinf(t * 3.14159265f) * HOP_HEIGHT; // luk
      }
    }
    if (p.dying) {
      busy = true;
      p.deathT += dt / DEATH_DURATION;
      if (p.deathT > 1.0f)
        p.deathT = 1.0f;
    }
  }
  // Usuwamy figury, ktore juz zniknely.
  pieces.erase(std::remove_if(pieces.begin(), pieces.end(),
                              [](const Piece &p) {
                                return p.dying && p.deathT >= 1.0f;
                              }),
               pieces.end());
  return busy;
}

// Domyślna partia (Ruy Lopez), używana gdy nie podano / nie znaleziono pliku.
const char *DEFAULT_PGN =
    "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 "
    "6. Re1 b5 7. Bb3 d6 8. c3 O-O 9. h3 Nb8 10. d4 Nbd7";

std::string loadTextFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open())
    return "";
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Macierz rzutujaca geometrie na plaszczyzne y=h wzdluz kierunku swiatla.
// lightToDir = kierunek DO swiatla; promienie biegna w przeciwna strone.
glm::mat4 planarShadowMatrix(glm::vec3 lightToDir, float h) {
  glm::vec3 L = -glm::normalize(lightToDir); // kierunek biegu swiatla (w dol)
  glm::mat4 S(0.0f);
  S[0][0] = 1.0f;
  S[1][0] = -L.x / L.y;
  S[1][2] = -L.z / L.y;
  S[2][2] = 1.0f;
  S[3][0] = (L.x / L.y) * h;
  S[3][1] = h;
  S[3][2] = (L.z / L.y) * h;
  S[3][3] = 1.0f;
  return S;
}

// Macierz modelu (swiata) pojedynczej figury - uwzglednia animacje ruchu i
// bicia. Uzywana zarowno przy rysowaniu figury, jak i jej cienia.
glm::mat4 pieceModelMatrix(const Piece &p, float baseScale) {
  glm::vec3 pos = p.worldPos;
  float scale = baseScale;
  if (p.dying) {
    scale *= (1.0f - p.deathT);
    pos.y -= p.deathT * 0.5f;
  }
  glm::mat4 m(1.0f);
  m = glm::translate(m, pos);
  m = glm::scale(m, glm::vec3(scale));
  if (p.isWhite)
    m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  return m;
}

// ======================
// MAIN
// ======================

int main(int argc, char **argv) {
  // --- Wczytanie PGN: argument -> plik game.pgn -> partia wbudowana ---
  std::string pgnText;
  if (argc > 1)
    pgnText = loadTextFile(argv[1]);
  if (pgnText.empty())
    pgnText = loadTextFile("game.pgn");
  if (pgnText.empty()) {
    std::cout << "Brak pliku PGN - uzywam wbudowanej partii.\n";
    pgnText = DEFAULT_PGN;
  }

  ChessGame game;
  std::vector<std::string> moves = ChessGame::parsePGN(pgnText);
  int currentPly = 0;
  std::cout << "Wczytano " << moves.size() << " ruchow.\n";
  std::cout << "Sterowanie:  -> / SPACJA = nastepny,  <- = poprzedni,"
               "  R = reset,  ENTER = autoodtwarzanie.\n";

  // --- Inicjalizacja OpenGL ---
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_STENCIL_BITS, 8); // bufor szablonu dla cieni

  GLFWwindow *window =
      glfwCreateWindow(1200, 720, "Szachy 3D - PGN (animacja)", nullptr,
                       nullptr);
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

  // --- Modele ---
  Mesh boardMesh = createBoardMesh();
  Mesh pieceMeshes[6];
  pieceMeshes[PAWN] = loadOBJ("models/Pawn.obj");
  pieceMeshes[ROOK] = loadOBJ("models/Rook.obj");
  pieceMeshes[KNIGHT] = loadOBJ("models/Knight.obj");
  pieceMeshes[BISHOP] = loadOBJ("models/Bishop.obj");
  pieceMeshes[QUEEN] = loadOBJ("models/Queen.obj");
  pieceMeshes[KING] = loadOBJ("models/King.obj");

  // --- Tekstura planszy ---
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

  // --- Figury (start) ---
  std::vector<Piece> pieces;
  syncPieces(game, pieces);

  // "Skok" do zadanego polruchu - reset planszy i ciche odtworzenie ruchow,
  // bez animacji. Uzywane przy cofaniu i resecie.
  auto goToPly = [&](int target) {
    target = std::max(0, std::min(target, (int)moves.size()));
    game.setup();
    for (int k = 0; k < target; k++)
      game.applyMove(moves[k]);
    currentPly = target;
    syncPieces(game, pieces);
  };

  // Wykonuje nastepny ruch Z animacja.
  auto stepForward = [&]() {
    if (currentPly >= (int)moves.size())
      return;
    MoveDelta d = game.applyMove(moves[currentPly]);
    std::cout << "Ruch " << (currentPly + 1) << ": " << moves[currentPly]
              << "\n";
    startMoveAnimation(d, pieces);
    currentPly++;
  };

  bool prevRight = false, prevLeft = false, prevSpace = false, prevR = false,
       prevEnter = false;
  bool autoPlay = false;
  float autoTimer = 0.0f;
  const float AUTO_PAUSE = 0.25f; // przerwa miedzy ruchami w autoodtwarzaniu
  float lastFrame = (float)glfwGetTime();

  // ======================
  // PĘTLA GŁÓWNA
  // ======================
  while (!glfwWindowShouldClose(window)) {
    float now = (float)glfwGetTime();
    float dt = now - lastFrame;
    lastFrame = now;

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
      glfwSetWindowShouldClose(window, true);

    // --- aktualizacja animacji ---
    bool busy = updateAnimations(pieces, dt);

    // --- sterowanie ---
    bool right = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
    bool left = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;
    bool space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    bool keyR = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    bool enter = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;

    // Nastepny ruch (tylko gdy nic sie nie animuje, by figury nie "skakaly").
    if (((right && !prevRight) || (space && !prevSpace)) && !busy)
      stepForward();
    if (left && !prevLeft)
      goToPly(currentPly - 1); // cofanie = skok (bez animacji)
    if (keyR && !prevR)
      goToPly(0);
    if (enter && !prevEnter)
      autoPlay = !autoPlay;

    // Autoodtwarzanie: po zakonczeniu animacji odczekaj chwile i graj dalej.
    if (autoPlay && !busy) {
      autoTimer += dt;
      if (autoTimer >= AUTO_PAUSE) {
        autoTimer = 0.0f;
        if (currentPly < (int)moves.size())
          stepForward();
        else
          autoPlay = false; // koniec partii
      }
    } else {
      autoTimer = 0.0f;
    }

    prevRight = right;
    prevLeft = left;
    prevSpace = space;
    prevR = keyR;
    prevEnter = enter;

    // --- rendering ---
    glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glUseProgram(shaderProgram);

    glm::mat4 projection =
        glm::perspective(glm::radians(45.0f), 1200.0f / 720.0f, 0.1f, 100.0f);
    glm::vec3 camPos(sin(now * 0.2f) * 12.0f, 8.0f, cos(now * 0.2f) * 12.0f);
    glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                       GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                       glm::value_ptr(view));

    // Jedno dominujace swiatlo "zza ramienia kamery": gora + w strone widza +
    // lekko z boku. To samo swiatlo rzuca cien, wiec cien zawsze pada ZA
    // figurami (z dala od widza) - spojnie z oswietleniem.
    glm::vec3 camDir = glm::normalize(glm::vec3(camPos.x, 0.0f, camPos.z));
    glm::vec3 sideDir = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f),
                                                  camDir));
    glm::vec3 keyDir =
        glm::normalize(camDir * 0.55f + glm::vec3(0.0f, 1.0f, 0.0f) +
                       sideDir * 0.35f);
    // Delikatne, stale swiatlo wypelniajace z gory - tylko zmiekcza cienie.
    glm::vec3 fillDir = glm::vec3(0.0f, 1.0f, 0.0f);
    glUniform3fv(glGetUniformLocation(shaderProgram, "keyDir"), 1,
                 glm::value_ptr(keyDir));
    glUniform3fv(glGetUniformLocation(shaderProgram, "fillDir"), 1,
                 glm::value_ptr(fillDir));
    glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1,
                 glm::value_ptr(camPos));
    glUniform1f(glGetUniformLocation(shaderProgram, "shadowAlpha"), 0.45f);

    const float globalObjScale = 1.2f;
    GLint locModel = glGetUniformLocation(shaderProgram, "model");
    GLint locUseTex = glGetUniformLocation(shaderProgram, "useTexture");
    GLint locShadow = glGetUniformLocation(shaderProgram, "shadowMode");
    GLint locColor = glGetUniformLocation(shaderProgram, "objectColor");

    // ----- 1. Plansza (oznacza swoj obszar w stencilu: stencil = 1) -----
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    glm::mat4 model = glm::mat4(1.0f);
    glUniform1i(locShadow, 0);
    glUniform1i(locUseTex, 1);
    glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
    glBindTexture(GL_TEXTURE_2D, boardTexture);
    glBindVertexArray(boardMesh.VAO);
    glDrawElements(GL_TRIANGLES, boardMesh.vertexCount, GL_UNSIGNED_INT, 0);

    // ----- 2. Cienie rzutowane (tylko na planszy, kazdy piksel raz) -----
    glm::mat4 shadowMat = planarShadowMatrix(keyDir, 0.01f);
    glStencilFunc(GL_EQUAL, 1, 0xFF);       // rysuj tylko na polach planszy...
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR); // ...i tam tylko raz (bez nakladania)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glUniform1i(locShadow, 1);
    for (auto &p : pieces) {
      glm::mat4 sModel = shadowMat * pieceModelMatrix(p, globalObjScale);
      glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(sModel));
      Mesh m = pieceMeshes[p.type];
      if (m.vertexCount > 0) {
        glBindVertexArray(m.VAO);
        glDrawArrays(GL_TRIANGLES, 0, m.vertexCount);
      }
    }
    glUniform1i(locShadow, 0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);

    // ----- 3. Figury -----
    glUniform1i(locUseTex, 0);
    for (auto &p : pieces) {
      model = pieceModelMatrix(p, globalObjScale);
      glm::vec3 color = p.isWhite ? glm::vec3(0.90f, 0.90f, 0.85f)
                                  : glm::vec3(0.22f, 0.22f, 0.27f);
      glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
      glUniform3fv(locColor, 1, glm::value_ptr(color));

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
  // pos(3) + uv(2) + normal(3). Plansza lezy w plaszczyznie y=0,
  // normalna skierowana w gore (0,1,0).
  float vertices[] = {
      -4.0f, 0.0f, -4.0f, 0.0f, 4.0f, 0.0f, 1.0f, 0.0f, //
      4.0f,  0.0f, -4.0f, 4.0f, 4.0f, 0.0f, 1.0f, 0.0f, //
      4.0f,  0.0f, 4.0f,  4.0f, 0.0f, 0.0f, 1.0f, 0.0f, //
      -4.0f, 0.0f, 4.0f,  0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
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
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(5 * sizeof(float)));
  glEnableVertexAttribArray(2);
  return {VAO, 6};
}
