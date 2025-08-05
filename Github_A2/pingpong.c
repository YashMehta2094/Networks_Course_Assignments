/**
 Boilerplate code implementing the GUI for the pingpong game. Update the file accordingly as necessary.
 You are allowed to update functions, add new functions, modify the stuctures etc. Keep the output graphics intact.

 CS3205 - Assignment 2 (Holi'25)
 Instructor: Ayon Chakraborty
 **/

 #include <ncurses.h>
 #include <pthread.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <stdio.h>
 #include <string.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>


 #define WIDTH 80
 #define HEIGHT 30
 #define OFFSETX 10
 #define OFFSETY 5

 typedef struct {
     int x, y;
     int dx, dy;
 } Ball;

 typedef struct {
     int x;
     int width;
 } Paddle;

 typedef struct {
     Ball ball;
     Paddle paddleA, paddleB;
     int penaltyA, penaltyB;
     int game_running;
 } GameState;

// This is the struct that will be used for communication

 const int stride = 1;
 int server_fd, client_fd;
 Ball ball;
 Paddle paddleA,paddleB;
 int game_running = 1;
 int penaltyA = 0;
 int penaltyB = 0;
 GameState game;
 pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
 void init();
 void* read_threadA(void *args);
 void* write_threadA(void *args);
 void* read_threadB(void* args);
 void* write_threadB(void* args);
 void client();
 void server();
 void end_game();
 void draw(WINDOW *win);
 void *handle_render(void *args);
 void *move_ballA(void *args);
 void *move_ballB(void *args);
 void *read_from_server(void *args);
 void *move_ball(void *args);
 void *rcv_paddleB_from_client_handler(void *args);
 void update_paddle_old(int ch);
 void update_paddleA(int ch);
 void update_paddleB(int ch);
 void reset_ball();
 void* handle_networkA(void* args);
 void* handle_networkB(void* args);
 void server();
 void client();
 int penalty;
 Paddle paddle;


// check whether we are running the client or the server
int main(int argc, char *argv[]) {

     ball = (Ball){WIDTH / 2, HEIGHT / 2, 1, 1};
     paddleA = (Paddle){WIDTH / 2 - 3, 10};
     paddleB = (Paddle){WIDTH / 2 - 3, 10};
     game = (GameState){ball, paddleA, paddleB, penaltyA, penaltyB,game_running};


     if (strcmp(argv[1],"server")==0){
         printf("Server\n");
         // pthread_create(&ball_thread, NULL, move_ballA, NULL);
         struct sockaddr_in address;
         int addrlen = sizeof(address);

         server_fd = socket(AF_INET, SOCK_STREAM, 0); //step 1
         memset(&address, '\0', sizeof(address));  //step 2, next 3 lines
         address.sin_family = AF_INET;
         address.sin_addr.s_addr = htonl(INADDR_ANY);
         address.sin_port = htons(atoi(argv[2]));

         bind(server_fd, (struct sockaddr *)&address, sizeof(address)); //step 3
         listen(server_fd, 5); //step 4

         client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
         printf("Client connected\n");
         server();
         close(client_fd);
     }
     else if(strcmp(argv[1],"client")==0){

         struct sockaddr_in address;
         int addrlen = sizeof(address);
         char buffer[100] = {0};

         client_fd = socket(AF_INET, SOCK_STREAM, 0);
         memset(&address, '\0', sizeof(address));
         address.sin_family = AF_INET;
         address.sin_port = htons(12345);
         printf("IP: %s\n",argv[2]);
         inet_pton(AF_INET,argv[2], &address.sin_addr); //change the IP accordingly

         connect(client_fd, (struct sockaddr *)&address, addrlen);
         printf("Connected to server\n");
         client();
         close(client_fd);
     }
     return 0;
 }

// use different threads for each of - receieve, send, computing ball's motion, etc..
void server()
{
  printf("Hello from Server!\n");
  init();

    pthread_t ball_thread, network_threadA_read, network_threadA_write;
    pthread_create(&ball_thread, NULL, move_ballA, NULL);
    pthread_create(&network_threadA_write, NULL, write_threadA,NULL);
    pthread_create(&network_threadA_read, NULL, read_threadA, NULL);

    while (game_running) {
        int ch = getch();
        if (ch == 'q') {
            game_running = 0;
            break;
        }
        update_paddleA(ch);
        draw(stdscr);
    }

    pthread_join(network_threadA_read,NULL);
    pthread_join(ball_thread,NULL);
    pthread_join(network_threadA_write,NULL);
    end_game();
    return;
}


void client()
{
  printf("Hello from Client!\n");
  init();

    pthread_t network_threadB_write, network_threadB_read;
    pthread_create(&network_threadB_read, NULL, read_threadB,NULL);
    pthread_create(&network_threadB_write,NULL, write_threadB, NULL);

    while (game_running) {
        int ch = getch();
        if (ch == 'q') {
            game_running = 0;
            break;
        }
        update_paddleB(ch);
        draw(stdscr);
    }

    pthread_join(network_threadB_read,NULL);
    pthread_join(network_threadB_write,NULL);
    end_game();
    return;
}

void* read_threadA(void* args) {
    int new_paddleB_x;
    while (game_running) {
        if (read(client_fd, &new_paddleB_x, sizeof(new_paddleB_x)) > 0) {
            paddleB.x = new_paddleB_x;
        }
        usleep(10000);
    }
    return NULL;
}

void* write_threadA(void* args) {
    while (game_running) {
        GameState new_gamestate = {ball, paddleA, paddleB, penaltyA, penaltyB, game_running};
        write(client_fd, &new_gamestate, sizeof(new_gamestate));
        usleep(10000);
    }
    return NULL;
}

void* read_threadB(void* args) {
    GameState new_gamestate;
    while (game_running) {
        if (read(client_fd, &new_gamestate, sizeof(new_gamestate)) > 0) {
            paddleA = new_gamestate.paddleA;
            ball = new_gamestate.ball;
            game_running = new_gamestate.game_running;
            penaltyA = new_gamestate.penaltyA;
            penaltyB = new_gamestate.penaltyB;
        }
        usleep(10000);
    }
    return NULL;
}

void* write_threadB(void* args) {
    while (game_running) {
        write(client_fd, &paddleB.x, sizeof(paddleB.x));
        usleep(10000);
    }
    return NULL;
}


void reset_ball() {
    ball.x = OFFSETX + WIDTH / 2;
    ball.y = OFFSETY + HEIGHT / 2;
    ball.dx = 1;
    ball.dy = 1;
}

void* handle_networkB(void* args)
{
   GameState new_gamestate;
   while(game_running)
   {
        write(client_fd, &paddleB.x, sizeof(paddleB.x));
        read(client_fd, &new_gamestate,sizeof(new_gamestate));
        paddleA = new_gamestate.paddleA;
        ball = new_gamestate.ball;
        game_running = new_gamestate.game_running;
        penaltyA = new_gamestate.penaltyA;
        penaltyB = new_gamestate.penaltyB;
        usleep(10000);
   }
   return NULL;
}

void* handle_networkA(void* args)
{
   int new_paddleB_x;
   //GameState new_gamestate = (GameState){ball, paddleA, paddleB, 0, 0,game_running};
   while(game_running)
   {
       read(client_fd, &new_paddleB_x, sizeof(new_paddleB_x));
       paddleB.x = new_paddleB_x;
       GameState new_gamestate = (GameState){ball, paddleA, paddleB, penaltyA, penaltyB,game_running};
       write(client_fd, &new_gamestate, sizeof(new_gamestate));
       usleep(10000);
   }
   return NULL;
}

void draw(WINDOW *win) {
     clear();  // Clear the screen

     // Draw the border
     attron(COLOR_PAIR(1));
     for (int i = OFFSETX; i <= OFFSETX + WIDTH; i++) {
         mvprintw(OFFSETY-1, i, " ");
     }
     mvprintw(OFFSETY-1, OFFSETX + 3, "CS3205 NetPong, Ball: %d, %d", ball.x, ball.y);
     mvprintw(OFFSETY-1, OFFSETX + WIDTH-25, "Player A: %d, Player B: %d", penaltyA, penaltyB);

     for (int i = OFFSETY; i < OFFSETY + HEIGHT; i++) {
         mvprintw(i, OFFSETX, "  ");
         mvprintw(i, OFFSETX + WIDTH - 1, "  ");
     }
     for (int i = OFFSETX; i < OFFSETX + WIDTH; i++) {
         mvprintw(OFFSETY, i, " ");
         mvprintw(OFFSETY + HEIGHT - 1, i, " ");
     }
     attroff(COLOR_PAIR(1));

     // Draw the ball
     mvprintw(OFFSETY + ball.y, OFFSETX + ball.x, "o");

     // Draw the paddle
     attron(COLOR_PAIR(2));
     for (int i = 0; i < paddleA.width; i++) {
         mvprintw(OFFSETY + HEIGHT-2, OFFSETX + paddleA.x + i, " ");
     }
     for (int i = 0; i < paddleB.width; i++) {
         mvprintw(OFFSETY+1, OFFSETX + paddleB.x + i, " ");
     }
     attroff(COLOR_PAIR(2));

     refresh();
 }

void *move_ballA(void *args) {
     while (game_running){
      // Move the ball
         ball.x += ball.dx;
         ball.y += ball.dy;

         if (ball.y == 2 && ball.x >= paddleB.x -1 && ball.x < paddleB.x + paddleB.width + 1) {
             ball.dy = -ball.dy;
         }

         // Ball goes past paddle (Game Over)
         if (ball.y <= 1) {
             penaltyA++;
             reset_ball();
         }

         // Ball bounces off left and right walls
         if (ball.x <= 2 || ball.x >= WIDTH - 2) {
             ball.dx = -ball.dx;
         }

         // Ball hits the paddle
         if (ball.y == HEIGHT - 3 && ball.x >= paddleA.x -1 && ball.x < paddleA.x + paddleA.width + 1) {
             ball.dy = -ball.dy;
         }

         // Ball goes past paddle (Game Over)
         if (ball.y >= HEIGHT - 2) {
             penaltyB++;
             reset_ball();
         }
             usleep(50000);
     }
     return NULL;
 }

void update_paddleA(int ch) {
     if (ch == KEY_LEFT && paddleA.x > 2) {
         paddleA.x -= 1;  // Move paddle left
   }
     if (ch == KEY_RIGHT && paddleA.x < WIDTH - paddleA.width - 1) {
         paddleA.x += 1;  // Move paddle right
     }
 }

void update_paddleB(int ch) {
     if (ch == KEY_LEFT && paddleB.x > 2) {
         paddleB.x -= 1;  // Move paddle left
   }
     if (ch == KEY_RIGHT && paddleB.x < WIDTH - paddleB.width - 1) {
         paddleB.x += 1;  // Move paddle right
     }
 }

void init() {
    initscr();
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_WHITE);
    init_pair(2, COLOR_YELLOW, COLOR_YELLOW);
    timeout(10);
    keypad(stdscr, TRUE);
    curs_set(FALSE);
    noecho();
}

void end_game() {
    endwin();  // End curses mode
}