// Implementacja logiki szachowej i parsera PGN.
// applyMove() zwraca teraz MoveDelta (opis ruchu dla warstwy graficznej).

#include "pgn.h"
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>

using namespace std;

// --- czyste funkcje pomocnicze (nie zalezne od stanu planszy) ---
namespace {
bool inb(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }
bool sameColor(char c, bool white) {
  return c != '.' &&
         (white ? isupper((unsigned char)c) : islower((unsigned char)c));
}
bool enemyColor(char c, bool white) {
  return c != '.' &&
         (white ? islower((unsigned char)c) : isupper((unsigned char)c));
}
} // namespace

void ChessGame::setup() {
  const char *back = "RNBQKBNR";
  for (int f = 0; f < 8; f++) {
    board[0][f] = back[f]; // biale figury (rzad 1)
    board[1][f] = 'P';     // biale piony  (rzad 2)
    for (int r = 2; r <= 5; r++)
      board[r][f] = '.';
    board[6][f] = 'p';                    // czarne piony (rzad 7)
    board[7][f] = (char)tolower(back[f]); // czarne figury (rzad 8)
  }
  whiteToMove = true;
  epRank = epFile = -1;
}

void ChessGame::printBoard() const {
  for (int r = 7; r >= 0; r--) {
    cout << (r + 1) << "  ";
    for (int f = 0; f < 8; f++)
      cout << board[r][f] << ' ';
    cout << '\n';
  }
  cout << "   a b c d e f g h\n\n";
}

bool ChessGame::attackedBy(int r, int f, bool byWhite) const {
  if (byWhite) {
    if (inb(r - 1, f - 1) && board[r - 1][f - 1] == 'P')
      return true;
    if (inb(r - 1, f + 1) && board[r - 1][f + 1] == 'P')
      return true;
  } else {
    if (inb(r + 1, f - 1) && board[r + 1][f - 1] == 'p')
      return true;
    if (inb(r + 1, f + 1) && board[r + 1][f + 1] == 'p')
      return true;
  }
  int kn[8][2] = {{1, 2},  {2, 1},  {-1, 2},  {-2, 1},
                  {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
  char N = byWhite ? 'N' : 'n', K = byWhite ? 'K' : 'k';
  char B = byWhite ? 'B' : 'b', R = byWhite ? 'R' : 'r',
       Q = byWhite ? 'Q' : 'q';
  for (auto &d : kn)
    if (inb(r + d[0], f + d[1]) && board[r + d[0]][f + d[1]] == N)
      return true;
  for (int dr = -1; dr <= 1; dr++)
    for (int df = -1; df <= 1; df++)
      if ((dr || df) && inb(r + dr, f + df) && board[r + dr][f + df] == K)
        return true;
  int diag[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  for (auto &d : diag) {
    int rr = r + d[0], ff = f + d[1];
    while (inb(rr, ff)) {
      char c = board[rr][ff];
      if (c != '.') {
        if (c == B || c == Q)
          return true;
        break;
      }
      rr += d[0];
      ff += d[1];
    }
  }
  int orth[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (auto &d : orth) {
    int rr = r + d[0], ff = f + d[1];
    while (inb(rr, ff)) {
      char c = board[rr][ff];
      if (c != '.') {
        if (c == R || c == Q)
          return true;
        break;
      }
      rr += d[0];
      ff += d[1];
    }
  }
  return false;
}

bool ChessGame::kingInCheck(bool white) const {
  char K = white ? 'K' : 'k';
  for (int r = 0; r < 8; r++)
    for (int f = 0; f < 8; f++)
      if (board[r][f] == K)
        return attackedBy(r, f, !white);
  return false;
}

bool ChessGame::pieceCanMove(char piece, int sr, int sf, int dr,
                             int df) const {
  char p = (char)toupper((unsigned char)piece);
  int rd = dr - sr, fd = df - sf, ard = abs(rd), afd = abs(fd);
  if (p == 'N')
    return (ard == 1 && afd == 2) || (ard == 2 && afd == 1);
  if (p == 'K')
    return ard <= 1 && afd <= 1;
  bool diag = (ard == afd), orth = (rd == 0 || fd == 0);
  if (p == 'B' && !diag)
    return false;
  if (p == 'R' && !orth)
    return false;
  if (p == 'Q' && !(diag || orth))
    return false;
  int stepR = (rd > 0) - (rd < 0), stepF = (fd > 0) - (fd < 0);
  int r = sr + stepR, f = sf + stepF;
  while (r != dr || f != df) {
    if (board[r][f] != '.')
      return false;
    r += stepR;
    f += stepF;
  }
  return true;
}

void ChessGame::performMove(int sr, int sf, int dr, int df, char piece,
                            bool white, char promo) {
  char moved = board[sr][sf];
  if (piece == 'P' && sf != df && board[dr][df] == '.') // bicie w przelocie
    board[sr][df] = '.';
  board[sr][sf] = '.';
  if (piece == 'P' && promo)
    board[dr][df] = white ? (char)toupper((unsigned char)promo)
                          : (char)tolower((unsigned char)promo);
  else
    board[dr][df] = moved;
  epRank = epFile = -1;
  if (piece == 'P' && abs(dr - sr) == 2) {
    epRank = (sr + dr) / 2;
    epFile = sf;
  }
}

MoveDelta ChessGame::applyMove(const string &sanRaw) {
  string s;
  for (char c : sanRaw)
    if (c != '+' && c != '#' && c != '!' && c != '?')
      s += c;
  if (s.empty())
    return {};
  bool white = whiteToMove;

  // Roszada.
  if (s == "O-O" || s == "0-0" || s == "O-O-O" || s == "0-0-0") {
    int r = white ? 0 : 7;
    bool kingside = (s == "O-O" || s == "0-0");

    MoveDelta d;
    d.ok = true;
    d.white = white;
    d.isCastle = true;
    d.fromR = r;
    d.fromF = 4;
    d.toR = r;
    d.toF = kingside ? 6 : 2;
    d.rookFromR = r;
    d.rookFromF = kingside ? 7 : 0;
    d.rookToR = r;
    d.rookToF = kingside ? 5 : 3;

    board[r][4] = '.';
    if (kingside) {
      board[r][6] = white ? 'K' : 'k';
      board[r][5] = white ? 'R' : 'r';
      board[r][7] = '.';
    } else {
      board[r][2] = white ? 'K' : 'k';
      board[r][3] = white ? 'R' : 'r';
      board[r][0] = '.';
    }
    epRank = epFile = -1;
    whiteToMove = !whiteToMove;
    return d;
  }

  // Promocja: "=Q" lub skrotowo koncowe "Q".
  char promo = 0;
  size_t eq = s.find('=');
  if (eq != string::npos) {
    promo = (char)toupper((unsigned char)s[eq + 1]);
    s = s.substr(0, eq);
  } else if (s.size() >= 3 && strchr("QRBNqrbn", s.back()) &&
             isdigit((unsigned char)s[s.size() - 2])) {
    promo = (char)toupper((unsigned char)s.back());
    s.pop_back();
  }

  // Typ figury (albo pion).
  char piece;
  size_t i = 0;
  if (strchr("KQRBN", s[0])) {
    piece = s[0];
    i = 1;
  } else
    piece = 'P';

  // Usuwamy 'x', reszte zostawiamy jako "cialo" ruchu.
  bool capture = false;
  string body;
  for (size_t k = i; k < s.size(); k++) {
    if (s[k] == 'x')
      capture = true;
    else
      body += s[k];
  }
  if (body.size() < 2)
    return {};

  int df = body[body.size() - 2] - 'a';
  int dr = body[body.size() - 1] - '1';
  int wantFile = -1, wantRank = -1; // ewentualna disambiguacja
  for (size_t k = 0; k + 2 < body.size(); k++) {
    char c = body[k];
    if (c >= 'a' && c <= 'h')
      wantFile = c - 'a';
    else if (c >= '1' && c <= '8')
      wantRank = c - '1';
  }
  char want = white ? (char)toupper((unsigned char)piece)
                    : (char)tolower((unsigned char)piece);

  // Szukamy figury, ktora moze tu zagrac i nie zostawia wlasnego krola w
  // szachu.
  int fr = -1, ff = -1;
  for (int r = 0; r < 8 && fr < 0; r++)
    for (int f = 0; f < 8; f++) {
      if (board[r][f] != want)
        continue;
      if (wantFile >= 0 && f != wantFile)
        continue;
      if (wantRank >= 0 && r != wantRank)
        continue;

      bool geo = false;
      if (piece == 'P') {
        int dir = white ? 1 : -1, start = white ? 1 : 6;
        if (!capture) {
          if (f == df && board[dr][df] == '.') {
            if (r + dir == dr)
              geo = true;
            else if (r == start && r + 2 * dir == dr &&
                     board[r + dir][f] == '.')
              geo = true;
          }
        } else if (abs(df - f) == 1 && r + dir == dr) {
          if (enemyColor(board[dr][df], white))
            geo = true; // zwykle bicie
          else if (dr == epRank && df == epFile)
            geo = true; // bicie w przelocie
        }
      } else {
        if (!sameColor(board[dr][df], white))
          geo = pieceCanMove(want, r, f, dr, df);
      }
      if (!geo)
        continue;

      char backup[8][8];
      memcpy(backup, board, sizeof board);
      int bER = epRank, bEF = epFile;
      performMove(r, f, dr, df, piece, white, promo);
      bool bad = kingInCheck(white);
      memcpy(board, backup, sizeof board);
      epRank = bER;
      epFile = bEF;
      if (bad)
        continue;

      fr = r;
      ff = f;
    }

  if (fr < 0)
    return {};

  // Budujemy opis ruchu PRZED zmiana planszy.
  MoveDelta d;
  d.ok = true;
  d.white = white;
  d.fromR = fr;
  d.fromF = ff;
  d.toR = dr;
  d.toF = df;
  if (board[dr][df] != '.') { // zwykle bicie
    d.isCapture = true;
    d.capR = dr;
    d.capF = df;
  } else if (piece == 'P' && ff != df) { // bicie w przelocie
    d.isCapture = true;
    d.capR = fr;
    d.capF = df;
  }
  if (piece == 'P' && promo) {
    d.isPromotion = true;
    d.promoPiece = promo; // juz wielka litera
  }

  performMove(fr, ff, dr, df, piece, white, promo);
  whiteToMove = !whiteToMove;
  return d;
}

vector<string> ChessGame::parsePGN(const string &text) {
  string clean;
  int paren = 0;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if ((unsigned char)c >= 0x80)
      continue;
    if (c == '{') {
      while (i < text.size() && text[i] != '}')
        i++;
      continue;
    }
    if (c == ';') {
      while (i < text.size() && text[i] != '\n')
        i++;
      continue;
    }
    if (c == '[') {
      while (i < text.size() && text[i] != ']')
        i++;
      continue;
    }
    if (c == '(') {
      paren++;
      continue;
    }
    if (c == ')') {
      if (paren > 0)
        paren--;
      continue;
    }
    if (paren == 0)
      clean += c;
  }

  vector<string> moves;
  stringstream toks(clean);
  string tok;
  while (toks >> tok) {
    if (tok == "1-0" || tok == "0-1" || tok == "1/2-1/2" || tok == "*")
      continue;
    size_t p = 0;
    while (p < tok.size() && isdigit((unsigned char)tok[p]))
      p++;
    if (p > 0 && p < tok.size() && tok[p] == '.') {
      while (p < tok.size() && tok[p] == '.')
        p++;
      tok = tok.substr(p);
    }
    if (tok.empty() || tok[0] == '$')
      continue;
    moves.push_back(tok);
  }
  return moves;
}
