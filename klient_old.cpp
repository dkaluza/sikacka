#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <algorithm>
#include <iostream>
#include <sys/time.h>
#include <cstdint>
#include <unistd.h>
#include <cstring>
#include <endian.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <map>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <vector>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

using namespace std;

namespace {
  int const OK = 0;
  int const ERROR = 1;

  int error(string s)
  {
    cerr << "Error: " << s << endl;
    return ERROR;
  }

  // splits ip:port from str
  void split_port_ip(string &str, string &port)
  {
    int counter = count(str.begin(), str.end(), ':');
    if( counter == 1 ) {
      int sep = str.find(':');
      port = str.substr(sep + 1,  string::npos);
      str = str.substr(0, sep);
    }

  }

  int parse_argv(int argc, char *argv[], string& player_name,
    struct addrinfo*& game_host, struct addrinfo*& ui_host)
  {
    for(char* c = argv[1]; *c != '\0'; c++){
      if( *c < 33 || *c > 126  || c - argv[1] > 64) {
        return error("Invalid player name");
      }
    }
    player_name = string(argv[1]);

    string game_host_ip = argv[2];
    string game_host_port = "12345";
    split_port_ip(game_host_ip, game_host_port);

    struct addrinfo game_host_hints;
    memset(&game_host_hints, 0, sizeof(struct addrinfo));
    game_host_hints.ai_family = AF_UNSPEC;
    game_host_hints.ai_socktype = SOCK_DGRAM;
    game_host_hints.ai_protocol = IPPROTO_UDP;
    game_host_hints.ai_flags = 0;
    game_host_hints.ai_addrlen = 0;
    game_host_hints.ai_addr = NULL;
    game_host_hints.ai_canonname = NULL;
    game_host_hints.ai_next = NULL;
    if(getaddrinfo(game_host_ip.c_str(), game_host_port.c_str(),
        &game_host_hints, &game_host) != 0) {
      return error("Invalid game host server address");
    }

    string ui_host_ip = "localhost";
    string ui_host_port = "12346";
    if(argc == 4) {
      ui_host_ip = argv[3];
      split_port_ip(ui_host_ip, ui_host_port);
    }
    struct addrinfo ui_host_hints;
    memset(&ui_host_hints, 0, sizeof(struct addrinfo));
    ui_host_hints.ai_family = AF_UNSPEC;
    ui_host_hints.ai_socktype = SOCK_STREAM;
    ui_host_hints.ai_protocol = IPPROTO_TCP;
    ui_host_hints.ai_flags = 0;
    ui_host_hints.ai_addrlen = 0;
    ui_host_hints.ai_addr = NULL;
    ui_host_hints.ai_canonname = NULL;
    ui_host_hints.ai_next = NULL;
    if(getaddrinfo(ui_host_ip.c_str(), ui_host_port.c_str(),
        &ui_host_hints, &ui_host) != 0) {
      return error("Invalid ui host server address");
    }

    return OK;
  }

  int send_game_server(int socket, uint64_t session_id, int8_t turn_direction,
    uint32_t next_event_no, string& player_name)
  {
    static const int BUFFER_SIZE = 8 + 1 + 4 + 66;
    static char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    uint64_t be_session = htobe64(session_id);
    uint32_t be_next_event = htobe32(next_event_no);
    int32_t pos = 0;
    const char* player_c_str = player_name.c_str();
    memcpy(buffer, &be_session, sizeof(be_session));
    pos += sizeof(be_session);
    memcpy(buffer + pos, &turn_direction, sizeof(turn_direction));
    pos += sizeof(turn_direction);
    memcpy(buffer + pos, &be_next_event, sizeof(be_next_event));
    pos += sizeof(be_next_event);
    memcpy(buffer + pos, &player_c_str, player_name.size());
    pos += player_name.size();

    int32_t len = send(socket, buffer, (size_t) pos, 0);
    if(len < 0 || len != pos) {
      return error("Partial/failed send to game server");
    }
    return OK;
  }

  int recive_ui_server(int socket, int8_t& turn_direction)
  {
    static const int BUFFER_SIZE = 20;
    static char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int32_t len = recv(socket, buffer, (size_t) BUFFER_SIZE, 0);
    if(len < 0) {
      return error("Failed recive from ui server");
    }

    string msg = string(buffer);

    if(msg == "RIGHT_KEY_DOWN" || msg == "LEFT_KEY_DOWN") {
      turn_direction = 0;
    }
    else if(msg == "LEFT_KEY_UP") {
      turn_direction = -1;
    }
    else if(msg == "RIGHT_KEY_UP") {
      turn_direction = 1;
    }
    else {
      return error("Ui bad message");
    }

    return OK;
  }

  uint8_t const NEW_GAME = 0;
  uint8_t const PIXEL = 1;
  uint8_t const PLAYER_ELIMINATED = 2;
  uint8_t const GAME_OVER = 3;
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
      player_number = e.player_number;
      x = e.x;
      y = e.y;
      return *this;
    }
  };
  vector<string> Event::players = vector<string>();
  using events_map = map<pair<uint32_t, uint32_t>,Event>;

  void get_from_buffer_h(uint32_t &number, char* buffer, int& pos)
  {
    memcpy(&(number), buffer, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    number = be32toh(number);
  }

  Event new_game_event(uint32_t game_id, uint32_t event_no, uint32_t len, char* buffer)
  {
    Event::players.clear();
    Event result = Event(game_id, event_no, NEW_GAME);
    int pos = 0;
    get_from_buffer_h(result.x, buffer, pos);
    get_from_buffer_h(result.y, buffer, pos);
    len -= pos;
    while(len > 0) {
      string tmp = string(buffer + pos);
      pos += tmp.size() + 1;
      len -= pos;
      Event::players.push_back(tmp);
    }
    return result;
  }

  Event pixel_event(uint32_t game_id, uint32_t event_no, uint32_t len, char* buffer)
  {
    Event result = Event(game_id, event_no, PIXEL);
    int pos = 0;
    memcpy(&(result.player_number), buffer, sizeof(uint8_t));
    pos += sizeof(uint8_t);
    get_from_buffer_h(result.x, buffer, pos);
    get_from_buffer_h(result.y, buffer, pos);
    return result;
  }

  Event player_eliminated_event(uint32_t game_id, uint32_t event_no, uint32_t len, char* buffer)
  {
    Event result = Event(game_id, event_no, PLAYER_ELIMINATED);
    memcpy(&(result.player_number), buffer, sizeof(uint8_t));
    return result;
  }

  Event game_over_event(uint32_t game_id, uint32_t event_no, uint32_t len, char* buffer)
  {
    return Event(game_id, event_no, GAME_OVER);
  }

  int recive_game_server(int socket, events_map &events, uint32_t &next_event_no,
    uint32_t& current_game_id)
  {
    static const int BUFFER_SIZE = 512;
    static char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int32_t data_len = recv(socket, buffer, (size_t) BUFFER_SIZE, 0);
    if(data_len < 0) {
      return error("Failed recive from ui server");
    }
    uint32_t game_id;
    int pos = 0;
    memcpy(&game_id, buffer + pos, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    uint32_t len;
    uint32_t event_no;
    uint8_t event_type;
    char* event_data;
    uint32_t crc32;
    int event_beg;
    while(data_len < pos) {
      event_beg = pos;
      get_from_buffer_h(len, buffer, pos);
      get_from_buffer_h(event_no, buffer, pos);
      memcpy(&event_type, buffer + pos, sizeof(uint8_t));
      pos += sizeof(uint8_t);
      event_data = buffer + pos;
      pos = event_beg + len - 4;
      get_from_buffer_h(crc32, buffer, pos);
      // TODO crc32 check
      pair<uint32_t, uint32_t> key = make_pair(game_id, event_no);
      switch(event_type) {
        case NEW_GAME:
          events[key] = new_game_event(game_id, event_no, len, event_data);
          if(game_id != current_game_id) {
            current_game_id = game_id;
            next_event_no = event_no + 1;
          }
          break;
        case PIXEL:
          events[key] = pixel_event(game_id, event_no, len, event_data);
          break;
        case PLAYER_ELIMINATED:
          events[key] = player_eliminated_event(game_id, event_no, len, event_data);
          break;
        case GAME_OVER:
          events[key] = game_over_event(game_id, event_no, len, event_data);
          break;
        default:
          pos = event_beg + len;
          break;
      }
    }

    auto key = make_pair(current_game_id, next_event_no);
    while(events.find(key) != events.end()) {
      next_event_no++;
      key.second++;
    }
    return OK;
  }

  int send_ui_server(int socket, events_map events, uint32_t next_event_no, uint32_t game_id)
  {
    auto iter = events.find(make_pair(game_id, next_event_no));
    if(iter == events.end()) {
      return OK;
    }
    stringstream stream;
    auto event = iter->second;
    switch (event.event_type) {
      case NEW_GAME:
        stream << "NEW_GAME " << event.x << " "<< event.y;
        for(auto i = Event::players.begin(); i != Event::players.end(); ++i) {
          stream << " " << *i;
        }
        break;
      case PIXEL:
        stream << "PIXEL " << event.x << " " << event.y << " " <<
          Event::players[event.player_number];
        break;
      case PLAYER_ELIMINATED:
        stream << Event::players[event. player_number];
        break;
    }
    string s = stream.str();
    const char* buffer = s.c_str();
    int len = send(socket, buffer, s.size(), 0);
    if(len < 0 || (size_t) len != s.size()) {
      return error("Partial/failed send to ui");
    }
    return OK;
  }

}

int main(int argc, char *argv[])
{
  if(argc < 3 || argc > 4) {
    cerr <<"Usage:" << argv[0] <<" player_name game_server_host[:port] [ui_server_host[:port]]\n";
    return ERROR;
  }

  struct timeval tv;
  if(gettimeofday(&tv, NULL) != 0 ) {
    return error("Getting time");
  }

  uint64_t session_id = tv.tv_sec *  1000000 + tv.tv_usec;
  int8_t turn_direction = 0;
  uint32_t next_event_no = 0, game_id = 0;
  events_map events;

  string player_name;
  struct addrinfo* game_host;
  struct addrinfo* ui_host;

  if(parse_argv(argc, argv, player_name, game_host, ui_host) != OK) {
    return ERROR;
  }

  int game_server = socket(game_host->ai_family, game_host->ai_socktype, game_host->ai_protocol);
  if(game_server < 0) {
    return error("Creating game server socket");
  }
  int ui_server = socket(ui_host->ai_family, ui_host->ai_socktype, ui_host->ai_protocol);
  if(ui_server < 0) {
    return error("Creating ui server socket");
  }
  int tmp_flag = 1;
  if(setsockopt(ui_server, IPPROTO_TCP, TCP_NODELAY, (void *) &tmp_flag,
      sizeof(int)) != 0 ) {
    return error("Socket options setting");
  }

  cout << inet_ntoa(((struct sockaddr_in*) (ui_host->ai_addr))->sin_addr) << " " << ntohs(((struct sockaddr_in*) (ui_host->ai_addr))->sin_port) << endl;
  if (connect(game_server, game_host->ai_addr, game_host->ai_addrlen) != 0) {
    cout << errno << "  " << strerror(errno) << endl;
    return error("Connect game server");
  }
  if (connect(ui_server, ui_host->ai_addr, ui_host->ai_addrlen) != 0) {
    cout << errno << "  " << strerror(errno) << endl;
    return error("Connect ui server");
  }

  freeaddrinfo(game_host);
  freeaddrinfo(ui_host);


  int timer = timerfd_create(CLOCK_REALTIME, 0);
  if(timer < 0) {
    return error("Timer creation");
  }
  struct itimerspec timer_period;
  timer_period.it_interval.tv_sec = 0;
  timer_period.it_interval.tv_nsec = 20 * 1000000;
  timer_period.it_value.tv_sec = 0;
  timer_period.it_value.tv_nsec = 20 * 1000000;

  int const nfds = 3;

  struct pollfd fds[nfds];

  fds[0].fd = timer;
  fds[0].events = POLLIN;
  fds[1].fd = game_server;
  fds[1].events = POLLIN;
  fds[2].fd = ui_server;
  fds[2].events = POLLIN | POLLOUT;

  // join the game
  if(send_game_server(game_server, session_id, 1, 0, player_name) != OK) {
    return ERROR;
  }
  if(timerfd_settime(timer, 0, &timer_period, NULL) < 0) {
    return error("Timer setting");
  }
  while(true) {
    if(poll(fds,(nfds_t) nfds, -1) < 0) {
      return error("Poll error");
    }
    if(fds[0].revents & POLLIN) {
      uint64_t tmp;
      if(read(fds[0].fd, &tmp, 8) < 0) {
        return error("Failed read from timer fd");
      }
      for(; tmp > 0; --tmp) {
        if(send_game_server(game_server, session_id, turn_direction, next_event_no,
          player_name) != OK) {
          return ERROR;
        }
      }
    }
    if(fds[1].revents & POLLIN) {
      if(recive_game_server(game_server, events, next_event_no, game_id) != OK){
        return ERROR;
      }
    }
    if(fds[2].revents & POLLIN) {
      if(recive_ui_server(ui_server, turn_direction) != OK){
        return ERROR;
      }
    }
    if(fds[2].revents & POLLOUT) {
      if(send_ui_server(ui_server, events, next_event_no, game_id) != OK) {
        return ERROR;
      }
    }
  }

  if (close(game_server) == -1) {
    return error("Game server socket close");
  }
  if (close(ui_server) == -1) {
    return error("Ui server socket close");
  }

  return 0;

}
