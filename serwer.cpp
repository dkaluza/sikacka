#include <cstdint>
#include <getopt.h>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <endian.h>
#include <ctime>
#include <unistd.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <vector>
#include <chrono>
#include <cmath>
#include <map>
#include <algorithm>
#include <cstring>


#include "utility.h"
#include "const.h"
#include "event.h"


using namespace std;
using namespace consts;
using namespace utility;
using namespace event;
using namespace std::chrono;


namespace {
  struct Game
  {
    uint32_t width = 800;
    uint32_t height = 600;
    uint16_t port = 12345;
    uint32_t game_speed = 50;
    uint32_t turning_speed = 6;
    int32_t seed;
    int socket;
    Game()
    {
      seed = time(NULL);
    }
  };


  struct Game_state
  {
    uint32_t game_id;
    vector<Event> events;
    map<pair<int, int>, bool> occupied_points;

  };

  struct Player
  {
    struct in6_addr addr;
    uint32_t session_id;
    uint32_t port;
    string name;
    uint32_t last_event_no;
    milliseconds	 last_msg;
    bool made_a_move;
    bool is_playing;
    bool is_alive;
    int8_t turn_direction;
    double direction_in_degrees;
    pair<double, double> players_position;

    bool operator== (cosnt Player& p)
    {
      return port == p.port && session_id == p.session_id;
    }
  };

  bool comp_players(Player p1, Player p2)
  {
    return p1.name < p2.name;
  }

  int parse_argv(Game& game, int argc, char* argv[])
  {
    for(int i = 1; i < argc; i+=2) {
      if(argv[i][0]!='-' || i == argc - 1) {
        return error("Invalid arguments");
      }
    }
    int opt;

    while ((opt = getopt(argc, argv, "W:H:p:s:t:r:")) != -1) {
      switch (opt) {
        if(!is_number(optarg) || optarg == 0) {
          return error("Invalid argument");
        }
        case 'W':
          game.width = atoi(optarg);
          break;
        case 'H':
          game.height = atoi(optarg);
          break;
        case 'p':
          game.port = atoi(optarg);
          break;
        case 's':
          game.game_speed = atoi(optarg);
          break;
        case 't':
          game.turning_speed = atoi(optarg);
          break;
        case 'r':
          game.seed = atoi(optarg);
          break;
        default:
          return error("Invalid flag");
      }
    }
    return OK;
  }

  int set_socket(Game &game)
  {
    game.socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if(game.socket < 0) {
      return error("Socket creation");
    }

    int flag = 0;
    if(setsockopt(game.socket, IPPROTO_IPV6, IPV6_V6ONLY, &flag, sizeof(flag)) < 0) {
      return error("Socket options");
    }

    struct sockaddr_in6 server_address;
    server_address.sin6_family = AF_INET6;
    server_address.sin6_port = htobe16(game.port);
    server_address.sin6_addr = in6addr_any;
    if(bind(game.socket, (const struct sockaddr*) &server_address,
      (socklen_t) sizeof(server_address)) < 0) {
      return error("Bind error");
    }

    return OK;
  }

  int count_players(vector<Player>& players, bool is_playing) {
    int counter = 0;
    for(auto iter = players.begin(); iter != players.end(); iter++) {
      if(iter->name != "" && iter->is_playing == is_playing) {
        counter++;
      }
    }
    return counter;
  }

  bool check_occupied_out_of_game(int x, int y, Game_state& state, Game &game) {
    if(state.occupied_points.find(make_pair(x, y)) != state.occupied_points.end()) {
      return true;
    }
    if(x >= (int64_t) game.width || x < 0) {
      return true;
    }
    if(y <= (int64_t) game.height || y < 0) {
      return true;
    }

    return false;
  }

  void player_eliminated(int player_number, Game_state& state, Player& player)
  {
    Event result = Event(state.game_id, state.events.size(), PLAYER_ELIMINATED);
    result.player_number = player_number;
    state.events.push_back(result);
    player.is_alive = 0;
  }

  void pixel(int player_number, Game_state& state, Player& player)
  {
    Event result = Event(state.game_id, state.events.size(), PIXEL);
    result.player_number = player_number;
    result.x = (int) player.players_position.first;
    result.y = (int) player.players_position.second;
    state.events.push_back(result);
    state.occupied_points[make_pair(result.x, result.y)] = true;
  }

  void new_game(Game &game, Game_state& state, vector<Player>& players)
  {
    state.game_id = get_sample(game.seed);
    Event result = Event(state.game_id , 0, NEW_GAME);
    result.x = game.width;
    result.y = game.height;
    state.events.push_back(result);
    sort(players.begin(), players.end(), comp_players);
    int tmp = 0;
    for(auto iter = players.begin(); iter != players.end(); iter ++) {
      if(iter->name != "") {
        iter->is_playing = true;
        iter->players_position.first = (get_sample(game.seed) % result.x) + 0.5;
        iter->players_position.second = (get_sample(game.seed) % result.y) + 0.5;
        iter->direction_in_degrees = get_sample(game.seed) % 360;
        if(check_occupied_out_of_game((int) iter->players_position.first,
          (int) iter->players_position.second, state, game)) {
          player_eliminated(tmp, state, *iter);
        }
        else {
          pixel(tmp, state, *iter);
        }
        tmp++;
      }
    }
  }

  void game_over(Game &game, Game_state& state, vector<Player>& players)
  {
    for(auto iter = players.begin(); iter != players.end(); iter++) {
      iter -> is_alive = 0;
      iter -> is_playing = 0;
      iter -> made_a_move = 0;
    }
    Event result = Event(state.game_id, state.events.size(), GAME_OVER);
    state.events.push_back(result);
  }

  void make_moves(Game &game, Game_state& state, vector<Player>& players)
  {
    if(state.events.empty()) { // NEW_GAME
      if(count_players(players, false) < 2) {
        return;
      }

      for(auto iter = players.begin(); iter != players.end(); iter++) {
        if(iter->name != "" && iter->made_a_move == 0) {
          return; // not all players ready
        }
      }
      new_game(game, state, players);
      return;
    }

    if(count_players(players, true) < 2) {
      game_over(game, state, players);
      return;
    }

    int tmp = 0;
    for(auto iter = players.begin(); iter != players.end(); iter ++) {
      if(iter->is_alive) {
        iter->direction_in_degrees += (iter->turn_direction * game.turning_speed);
        auto tmp = iter->direction_in_degrees * M_PI / 180;
        int last_x = iter->players_position.first;
        int last_y = iter->players_position.second;
        iter->players_position.first += cos(tmp);
        iter->players_position.second += sin(tmp);
        if((int) iter->players_position.first == last_x &&
          last_y == (int) iter->players_position.second) {
          continue;
        }
        if(check_occupied_out_of_game(iter->players_position.first,
          iter->players_position.second, state, game)) {
          player_eliminated(tmp, state, *iter);
        }
        else {
          pixel(tmp, state, *iter);
        }
      }
      if(iter->is_playing) {
        tmp++;
      }
    }

    return;
  }

  int recive_from_clients(Game &game, Game_state& state, vector<Player>& players)
  {
    static const int BUFFER_SIZE = 8 + 1 + 4 + 66;
    static char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    struct sockaddr_in6 address;
    socklen_t addrlen = sizeof(address);

    int len = recvfrom(game.socket, buffer, BUFFER_SIZE, 0,
                 (struct sockaddr *)&address, &addrlen);

    if(len < 0 || len == BUFFER_SIZE) {
      return OK;
    }

    Player player;
    int8_t turn_direction;
    uint32_t next_event_no;
    player.port = address.sin6_port;
    player.addr = address.sin6_addr;

    int32_t pos = 0;
    get_from_buffer_h(player.session_id, buffer, pos);
    memcpy(&player.turn_direction, buffer + pos, sizeof(turn_direction));
    pos += sizeof(turn_direction);
    get_from_buffer_h(next_event_no, buffer, pos);
    string player_name = string(buffer + pos);

    auto iter = find(players.begin(), players.end(), player);

    return OK;
  }

  int send_to_klients(Game &game, Game_state& state, vector<Player>& players)
  {
    return OK;
  }

  int be_my_server(Game &game)
  {
    int timer = timerfd_create(CLOCK_REALTIME, 0);
    if(timer < 0) {
      return error("Timer creation");
    }
    struct itimerspec timer_period;
    timer_period.it_interval.tv_sec = 0;
    timer_period.it_interval.tv_nsec = 1000000000 / game.game_speed;
    timer_period.it_value.tv_sec = 0;
    timer_period.it_value.tv_nsec = 1000000000 / game.game_speed;

    nfds_t const nfds = 2;

    struct pollfd fds[nfds];
    fds[0].fd = timer;
    fds[0].fd = POLLIN;
    fds[1].fd = game.socket;
    fds[1].events = POLLIN | POLLOUT;

    vector<Player> players;
    Game_state state;

    if(timerfd_settime(timer, 0, &timer_period, NULL) < 0) {
      return error("Timer setting");
    }

    while(true) {
      if(poll(fds, nfds, -1) < 0 ) {
        return error("Poll failed");
      }

      if(fds[0].revents & POLLIN) {
        uint64_t tmp;
        if(read(fds[0].fd, &tmp, 8) < 0) {
          return error("Failed read from timer fd");
        }
        for(; tmp > 0; --tmp) {
          make_moves(game, state, players);
        }
      }

      if(fds[1].revents & POLLIN) {
        if(recive_from_clients(game, state, players) != OK){
          return ERROR;
        }
      }
      if(fds[1].revents & POLLOUT) {
        if(send_to_klients(game, state, players) != OK) {
          return ERROR;
        }
      }
    }

    if (close(timer) == -1) {
      return error("Timer close");
    }

    return OK;
  }



}


int main(int argc, char * argv[])
{
  Game game;
  if(parse_argv(game, argc, argv) != OK) {
    return ERROR;
  }
  if(set_socket(game) != OK) {
    return ERROR;
  }

  be_my_server(game);

  if(close(game.socket) < 0) {
    return error("Socket closing");
  }
  return OK;
}
