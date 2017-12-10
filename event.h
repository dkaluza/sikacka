#include <string>
#include <vector>

using namespace std;

#ifndef EVENT_H
#define EVENT_H

namespace event {
  struct Event
  {
    uint32_t game_id;
    uint32_t event_no;
    uint8_t event_type;
    static vector<string> players;
    uint8_t player_number; //storing eliminated and moving player
    uint32_t x;
    uint32_t y;

    Event() {}

    Event(uint32_t game_id, uint32_t event_no, uint8_t event_type) :
      game_id{game_id}, event_no{event_no}, event_type{event_type} {}

    Event& operator=(const Event& e)
    {
      game_id = e.game_id;
      event_no = e.event_no;
      event_type = e.event_type;
      player_number = e.player_number;
      x = e.x;
      y = e.y;
      return *this;
    }
  };
  vector<string> Event::players = vector<string>();
}

#endif
