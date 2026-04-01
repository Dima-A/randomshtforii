#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define DATA_FILE "/tmp/flock_demo.dat"
#define LOCK_FILE "/tmp/flock_demo.lock"

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

// Чтение данных из файла
int read_data(file_data_t *data) {
    FILE *f = fopen(DATA_FILE, "rb");
    if (!f) return -1;
    
    int result = fread(data, sizeof(file_data_t), 1, f);
    fclose(f);
    
    return result == 1 ? 0 : -1;
}

// Запись данных в файл
int write_data(const file_data_t *data) {
    FILE *f = fopen(DATA_FILE, "wb");
    if (!f) return -1;
    
    int result = fwrite(data, sizeof(file_data_t), 1, f);
    fclose(f);
    
    return result == 1 ? 0 : -1;
}

// Демонстрация race condition (без блокировок)
void demonstrate_race_condition() {
    pid_t pid;
    int status;

    printf("\n=== Демонстрация Race Condition с файлом (БЕЗ блокировок) ===\n\n");

    // Создаем файл с начальными данными
    init_data_file();

    printf("Начальное состояние файла:\n");
    print_file_data(DATA_FILE);
    printf("\n");

    printf("Запускаем 5 процессов для одновременного увеличения счетчика...\n");

    for (int i = 0; i < 5; i++) {
        pid = fork();

        if (pid < 0) error_exit("fork failed");

        if (pid == 0) {  // Дочерний процесс
            for (int j = 0; j < 3; j++) {
                file_data_t data;
                
                // Читаем текущие данные
                if (read_data(&data) == -1) {
                    perror("read_data");
                    exit(1);
                }
                
                int current = data.counter;
                
                // Имитация длительной операции
                usleep(rand() % 100000);
                
                // Race condition: между чтением и записью
                // другой процесс может изменить данные
                data.counter = current + 1;
                data.last_pid = getpid();
                snprintf(data.message, sizeof(data.message),
                        "Обновлен PID %d, итерация %d", getpid(), j);
                data.timestamp = time(NULL);
                
                // Записываем обратно
                if (write_data(&data) == -1) {
                    perror("write_data");
                    exit(1);
                }
                
                printf("Процесс %d: обновил счетчик (было %d, стало %d)\n",
                       getpid(), current, data.counter);
                
                usleep(rand() % 50000);
            }
            exit(0);
        }
    }

    // Ждем завершения всех дочерних процессов
    while (wait(&status) > 0);

    printf("\n=== РЕЗУЛЬТАТ (Race Condition) ===\n");
    printf("Ожидаемое значение счетчика: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение:\n");
    print_file_data(DATA_FILE);
}

// Демонстрация корректной работы с flock
void demonstrate_with_flock() {
    pid_t pid;
    int status;

    printf("\n=== Корректная работа с синхронизацией (flock) ===\n\n");

    // Создаем файл с начальными данными
    init_data_file();

    printf("Начальное состояние файла:\n");
    print_file_data(DATA_FILE);
    printf("\n");

    printf("Запускаем 5 процессов с синхронизацией через flock...\n");

    for (int i = 0; i < 5; i++) {
        pid = fork();

        if (pid < 0) error_exit("fork failed");

        if (pid == 0) {  // Дочерний процесс
            for (int j = 0; j < 3; j++) {
                // Открываем файл с данными
                int data_fd = open(DATA_FILE, O_RDWR);
                if (data_fd == -1) error_exit("open data file");
                
                // БЛОКИРОВКА: захватываем эксклюзивную блокировку на файл
                if (flock(data_fd, LOCK_EX) == -1) 
                    error_exit("flock LOCK_EX");
                
                // КРИТИЧЕСКАЯ СЕКЦИЯ - начало
                file_data_t data;
                
                // Читаем текущие данные
                lseek(data_fd, 0, SEEK_SET);
                if (read(data_fd, &data, sizeof(data)) != sizeof(data)) {
                    perror("read");
                    flock(data_fd, LOCK_UN);
                    close(data_fd);
                    exit(1);
                }
                
                int current = data.counter;
                
                // Имитация длительной операции внутри критической секции
                usleep(rand() % 50000);
                
                // Обновляем данные
                data.counter = current + 1;
                data.last_pid = getpid();
                snprintf(data.message, sizeof(data.message),
                        "Обновлен PID %d (flock)", getpid());
                data.timestamp = time(NULL);
                
                // Записываем обратно
                lseek(data_fd, 0, SEEK_SET);
                if (write(data_fd, &data, sizeof(data)) != sizeof(data)) {
                    perror("write");
                    flock(data_fd, LOCK_UN);
                    close(data_fd);
                    exit(1);
                }
                
                // Принудительно сбрасываем на диск
                fsync(data_fd);
                
                printf("Процесс %d: обновил счетчик (было %d, стало %d) [с flock]\n",
                       getpid(), current, data.counter);
                
                // Дополнительная задержка внутри критической секции
                usleep(10000);
                // КРИТИЧЕСКАЯ СЕКЦИЯ - конец
                
                // Снимаем блокировку
                if (flock(data_fd, LOCK_UN) == -1)
                    error_exit("flock LOCK_UN");
                
                close(data_fd);
                
                // Работа вне критической секции
                usleep(rand() % 20000);
            }
            exit(0);
        }
    }

    // Ждем завершения всех дочерних процессов
    while (wait(&status) > 0);

    printf("\n=== РЕЗУЛЬТАТ С СИНХРОНИЗАЦИЕЙ (flock) ===\n");
    printf("Ожидаемое значение счетчика: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение:\n");
    print_file_data(DATA_FILE);
}

int main() {
    printf("===============================================================\n");
    printf("Программа для демонстрации синхронизации доступа к файлу\n");
    printf("с использованием системного вызова flock\n");
    printf("===============================================================\n");

    srand(time(NULL));

    // Удаляем старые файлы
    unlink(DATA_FILE);

    // Демонстрация race condition (без блокировок)
    demonstrate_race_condition();

    // Демонстрация с flock
    demonstrate_with_flock();

    printf("\nПрограмма завершена. Сравните результаты с синхронизацией и без нее.\n");

    // Очистка
    unlink(DATA_FILE);
    
    return 0;
}
