/* SIGHUPハンドラによる再起動できるサーバ側サンプル */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>

/* クローズする最大ディスクリプタ値 */
#define MAXFD   64

/* コマンドライン引数、環境変数のアドレス保持用 */
int *argc_;
char ***argv_;
char ***envp_;

/* デーモン化 */
int
daemonize(int nochdir, int noclose)
{
    (void) fprintf(stderr, "parent process id (%d)\n", getpid());
    int i, fd;
    pid_t pid;
    if ((pid = fork()) == -1) {
        return (-1);
    } else if (pid != 0) {
        /* 親プロセスの終了 */
        _exit(0);
    }
    (void) fprintf(stderr, "child fork 1 process id (%d)\n", getpid());

    /* 最初の子プロセスにてsetsidで
       端末セッション(tty)からの切り離すために、セッションリーダーにする */
    (void) setsid();

    /* HUPシグナルを無視するように:
       環境によってはセッションリーダーが終了時に子プロセスに対してSIGHUPシグナルを送り、
       それによって子プロセスが終了してしまう可能性があるため、SIGHUPシグナルを無視する。
    */
    (void) signal(SIGHUP, SIG_IGN);

    /** 端末セッションの再取得防止するため２度目fork
        端末関連付けはセッションリーダのみ行える */
    if ((pid = fork()) != 0) {
        /* 最初の子プロセスの終了 */
        _exit(0);
    }
    (void) fprintf(stderr, "child fork 2process id (%d)\n", getpid());
    syslog(LOG_USER|LOG_NOTICE, "start process(%d) as daemon!!!\n", getpid());
    /* デーモンプロセス */
    if (nochdir == 0) {
        /* ルートディレクトリに移動 */
        (void) chdir("/");
    }
    if (noclose == 0) {
        /* すべてのファイルディスクリプタのクローズ */
        for (i = 0; i < MAXFD; i++) {
            (void) close(i);
        }
        /* stdin,stdout,stderrを/dev/nullでオープン */
        if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
            (void) dup2(fd, 0);
            (void) dup2(fd, 1);
            (void) dup2(fd, 2);
            if (fd > 2) {
                (void) close(fd);
            }
        }
    }
    return (0);
}

/* SIGHUPハンドラ */
void
sig_hangup_handler(int sig)
{
    int i;
    syslog(LOG_USER|LOG_NOTICE, "sig_hangup_handler(%d --> %d)!!!\n", getpid(), sig);
    (void) fprintf(stderr, "sig_hangup_handler(%d)\n", sig);
    /* stdin,stdout,stderr以外をクローズ */
    for (i = 3; i < MAXFD; i++) {
        (void) close(i);
    }

    char *args[3];
    args[0]="./re-exec";
	args[1]="8888";
	args[2]="1";
    args[3]=NULL;

    /* 自プロセスの上書き再実行 */
    if (execve((*argv_)[0], args /* (*argv_) */,(*envp_)) == -1) {
        perror("execve");
    }
}
/* サーバソケットの準備 */
int
server_socket(const char *portnm)
{
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res0;
    int soc, opt, errcode;
    socklen_t opt_len;

    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    /* アドレス情報の決定 */
    if ((errcode = getaddrinfo(NULL, portnm, &hints, &res0)) != 0) {
        (void) fprintf(stderr, "getaddrinfo():%s\n", gai_strerror(errcode));
        return (-1);
    }
    if ((errcode = getnameinfo(res0->ai_addr, res0->ai_addrlen,
                               nbuf, sizeof(nbuf),
                               sbuf, sizeof(sbuf),
                               NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        (void) fprintf(stderr, "getnameinfo():%s\n", gai_strerror(errcode));
        freeaddrinfo(res0);
        return (-1);
    }
    (void) fprintf(stderr, "port=%s\n", sbuf);
    /* ソケットの生成 */
    if ((soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol))
        == -1) {
        perror("socket");
        freeaddrinfo(res0);
        return (-1);
    }
    /* ソケットオプション（再利用フラグ）設定 */
    opt = 1;
    opt_len = sizeof(opt);
    if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &opt, opt_len) == -1) {
        perror("setsockopt");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    /* ソケットにアドレスを指定 */
    if (bind(soc, res0->ai_addr, res0->ai_addrlen) == -1) {
        perror("bind");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    /* アクセスバックログの指定 */
    if (listen(soc, SOMAXCONN) == -1) {
        perror("listen");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    freeaddrinfo(res0);
    return (soc);
}
/* アクセプトループ */
void
accept_loop(int soc)
{
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct sockaddr_storage from;
    int acc;
    socklen_t len;
    for (;;) {
        len = (socklen_t) sizeof(from);
        /* 接続受付 */
        if ((acc = accept(soc, (struct sockaddr *) &from, &len)) == -1) {
            if (errno != EINTR) {
                perror("accept");
            }
        } else {
            (void) getnameinfo((struct sockaddr *) &from, len,
                               hbuf, sizeof(hbuf),
                               sbuf, sizeof(sbuf),
                               NI_NUMERICHOST | NI_NUMERICSERV);
            (void) fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);
            /* 送受信ループ */
            send_recv_loop(acc);
            /* アクセプトソケットクローズ */
            close(acc);
            acc = 0;
        }
    }
}
/* サイズ指定文字列連結 */
size_t
mystrlcat(char *dst, const char *src, size_t size)
{
    const char *ps;
    char *pd, *pde;
    size_t dlen, lest;
    for (pd = dst, lest = size; *pd != '\0' && lest !=0; pd++, lest--);
    dlen = pd - dst;
    if (size - dlen == 0) {
        return (dlen + strlen(src));
    }
    pde = dst + size - 1;
    for (ps = src; *ps != '\0' && pd < pde; pd++, ps++) {
        *pd = *ps;
    }
    for (; pd <= pde; pd++) {
        *pd = '\0';
    }
    while (*ps++);
    return (dlen + (ps - src - 1));
}

/* 送受信ループ */
void
send_recv_loop(int acc)
{
    char buf[512], *ptr;
    ssize_t len;
    for (;;) {
        /* 受信 */
        if ((len = recv(acc, buf, sizeof(buf), 0)) == -1) {
            /* エラー */
            perror("recv");
            break;
        }
        if (len == 0) {
            /* エンド・オブ・ファイル */
            (void) fprintf(stderr, "recv:EOF\n");
            break;
        }
        /* 文字列化・表示 */
        buf[len]='\0';
        if ((ptr = strpbrk(buf, "\r\n")) != NULL) {
            *ptr = '\0';
        }
        (void) fprintf(stderr, "[client]%s\n", buf);
        /* 応答文字列作成 */
        (void) mystrlcat(buf, ":OK\r\n", sizeof(buf));
        len = (ssize_t) strlen(buf);
        /* 応答 */
        if ((len = send(acc, buf, (size_t) len, 0)) == -1) {
            /* エラー */
            perror("send");
            break;
        }
    }
}
int
main(int argc, char *argv[], char *envp[])
{

    struct sigaction sa;
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 1) {
        (void) fprintf(stderr,"re-exec port\n");
        return (EX_USAGE);
    }

    if (argc == 3) {
        (void) fprintf(stderr,"skip daemonize\n");
    } else {
        /* デーモン化 */
        (void) daemonize(0, 0);
    }

    /* コマンドライン引数、環境変数のアドレスをグローバルに保持 */
    argc_ = &argc;
    argv_ = &argv;
    envp_ = &envp;
    /* SIGHUPのシグナルハンドラを指定 */
    (void) sigaction(SIGHUP, (struct sigaction *) NULL, &sa);
    sa.sa_handler = sig_hangup_handler;
    sa.sa_flags = SA_NODEFER;
    (void) sigaction(SIGHUP, &sa, (struct sigaction *) NULL);
    (void) fprintf(stderr, "sigaction():end\n");
    /* サーバソケットの準備 */
    if ((soc = server_socket(argv[1])) == -1) {
        (void) fprintf(stderr,"server_socket(%s):error\n", argv[1]);
        return (EX_UNAVAILABLE);
    }
    (void) fprintf(stderr, "ready for accept\n");
    /* アクセプトループ */
    accept_loop(soc);
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}
