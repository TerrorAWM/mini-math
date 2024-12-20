#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>

#define PORT 6725
#define BUFFER_SIZE 256
#define MAX_ROOMS 20
#define MAX_PLAYERS_PER_ROOM 2
#define QUESTION_COUNT 10

typedef struct {
    int clients[MAX_PLAYERS_PER_ROOM]; // 客户端套接字数组
    int player_count;                  // 当前房间内的玩家数量
    char room_id[7];                   // 房间代码
    char questions[QUESTION_COUNT][BUFFER_SIZE]; // 存储生成的题目
    int answers[QUESTION_COUNT]; // 存储正确答案 ('>' 为 '>'，'<' 为 '<')
    int scores[MAX_PLAYERS_PER_ROOM]; // 存储每个客户端的得分
    pthread_mutex_t lock;              // 房间的互斥锁
    pthread_cond_t cond;               // 条件变量
} Room;

Room rooms[MAX_ROOMS]; // 房间列表
int room_count = 0;    // 当前房间数量
pthread_mutex_t room_mutex = PTHREAD_MUTEX_INITIALIZER; // 用于保护房间列表的互斥锁

// 生成6位房间ID
void generateRoomID(char *id) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 6; i++) {
        id[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    id[6] = '\0';
}

// 去除字符串前后的空白字符
void trim(char *str) {
    char *end;

    // 去除前导空白字符
    while (isspace((unsigned char)*str)) str++;

    // 如果字符串全是空白字符
    if (*str == 0) return;

    // 去除尾部空白字符
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // 写入新的字符串结束符
    *(end + 1) = '\0';
}

// 生成比大小的问题
void generateComparisonQuestions(Room *room) {
    for (int i = 0; i < QUESTION_COUNT; i++) {
        int num1 = rand() % 1000;
        int num2 = rand() % 1000;
        room->answers[i] = (num1 > num2) ? '>' : '<';
        snprintf(room->questions[i], BUFFER_SIZE, "%d ? %d\n", num1, num2);
    }
}

// 发送题目到客户端
void sendQuestions(int client, Room *room) {
    for (int i = 0; i < QUESTION_COUNT; i++) {
        send(client, room->questions[i], strlen(room->questions[i]), 0);
        usleep(50000);
    }
    send(client, "END\n", strlen("END\n"), 0);
}

// 计算客户端的得分
int calculateScore(Room *room, char *client_answers) {
    int score = 0;
    for (int i = 0; i < QUESTION_COUNT; i++) {
        if (client_answers[i] == room->answers[i]) {
            score++;
        }
    }
    return score;
}

// 处理单人游戏模式
void handleSinglePlayer(int client_socket) {
    char buffer[BUFFER_SIZE];
    char questions[QUESTION_COUNT][BUFFER_SIZE];
    int answers[QUESTION_COUNT];

    // 生成题目
    for (int i = 0; i < QUESTION_COUNT; i++) {
        int num1 = rand() % 1000;
        int num2 = rand() % 1000;
        answers[i] = (num1 > num2) ? '>' : '<';
        snprintf(questions[i], BUFFER_SIZE, "%d ? %d\n", num1, num2);
    }

    // 发送题目到客户端
    for (int i = 0; i < QUESTION_COUNT; i++) {
        send(client_socket, questions[i], strlen(questions[i]), 0);
        usleep(50000);
    }
    send(client_socket, "END\n", strlen("END\n"), 0);

    // 接收客户端的答案
    char client_answers[QUESTION_COUNT];
    int bytes_received = recv(client_socket, client_answers, QUESTION_COUNT, 0);
    if (bytes_received <= 0) {
        printf("接收客户端答案失败。\n");
        close(client_socket);
        return;
    }

    // 计算得分
    int score = 0;
    for (int i = 0; i < QUESTION_COUNT; i++) {
        if (client_answers[i] == answers[i]) {
            score++;
        }
    }

    // 发送得分给客户端
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "Your score: %d/%d\n", score, QUESTION_COUNT);
    send(client_socket, message, strlen(message), 0);

    close(client_socket);
}

// 处理创建房间请求
void handleCreateRoom(int client_socket) {
    pthread_mutex_lock(&room_mutex);

    if (room_count >= MAX_ROOMS) {
        // 房间已满，无法创建新房间
        send(client_socket, "Server room limit reached\n", strlen("Server room limit reached\n"), 0);
        close(client_socket);
        pthread_mutex_unlock(&room_mutex);
        return;
    }

    // 创建新房间
    Room *room = &rooms[room_count++];
    memset(room, 0, sizeof(Room)); // 初始化房间
    generateRoomID(room->room_id);
    room->clients[0] = client_socket;
    room->player_count = 1;
    pthread_mutex_init(&room->lock, NULL);
    pthread_cond_init(&room->cond, NULL);

    // 将房间代码发送给客户端
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "%s\n", room->room_id);
    send(client_socket, message, strlen(message), 0);

    pthread_mutex_unlock(&room_mutex);

    // 等待其他玩家加入房间
    pthread_mutex_lock(&room->lock);
    while (room->player_count < MAX_PLAYERS_PER_ROOM) {
        pthread_cond_wait(&room->cond, &room->lock);
    }
    pthread_mutex_unlock(&room->lock);

    // 开始游戏
    generateComparisonQuestions(room);

    // 发送题目给所有玩家
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++) {
        sendQuestions(room->clients[i], room);
    }

    // 接收玩家的答案并计算得分
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++) {
        int client_socket = room->clients[i];
        char client_answers[QUESTION_COUNT];
        int bytes_received = recv(client_socket, client_answers, QUESTION_COUNT, 0);
        if (bytes_received <= 0) {
            close(client_socket);
            room->clients[i] = -1;
            continue;
        }
        room->scores[i] = calculateScore(room, client_answers);
    }

    // 发送游戏结果
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++) {
        int client_socket = room->clients[i];
        if (client_socket != -1) {
            char message[BUFFER_SIZE];
            snprintf(message, BUFFER_SIZE, "Your score: %d/%d\n", room->scores[i], QUESTION_COUNT);
            send(client_socket, message, strlen(message), 0);
            close(client_socket);
        }
    }

    // 关闭房间
    pthread_mutex_lock(&room_mutex);

    // 销毁互斥锁和条件变量
    pthread_mutex_destroy(&room->lock);
    pthread_cond_destroy(&room->cond);

    // 从房间列表中移除房间
    for (int i = 0; i < room_count; i++) {
        if (&rooms[i] == room) {
            // 将最后一个房间移动到当前位置
            rooms[i] = rooms[room_count - 1];
            room_count--;
            break;
        }
    }

    pthread_mutex_unlock(&room_mutex);
}

// 处理加入房间请求
void handleJoinRoom(int client_socket) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // 接收房间代码
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return;
    }
    buffer[bytes_received] = '\0';
    trim(buffer); // 去除换行符和空格

    pthread_mutex_lock(&room_mutex);

    Room *room = NULL;
    // 查找匹配的房间
    for (int i = 0; i < room_count; i++) {
        if (strcmp(rooms[i].room_id, buffer) == 0) {
            room = &rooms[i];
            break;
        }
    }

    if (room == NULL) {
        // 房间不存在
        send(client_socket, "Room not found\n", strlen("Room not found\n"), 0);
        close(client_socket);
        pthread_mutex_unlock(&room_mutex);
        return;
    }

    pthread_mutex_lock(&room->lock);

    if (room->player_count >= MAX_PLAYERS_PER_ROOM) {
        // 房间已满
        send(client_socket, "Room is full\n", strlen("Room is full\n"), 0);
        close(client_socket);
        pthread_mutex_unlock(&room->lock);
        pthread_mutex_unlock(&room_mutex);
        return;
    }

    // 添加客户端到房间
    room->clients[room->player_count++] = client_socket;
    send(client_socket, "Joined room\n", strlen("Joined room\n"), 0);

    // 如果房间已满，唤醒等待的线程
    if (room->player_count == MAX_PLAYERS_PER_ROOM) {
        pthread_cond_broadcast(&room->cond);
    }

    pthread_mutex_unlock(&room->lock);
    pthread_mutex_unlock(&room_mutex);

    // 等待其他玩家准备就绪
    pthread_mutex_lock(&room->lock);
    while (room->player_count < MAX_PLAYERS_PER_ROOM) {
        pthread_cond_wait(&room->cond, &room->lock);
    }
    pthread_mutex_unlock(&room->lock);

    // 开始游戏
    // 在创建房间的线程中已经生成题目，这里只需要发送题目

    // 发送题目给所有玩家
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++) {
        sendQuestions(room->clients[i], room);
    }

    // 接收玩家的答案并计算得分
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++) {
        int client_socket = room->clients[i];
        char client_answers[QUESTION_COUNT];
        int bytes_received = recv(client_socket, client_answers, QUESTION_COUNT, 0);
        if (bytes_received <= 0) {
            close(client_socket);
            room->clients[i] = -1;
            continue;
        }
        room->scores[i] = calculateScore(room, client_answers);
    }

    // 发送游戏结果
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++) {
        int client_socket = room->clients[i];
        if (client_socket != -1) {
            char message[BUFFER_SIZE];
            snprintf(message, BUFFER_SIZE, "Your score: %d/%d\n", room->scores[i], QUESTION_COUNT);
            send(client_socket, message, strlen(message), 0);
            close(client_socket);
        }
    }

    // 关闭房间
    pthread_mutex_lock(&room_mutex);

    // 销毁互斥锁和条件变量
    pthread_mutex_destroy(&room->lock);
    pthread_cond_destroy(&room->cond);

    // 从房间列表中移除房间
    for (int i = 0; i < room_count; i++) {
        if (&rooms[i] == room) {
            // 将最后一个房间移动到当前位置
            rooms[i] = rooms[room_count - 1];
            room_count--;
            break;
        }
    }

    pthread_mutex_unlock(&room_mutex);
}

// 处理客户端连接
void *handleClient(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // 接收模式选择（SINGLE、CREATE 或 JOIN）
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return NULL;
    }
    buffer[bytes_received] = '\0';
    trim(buffer);

    if (strcmp(buffer, "SINGLE") == 0) {
        // 处理单人游戏模式
        printf("客户端进入单人游戏模式。\n");
        handleSinglePlayer(client_socket);
    } else if (strcmp(buffer, "CREATE") == 0) {
        // 客户端请求创建房间
        printf("客户端请求创建房间。\n");
        handleCreateRoom(client_socket);
    } else if (strcmp(buffer, "JOIN") == 0) {
        // 客户端请求加入房间
        printf("客户端请求加入房间。\n");
        handleJoinRoom(client_socket);
    } else {
        // 无效的请求类型
        send(client_socket, "Invalid request\n", strlen("Invalid request\n"), 0);
        close(client_socket);
    }

    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    srand(time(NULL));

    // 创建服务器套接字
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 设置端口重用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 绑定套接字
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("服务器正在运行，等待连接...\n");

    while (1) {
        int *new_socket = malloc(sizeof(int));
        *new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (*new_socket < 0) {
            perror("accept");
            free(new_socket);
            continue;
        }

        printf("客户端已连接。\n");

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handleClient, new_socket);
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}