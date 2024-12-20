#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define usleep(x) Sleep((x) / 1000)
#define close closesocket
#define CLEAR "cls"
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <termios.h>
#define CLEAR "clear"
#endif

#ifdef _WIN32
int custom_inet_pton(int af, const char *src, void *dst) {
    if (af == AF_INET) {
        unsigned long addr = inet_addr(src);
        if (addr == INADDR_NONE) {
            return 0; // 转换失败
        }
        memcpy(dst, &addr, sizeof(addr));
        return 1; // 转换成功
    }
    return -1; // 不支持的地址族
}
#else
#define custom_inet_pton inet_pton
#endif

// 自定义宏定义
#ifdef _WIN32
#define fopen_s(pFile, filename, mode) ((*(pFile)) = fopen((filename), (mode))) == NULL
#endif

// ================== 配置区域开始 ==================

// 服务器 IP 地址
#define SERVER_IP "47.121.128.254"

// 服务器端口
#define PORT 6725

// 登录接口 URL
#define LOGIN_URL "https://3r.com.pa/minigame/login.php"

// 注册页面 URL
#define REGISTER_URL "https://3r.com.pa/minigame/register"

// 查询准确率接口 URL
#define ACCURACY_URL "https://3r.com.pa/minigame/rank_search.php"

// 发送游戏结果接口 URL
#define GAME_RESULT_URL "https://3r.com.pa/minigame/rank_regis.php"

// =================== 配置区域结束 ===================

#define BUFFER_SIZE 256
#define QUESTION_COUNT 10
#define LOGIN_STATUS_FILE "login_status.txt"

char username[BUFFER_SIZE];

struct string {
    char *ptr;
    size_t len;
};

void start_comparison_game();
void start_single_player_game();
int login();
int check_login_status();
void logout();
void get_password(char *password, int max_length);
void *ai_thread_func(void *arg);
void send_game_results(const char *username, const char *game_result, int correct_answers, int total_questions);
int fetch_user_accuracy();
void trim(char *str);
void init_string(struct string *s);
size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s);

int ai_score = 0;
int user_score = 0;
int user_finished = 0;
int ai_finished = 0;

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Failed to initialize Winsock.\n");
        return 1;
    }
#endif

    system(CLEAR);

    while (1) {
        // 登录流程
        while (!check_login_status()) {
            if (!login()) {
                printf("登录失败，请重试。\n");
            } else {
                printf("登录成功！\n");
            }
        }

        printf("您已登录，欢迎回来！\n");

        // 游戏主面板
        char input[BUFFER_SIZE];
        int stay_in_menu = 1;

        while (stay_in_menu) {
            printf("\n====== 游戏面板 ======\n");
            printf("1. 比大小\n");
            printf("LG. 退出登录\n");
            printf("0. 退出程序\n");
            printf("请选择操作: ");
            fgets(input, BUFFER_SIZE, stdin);
            input[strcspn(input, "\n")] = '\0';
            trim(input);

            if (strlen(input) == 0) {
                printf("输入不能为空，请重试。\n");
                continue;
            }
            if (strcmp(input, "1") == 0) {
                
                start_comparison_game();
            } else if (strcmp(input, "2") == 0) {
                
                start_single_player_game();
            } else if (strcmp(input, "LG") == 0) {
                logout();
                printf("您已成功退出登录。\n");
                stay_in_menu = 0; // 退出登录返回登录流程
            } else if (strcmp(input, "0") == 0) {
                printf("退出程序。\n");
                exit(0); // 退出整个程序
            } else {
                printf("无效选择，请重试。\n");
            }
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

void trim(char *str) {
    char *end;

    // 去除前导空格
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return;

    // 去除尾部空格
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // 写入新的字符串结束符
    *(end + 1) = '\0';
}

void init_string(struct string *s) {
    s->len = 0;
    s->ptr = malloc(s->len + 1);
    if (s->ptr == NULL) {
        fprintf(stderr, "malloc() failed\n");
        exit(EXIT_FAILURE);
    }
    s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s) {
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len + 1);
    if (s->ptr == NULL) {
        fprintf(stderr, "realloc() failed\n");
        exit(EXIT_FAILURE);
    }
    memcpy(s->ptr + s->len, ptr, size * nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;

    return size * nmemb;
}

void get_password(char *password, int max_length) {
    int i = 0;
#ifdef _WIN32
    char ch;
    while ((ch = getch()) != '\r' && i < max_length - 1) { // Enter to end input
        if (ch == '\b' && i > 0) { // Handle backspace
            printf("\b \b");
            i--;
        } else if (ch != '\b') {
            password[i++] = ch;
            printf("*");
        }
    }
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO); // Turn off echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    char ch;
    while (i < max_length - 1 && (ch = getchar()) != '\n') {
        if (ch == 127 && i > 0) { // Handle backspace on Linux (ASCII 127)
            printf("\b \b");
            i--;
        } else if (ch != 127) {
            password[i++] = ch;
            printf("*");
        }
    }
    password[i] = '\0';
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    password[i] = '\0';
    printf("\n");
}

int login() {
    CURL *curl;
    CURLcode res;
    int success = 0;
    char user_input[BUFFER_SIZE], password[BUFFER_SIZE];
    char post_data[BUFFER_SIZE];

    printf("如果未注册请访问以下链接进行注册:\n");
    printf("%s\n\n", REGISTER_URL);

    printf("请输入用户名或邮箱: ");
    fgets(user_input, BUFFER_SIZE, stdin);
    user_input[strcspn(user_input, "\n")] = '\0';

    printf("请输入密码: ");
    get_password(password, BUFFER_SIZE);

    snprintf(post_data, BUFFER_SIZE, "user_input=%s&password=%s", user_input, password);

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        curl_easy_setopt(curl, CURLOPT_URL, LOGIN_URL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        struct string response;
        init_string(&response);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            if (strstr(response.ptr, "\"status\":\"success\"")) {
                FILE *file = fopen(LOGIN_STATUS_FILE, "w");
                if (file) {
                    fprintf(file, "logged_in");
                    fclose(file);
                    strcpy(username, user_input);
                    success = 1;
                } else {
                    printf("无法保存登录状态。\n");
                }
            } else {
                printf("登录失败，请检查用户名或密码。\n");
            }
        } else {
            fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
        }

        free(response.ptr);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return success;
}

int check_login_status() {
    FILE *file = fopen(LOGIN_STATUS_FILE, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

void logout() {
    remove(LOGIN_STATUS_FILE);
}

void start_single_player_game() {
    pthread_t ai_thread;
    int user_accuracy = fetch_user_accuracy();

    srand(time(NULL));

    pthread_create(&ai_thread, NULL, ai_thread_func, &user_accuracy);

    int user_correct = 0;

    for (int i = 0; i < QUESTION_COUNT; i++) {
        int num1 = rand() % 100;
        int num2 = rand() % 100;

        printf("题目 %d: %d ? %d\n", i + 1, num1, num2);
        printf("请输入答案 ('>' 或 '<'): ");
        char input_buffer[BUFFER_SIZE];
        fgets(input_buffer, BUFFER_SIZE, stdin);
        char answer = input_buffer[0];

        char correct_answer = (num1 > num2) ? '>' : '<';
        if (answer == correct_answer) {
            user_correct++;
        }
    }

    user_score = user_correct;
    user_finished = 1;

    pthread_join(ai_thread, NULL);

    const char *result = (user_score > ai_score) ? "WIN" : "LOSE";
    send_game_results(username, result, user_score, QUESTION_COUNT);
    printf("人机对战结束。结果: %s\n", result);
}

void *ai_thread_func(void *arg) {
    int *user_accuracy = (int *)arg;
    int ai_correct = 0;

    for (int i = 0; i < QUESTION_COUNT; i++) {
        int num1 = rand() % 100;
        int num2 = rand() % 100;
        char correct_answer = (num1 > num2) ? '>' : '<';
        char ai_answer = ((rand() % 100) < *user_accuracy) ? correct_answer : (correct_answer == '>' ? '<' : '>');

        if (ai_answer == correct_answer) {
            ai_correct++;
        }

        usleep(500000); // 模拟答题延迟
    }

    ai_score = ai_correct;
    ai_finished = 1;
    return NULL;
}

void send_game_results(const char *username, const char *game_result, int correct_answers, int total_questions) {
    CURL *curl;
    CURLcode res;
    char post_data[BUFFER_SIZE];

    snprintf(post_data, BUFFER_SIZE, "user=%s&game_result=%s&accuracy=%d/%d",
             username, game_result, correct_answers, total_questions);

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        curl_easy_setopt(curl, CURLOPT_URL, GAME_RESULT_URL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
}

int fetch_user_accuracy() {
    CURL *curl;
    CURLcode res;
    int accuracy = 50; // 默认值
    struct string response;
    init_string(&response);

    curl = curl_easy_init();
    if (curl) {
        char url[BUFFER_SIZE];
        snprintf(url, BUFFER_SIZE, "%s?user=%s", ACCURACY_URL, username);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            sscanf(response.ptr, "{\"accuracy\":\"%d\"}", &accuracy);
        }

        free(response.ptr);
        curl_easy_cleanup(curl);
    }
    return accuracy;
}

void start_comparison_game() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char questions[QUESTION_COUNT][BUFFER_SIZE];
    char answers[QUESTION_COUNT];
    int i = 0;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error.\n");
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (custom_inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address/Address not supported.\n");
        close(sock);
        return;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("服务器连接失败，请重试或选择其他模式。\n");
        close(sock);
        return;
    }

    char choice[BUFFER_SIZE];

    while (1) {
        printf("请选择操作：\n");
        printf("1. 单人游戏模式\n");
        printf("2. 创建房间\n");
        printf("3. 加入房间\n");
        printf("请选择: ");
        fgets(choice, BUFFER_SIZE, stdin);
        choice[strcspn(choice, "\n")] = '\0';
        trim(choice);

        if (strlen(choice) == 0) {
            printf("输入不能为空，请重试。\n");
            continue;
        }

        if (strcmp(choice, "1") == 0) {
            // 单人游戏模式
            send(sock, "SINGLE\n", strlen("SINGLE\n"), 0);
            printf("进入单人游戏模式。\n");
            break;
        } else if (strcmp(choice, "2") == 0) {
            // 创建房间
            send(sock, "CREATE\n", strlen("CREATE\n"), 0);
            // 接收房间代码
            int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                printf("接收房间代码失败。\n");
                close(sock);
                return;
            }
            buffer[bytes_received] = '\0';
            printf("房间创建成功，房间代码: %s\n", buffer);
            printf("请将房间代码告知好友，等待好友加入。\n");
            break;
        } else if (strcmp(choice, "3") == 0) {
            // 加入房间
            send(sock, "JOIN\n", strlen("JOIN\n"), 0);
            printf("请输入要加入的房间代码: ");
            char room_code[BUFFER_SIZE];
            fgets(room_code, BUFFER_SIZE, stdin);
            room_code[strcspn(room_code, "\n")] = '\0';
            strcat(room_code, "\n"); // 添加换行符
            send(sock, room_code, strlen(room_code), 0);

            // 接收服务器响应
            int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                printf("接收服务器响应失败。\n");
                close(sock);
                return;
            }
            buffer[bytes_received] = '\0';
            if (strcmp(buffer, "Joined room\n") != 0) {
                printf("无法加入房间：%s\n", buffer);
                close(sock);
                return;
            }
            printf("成功加入房间。\n");
            break;
        } else {
            printf("无效的选择，请重试。\n");
        }
    }

    printf("等待接收题目...\n");

    // 接收题目
    i = 0;
    while (i < QUESTION_COUNT) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("服务器已断开连接。\n");
            close(sock);
            return;
        }
        buffer[bytes_received] = '\0';

        char *line = strtok(buffer, "\n");
        while (line != NULL) {
            if (strcmp(line, "END") == 0) {
                i = QUESTION_COUNT;
                break;
            }

            if (i < QUESTION_COUNT) {
                strcpy(questions[i], line);
                printf("题目 %d: %s\n", i + 1, questions[i]);
                printf("请输入答案 ('>' 或 '<'): ");
                fgets(buffer, BUFFER_SIZE, stdin);
                answers[i] = buffer[0];
                i++;
            }
            line = strtok(NULL, "\n");
        }
    }

    // 发送答案给服务器
    send(sock, answers, QUESTION_COUNT, 0);

    // 接收得分
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("%s\n", buffer);
    }

    close(sock);
}