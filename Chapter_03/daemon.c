#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
/* クローズする最大ディスクリプタ値 */
#define MAXFD   64
/* デーモン化 */
int
daemonize(int nochdir, int noclose)
{
    int i, fd;
    pid_t pid;
    if ((pid = fork()) == -1) {
        return (-1);
    } else if (pid != 0) {
        /* 親プロセスの終了 */
        _exit(0);
    }

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
#ifdef UNIT_TEST
#include <syslog.h>
int
main(int argc, char *argv[])
{
    char buf[256];
    /* デーモン化 */
    (void) daemonize(0, 0);
    /* ディスクリプタクローズのチェック */
    (void) fprintf(stderr, "stderr\n");
    /* カレントディレクトリの表示 */
    syslog(LOG_USER|LOG_NOTICE, "daemon:cwd=%s\n", getcwd(buf, sizeof(buf)));
    for(;;){
        sleep(1);
        syslog(LOG_USER|LOG_NOTICE, "daemon sleep 1\n");
    }
    return (EX_OK);
}
#endif
