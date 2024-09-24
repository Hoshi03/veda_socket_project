#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PORT 5100
#define BUF_SIZE 1024

int main()
{

// 소켓 생성해서 서버와 연결
#pragma region

    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];
    ssize_t read_size;

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("socket create error");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    //내부망
    // server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    //
    server_addr.sin_addr.s_addr = inet_addr("192.168.0.98");
    server_addr.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("connect error");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");

    // 로그인, 회원가입시 사용할 변수 저장
    char id[44];
    char pw[20];
    char nickname[20];
    char send[BUF_SIZE];

#pragma endregion
    int idx = 1; // 회원가입 , 로그인용
    while (idx)
    {
        // 회원가입 or 로그인 선택
        printf("1 : 회원가입 2: 로그인\n");
        if (fgets(buffer, sizeof(buffer), stdin) != NULL)
        {
            // 개행 문자 제거
            buffer[strcspn(buffer, "\n")] = '\0';
            idx = atoi(buffer);
        }

        if (idx == 1) // 회원가입
        {
            char registerQuery[80] = "register: ";
            printf("Enter ID: ");
            fgets(id, sizeof(id), stdin);
            id[strlen(id) - 1] = 0;
            printf("Enter PW: ");
            fgets(pw, sizeof(pw), stdin);
            pw[strlen(pw) - 1] = 0;
            printf("nickname: ");
            fgets(nickname, sizeof(nickname), stdin);
            nickname[strlen(nickname) - 1] = 0;

            strcat(registerQuery, id);
            strcat(registerQuery, " ");
            strcat(registerQuery, pw);
            strcat(registerQuery, " ");
            strcat(registerQuery, nickname);
            write(sock, registerQuery, strlen(registerQuery));

            memset(buffer, 0, BUF_SIZE);
            read_size = read(sock, buffer, BUF_SIZE - 1);
            if (read_size > 0)
            {
                buffer[read_size] = '\0';
                printf("register response: %s\n", buffer);

                if (strcmp(buffer, "register_success") == 0)
                {
                    printf("register success.\n");
                }

                else if (strcmp(buffer, "register_fail") == 0)
                {
                    printf("register failed. Try again.\n");
                }
            }
            else if (read_size == 0)
            {
                printf("Server disconnected.\n");
            }
            else
            {
                perror("client read error");
            }
        }

        else if (idx == 2)
        { // 로그인
            char loginQuery[80] = "login: ";
            printf("Enter ID: ");
            fgets(id, sizeof(id), stdin);
            id[strlen(id) - 1] = 0;
            printf("Enter PW: ");
            fgets(pw, sizeof(pw), stdin);
            pw[strlen(pw) - 1] = 0;

            // "login : id pw" 형태로 로그인 쿼리를 만드어서 서버에 전송
            strcat(loginQuery, id);
            strcat(loginQuery, " ");
            strcat(loginQuery, pw);
            write(sock, loginQuery, strlen(loginQuery));

            memset(buffer, 0, BUF_SIZE);
            read_size = read(sock, buffer, BUF_SIZE - 1);
            if (read_size > 0)
            {
                buffer[read_size] = '\0';
                printf("login response: %s\n", buffer);

                if (strcmp(buffer, "login_fail") != 0)
                {
                    strcpy(nickname, buffer);
                    printf("nickname : %s\n", nickname);
                    printf("Login success!\n");
                    idx = 0;
                }

                else if (strcmp(buffer, "login_fail") == 0)
                {
                    printf("Login failed. Try again.\n");
                }
            }
            else if (read_size == 0)
            {
                printf("Server disconnected.\n");
            }
            else
            {
                perror("client read error");
            }
        }
    }
    int pid = fork();

// 메세지 보내기
#pragma region

    if (pid == 0)
    {
        while (1)
        {
            memset(buffer, 0, BUF_SIZE);

            if (fgets(buffer, BUF_SIZE, stdin) != NULL)
            {

                size_t len = strlen(buffer);
                if (len > 0 && buffer[len - 1] == '\n')
                {
                    buffer[len - 1] = '\0';
                }

                if (strcmp(buffer, "quit") == 0)
                {
                    close(sock);
                    exit(0);
                    return 0;
                }

                if (strlen(buffer) == 0)
                {
                    strcpy(buffer, " ");
                }

                memset(send, 0, BUF_SIZE);
                strcat(send, "sender: ");
                strcat(send, nickname);
                strcat(send, " msg: ");
                strcat(send, buffer);

                if (write(sock, send, strlen(send)) == -1)
                {
                    perror("client write error");
                    close(sock);
                    exit(EXIT_FAILURE);
                }
            }
        }
        printf("stop sending message to server\n");
    }
#pragma endregion

// 서버로부터 메세지 받기
#pragma region
    else if (pid > 0)
    {
        while (1)
        {
            memset(buffer, 0, BUF_SIZE);
            read_size = read(sock, buffer, BUF_SIZE - 1);
            if (read_size > 0)
            {
                buffer[read_size] = '\0';
                printf("%s\n", buffer);
            }
            else if (read_size == 0)
            {
                printf("Server disconnected.\n");
                break;
            }
            else
            {
                perror("client read error");
                break;
            }
        }

        printf("stop waiting server response\n");
        close(sock);
    }
#pragma endregion

    else
    {
        perror("fork error");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return 0;
}
