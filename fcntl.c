#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define DATA_FILE "/tmp/fcntl_demo.dat"

// Структура для данных в файле
typedef struct {
    int counter;            // Счетчик
    int last_pid;           // PID последнего процесса
    char message[256];      // Сообщение
    time_t timestamp;       // Время последнего обновления
} file_data_t;

void print_file_data(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Файл еще не создан или пуст\n");
        return;
    }

    file_data_t data;
    if (fread(&data, sizeof(data), 1, f) == 1) {
        printf("  counter=%d, last_pid=%d, message='%s', time=%s",
               data.counter, data.last_pid, data.message, ctime(&data.timestamp));
    } else {
        printf("Ошибка чтения данных\n");
    }
    fclose(f);
}

void error_exit(const char *msg) {
    perror(msg);
    exit(1);
}

// Инициализация файла с данными
void init_data_file() {
    FILE *f = fopen(DATA_FILE, "wb");
    if (!f) error_exit("fopen для инициализации");

    file_data_t data;
    data.counter = 0;
    data.last_pid = 0;
    strcpy(data.message, "Начальное сообщение");
    data.timestamp = time(NULL);

    if (fwrite(&data, sizeof(data), 1, f) != 1)
        error_exit("fwrite при инициализации");

    fclose(f);
    printf("Файл %s инициализирован\n", DATA_FILE);
}

// Установка блокировки с помощью fcntl
void set_lock(int fd, int type) {
    struct flock lock;
    
    lock.l_type = type;      // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = getpid();
    
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        error_exit("fcntl F_SETLKW");
    }
}

// Попытка установки неблокирующей блокировки
int try_set_lock(int fd, int type) {
    struct flock lock;
    
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = getpid();
    
    return fcntl(fd, F_SETLK, &lock);
}

// Снятие блокировки
void unlock_file(int fd) {
    struct flock lock;
    
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = getpid();
    
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        error_exit("fcntl unlock");
    }
}

// Проверка текущей блокировки
void check_lock(int fd) {
    struct flock lock;
    
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = -1;
    
    if (fcntl(fd, F_GETLK, &lock) == -1) {
        error_exit("fcntl F_GETLK");
    }
    
    if (lock.l_type == F_UNLCK) {
        printf("  Файл не заблокирован\n");
    } else {
        printf("  Файл заблокирован процессом %d, тип блокировки: %s\n", 
               lock.l_pid,
               lock.l_type == F_RDLCK ? "чтение (F_RDLCK)" : "запись (F_WRLCK)");
    }
}

// Демонстрация race condition (без блокировок)
void demonstrate_race_condition() {
    pid_t pid;
    int status;

    printf("\n=== Демонстрация Race Condition с файлом (БЕЗ блокировок) ===\n\n");

    init_data_file();

    printf("Начальное состояние файла:\n");
    print_file_data(DATA_FILE);
    printf("\n");

    printf("Запускаем 5 процессов для одновременного увеличения счетчика...\n");

    for (int i = 0; i < 5; i++) {
        pid = fork();

        if (pid < 0) error_exit("fork failed");

        if (pid == 0) {
            for (int j = 0; j < 3; j++) {
                FILE *f = fopen(DATA_FILE, "r+b");
                if (!f) error_exit("fopen");

                file_data_t data;
                if (fread(&data, sizeof(data), 1, f) != 1) {
                    error_exit("fread");
                }

                int current = data.counter;
                usleep(rand() % 100000);

                data.counter = current + 1;
                data.last_pid = getpid();
                snprintf(data.message, sizeof(data.message),
                        "Обновлен PID %d, итерация %d", getpid(), j);
                data.timestamp = time(NULL);

                rewind(f);
                if (fwrite(&data, sizeof(data), 1, f) != 1) {
                    error_exit("fwrite");
                }
                fflush(f);
                fclose(f);

                printf("Процесс %d: обновил счетчик (было %d, стало %d)\n",
                       getpid(), current, data.counter);

                usleep(rand() % 50000);
            }
            exit(0);
        }
    }

    while (wait(&status) > 0);

    printf("\n=== РЕЗУЛЬТАТ (Race Condition) ===\n");
    printf("Ожидаемое значение: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение:\n");
    print_file_data(DATA_FILE);
}

// Демонстрация корректной работы с fcntl
void demonstrate_with_fcntl() {
    pid_t pid;
    int status;

    printf("\n=== Корректная работа с синхронизацией (fcntl) ===\n\n");

    init_data_file();

    printf("Начальное состояние файла:\n");
    print_file_data(DATA_FILE);
    printf("\n");

    printf("Запускаем 5 процессов с синхронизацией через fcntl...\n");

    for (int i = 0; i < 5; i++) {
        pid = fork();

        if (pid < 0) error_exit("fork failed");

        if (pid == 0) {
            for (int j = 0; j < 3; j++) {
                int fd = open(DATA_FILE, O_RDWR);
                if (fd == -1) error_exit("open");

                set_lock(fd, F_WRLCK);

                file_data_t data;
                
                lseek(fd, 0, SEEK_SET);
                if (read(fd, &data, sizeof(data)) != sizeof(data)) {
                    error_exit("read");
                }

                int current = data.counter;
                usleep(rand() % 50000);

                data.counter = current + 1;
                data.last_pid = getpid();
                snprintf(data.message, sizeof(data.message),
                        "Обновлен PID %d (fcntl)", getpid());
                data.timestamp = time(NULL);

                lseek(fd, 0, SEEK_SET);
                if (write(fd, &data, sizeof(data)) != sizeof(data)) {
                    error_exit("write");
                }
                fsync(fd);

                printf("Процесс %d: обновил счетчик (было %d, стало %d) [с fcntl]\n",
                       getpid(), current, data.counter);

                usleep(10000);

                unlock_file(fd);
                close(fd);

                usleep(rand() % 20000);
            }
            exit(0);
        }
    }

    while (wait(&status) > 0);

    printf("\n=== РЕЗУЛЬТАТ С СИНХРОНИЗАЦИЕЙ (fcntl) ===\n");
    printf("Ожидаемое значение: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение:\n");
    print_file_data(DATA_FILE);
}

int main() {
    printf("===============================================================\n");
    printf("Программа для демонстрации синхронизации доступа к файлу\n");
    printf("с использованием системного вызова fcntl\n");
    printf("===============================================================\n");

    srand(time(NULL));

    unlink(DATA_FILE);

    demonstrate_race_condition();

    demonstrate_with_fcntl();

    printf("\nПрограмма завершена. Сравните результаты с синхронизацией и без нее.\n");

    unlink(DATA_FILE);

    return 0;
}
