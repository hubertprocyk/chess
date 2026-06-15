#pragma once
#include <string>
#include <vector>

// ============================================================================
//  Logika szachowa + parser PGN  (moduł niezależny od grafiki)
//  Plansza: board[rzad][kolumna], rzad 0..7 = 1..8, kolumna 0..7 = a..h.
//  Wielkie litery = biale figury, male = czarne, '.' = puste pole.
// ============================================================================

// Opis pojedynczego ruchu - "roznica" miedzy pozycjami. Tego potrzebuje
// warstwa graficzna, zeby animowac ruch (skad-dokad), bicie i promocje.
struct MoveDelta {
  bool ok = false; // czy ruch w ogole rozpoznano

  int fromR = -1, fromF = -1; // pole startowe figury
  int toR = -1, toF = -1;     // pole docelowe

  bool isCapture = false;     // czy doszlo do bicia
  int capR = -1, capF = -1;   // pole zbitej figury (przy biciu w przelocie != to)

  bool isCastle = false;      // roszada (rusza sie tez wieza)
  int rookFromR = -1, rookFromF = -1;
  int rookToR = -1, rookToF = -1;

  bool isPromotion = false;   // promocja piona
  char promoPiece = 0;        // 'Q','R','B','N' (wielka litera)

  bool white = true;          // kto wykonal ruch
};

class ChessGame {
public:
  char board[8][8];
  bool whiteToMove = true;
  int epRank = -1, epFile = -1; // pole bicia w przelocie, -1 = brak

  ChessGame() { setup(); }

  // Ustawia pozycje startowa.
  void setup();

  // Wypisuje plansze w konsoli (przydatne do debugowania).
  void printBoard() const;

  // Wykonuje pojedynczy ruch w notacji SAN i zwraca jego opis (MoveDelta).
  // Jesli ruchu nie rozpoznano, zwrocony delta ma pole ok == false.
  MoveDelta applyMove(const std::string &san);

  // Czysci surowy tekst PGN (naglowki, komentarze, warianty, numery ruchow)
  // i zwraca uporzadkowana liste ruchow w notacji SAN.
  static std::vector<std::string> parsePGN(const std::string &text);

private:
  bool attackedBy(int r, int f, bool byWhite) const;
  bool kingInCheck(bool white) const;
  bool pieceCanMove(char piece, int sr, int sf, int dr, int df) const;
  void performMove(int sr, int sf, int dr, int df, char piece, bool white,
                   char promo);
};
