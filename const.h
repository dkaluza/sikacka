#include <cstdint>

#ifndef CONST_H
#define CONST_H

namespace consts {
  // return states
  int const OK = 0;
  int const ERROR = 1;

  // event types
  uint8_t const NEW_GAME = 0;
  uint8_t const PIXEL = 1;
  uint8_t const PLAYER_ELIMINATED = 2;
  uint8_t const GAME_OVER = 3;
}

#endif
