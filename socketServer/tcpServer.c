#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <signal.h>
#include <mariadb/mysql.h>

#define PORT 5100
#define BUF_SIZE 1024
#define MAX_CLIENTS 50
int client_sockets[MAX_CLIENTS];

void daemonize()
{
    pid_t pid, sid;
    pid = fork();
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS); 
    }

  
    sid = setsid();
    if (sid < 0)
    {
        exit(EXIT_FAILURE);
    }


    pid = fork();
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS); 
    }

    // 작업 디렉토리를 루트로 변경
    if (chdir("/") < 0)
    {
        exit(EXIT_FAILURE);
    }

    umask(0);


    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    openlog("chat_server", LOG_PID, LOG_DAEMON);
    syslog(LOG_NOTICE, "Daemon started");
}



// Azure MySQL에 연결하는 함수
MYSQL *connect_db()
{
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL)
    {
        fprintf(stderr, "mysql_init() failed\n");
        exit(EXIT_FAILURE);
    }

    const char* host = getenv("DB_HOST");
    const char* user = getenv("DB_USER");
    const char* password = getenv("DB_PASS");
    const char* dbname = getenv("DB_NAME");
    int port = atoi(getenv("DB_PORT"));

    // 환경 변수가 제대로 설정되지 않은 경우
    if (host == NULL || user == NULL || password == NULL || dbname == NULL)
    {
        fprintf(stderr, "One or more environment variables are missing.\n");
        exit(EXIT_FAILURE);
    }

    // Azure MySQL에 연결
    if (mysql_real_connect(conn, host, user, password, dbname, port, NULL, 0) == NULL)
    {
        fprintf(stderr, "mysql_real_connect() failed. Error: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(EXIT_FAILURE);
    }
}

// MySQL에 메시지 저장
void save_message_to_db(char *nickname, char *text)
{
    // printf("start save msg\n");
    MYSQL *conn = connect_db();


    char escaped_nickname[64];
    char escaped_text[BUF_SIZE]; 

    mysql_real_escape_string(conn, escaped_nickname, nickname, strlen(nickname));
    mysql_real_escape_string(conn, escaped_text, text, strlen(text));

    char query[BUF_SIZE + 128];
    snprintf(query, sizeof(query), "INSERT INTO messages (nickname, text) VALUES ('%s', '%s')",
             escaped_nickname, escaped_text);

    if (mysql_query(conn, query))
    {
        fprintf(stderr, "INSERT failed. Error: %s\n", mysql_error(conn));
    }

    mysql_close(conn);
}

// MySQL에서 로그인 정보 확인 함수
char *check_login(char *id, char *pw)
{
    MYSQL *conn = connect_db();
    MYSQL_RES *res;
    MYSQL_ROW row;

    char query[128];
    snprintf(query, sizeof(query), "SELECT nickname FROM member WHERE id='%s' AND pw='%s'", id, pw);

    if (mysql_query(conn, query))
    {
        fprintf(stderr, "Login check failed. Error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return "login_fail"; 
    }
    char *nickname;
    res = mysql_store_result(conn);
    if ((row = mysql_fetch_row(res)) != NULL)
    {
        // 로그인 성공, 닉네임 반환
        char *nickname = strdup(row[0]); 
        printf("Login success! Nickname: %s\n", nickname);
        mysql_free_result(res);
        mysql_close(conn);
        return nickname; 
    }

    mysql_free_result(res);
    mysql_close(conn);
    return "login_fail"; // 로그인 실패
}

// MySQL에서 회원 등록 함수
int register_user(char *id, char *pw, char *nickname)
{
    MYSQL *conn = connect_db();
    char query[256];

    snprintf(query, sizeof(query), "INSERT INTO member (id, pw, nickname) VALUES ('%s', '%s', '%s')", id, pw, nickname);

    if (mysql_query(conn, query))
    {
        fprintf(stderr, "Register failed. Error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return 0;
    }

    mysql_close(conn);
    return 1; 
}

void handle_client_signal(int signum)
{
    char buffer[BUF_SIZE];
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_sockets[i] != -1)
        {
            int bytes_read = read(client_sockets[i], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                printf("Received from client %d: %s\n", client_sockets[i], buffer);
                if (strncmp(buffer, "login:", 6) == 0)
                {
                    char id[44], pw[20];
                    sscanf(buffer + 6, "%s %s", id, pw);
                    char *nickname = check_login(id, pw);
                    if (strcmp(nickname, "login_fail") != 0)
                    {
                        printf("Logged in as: %s\n", nickname);
                        write(client_sockets[i], nickname, strlen(nickname));
                        free(nickname);
                    }
                    else
                    {
                        printf("Login failed.\n");
                        write(client_sockets[i], "login_fail", 10);
                    }
                    return;
                }

                else if (strncmp(buffer, "register:", 9) == 0)
                {
                    char id[44], pw[20], nickname[30];
                    sscanf(buffer + 9, "%s %s %s", id, pw, nickname);
                    char *succes = "register_success";
                    char *fail = "register_fail";
                    if (register_user(id, pw, nickname))
                    {
                        write(client_sockets[i], succes, strlen(succes));
                    }
                    else
                    {
                        write(client_sockets[i], fail, strlen(fail));
                    }
                    return;
                }

                else if (strncmp(buffer, "sender:", 7) == 0)
                {
                    // 메시지를 sender: nickname text 형식으로 받음
                    char nickname[30], text[BUF_SIZE], temp[30];
                    sscanf(buffer + 7, "%s %s %s", nickname, temp, text); 

                    // 클라이언트에게 메시지를 전송
                    for (int j = 0; j < MAX_CLIENTS; j++)
                    {
                        if (client_sockets[j] != -1 && client_sockets[j] != client_sockets[i])
                        {
                            write(client_sockets[j], buffer, strlen(buffer));
                        }
                    }

                    // DB에 메시지 저장
                    save_message_to_db(nickname, text);
                }
            }
            else if (bytes_read == 0)
            {
                printf("Client %d disconnected.\n", client_sockets[i]);
                close(client_sockets[i]);
                client_sockets[i] = -1;
            }
            else if (bytes_read == -1)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    perror("read error");
                    close(client_sockets[i]);
                    client_sockets[i] = -1;
                }
            }
        }
    }
}

void zombieCut(int sig)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

int set_socket_async(int socket_fd)
{
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK | O_ASYNC) == -1)
        return -1;

    if (fcntl(socket_fd, F_SETOWN, getpid()) == -1)
        return -1;

    return 0;
}

int add_client_socket(int socket)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_sockets[i] == -1)
        {
            client_sockets[i] = socket;
            return i;
        }
    }
    return -1;
}

int main()
{
    //데몬화
    daemonize();

    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size;

    // 비동기 IO(여기서는 클라이언트의 write)가 일어나면 시그널을 발생시켜서 브로드캐스팅
    struct sigaction sa;
    sa.sa_handler = handle_client_signal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGIO, &sa, NULL) == -1)
    {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }

    // 좀비
    struct sigaction act;
    act.sa_handler = zombieCut;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    if (sigaction(SIGCHLD, &act, NULL) == -1)
    {
        perror("sigaction() error");
        exit(1);
    }

    server_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CLIENTS) == -1)
    {
        perror("listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    memset(client_sockets, -1, sizeof(client_sockets));

    while (1)
    {
        client_addr_size = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_size);

        if (client_socket == -1)
        {
            // if (errno == EAGAIN || errno == EWOULDBLOCK)
            // {
            //     continue;
            // }
            // else
            // {
            //     perror("accept failed");
            //     continue;
            // }
            continue;
        }

        printf("Client connected: %d\n", client_socket);

        if (set_socket_async(client_socket) == -1)
        {
            perror("set_socket_async failed");
            close(client_socket);
            continue;
        }

        if (add_client_socket(client_socket) == -1)
        {
            printf("Max clients reached. Rejecting client.\n");
            close(client_socket);
            continue;
        }
    }

    close(server_socket);
    return 0;
}
