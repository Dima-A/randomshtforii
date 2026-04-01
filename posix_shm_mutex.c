#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>

#define SHM_NAME "/posix_shm_demo"
#define SHM_SIZE 1024

// Структура для данных в разделяемой памяти
typedef struct {
    pthread_mutex_t mutex;  // Мьютекс для синхронизации
    int counter;            // Счетчик
    int last_pid;           // PID последнего процесса
    char message[256];      // Сообщение
} shared_data_t;

void print_shared_data(shared_data_t *data, const char *prefix) {
    printf("%s: counter=%d, last_pid=%d, message='%s'\n",
           prefix, data->counter, data->last_pid, data->message);
}

void error_exit(const char *msg) {
    perror(msg);
    exit(1);
}

// Инициализация process-shared мьютекса
void init_process_shared_mutex(pthread_mutex_t *mutex) {
    pthread_mutexattr_t attr;
    
    if (pthread_mutexattr_init(&attr) != 0)
        error_exit("pthread_mutexattr_init failed");
    
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0)
        error_exit("pthread_mutexattr_setpshared failed");
    
    if (pthread_mutex_init(mutex, &attr) != 0)
        error_exit("pthread_mutex_init failed");
    
    pthread_mutexattr_destroy(&attr);
}

// Демонстрация race condition (без синхронизации)
void demonstrate_race_condition() {
    int fd;
    shared_data_t *shared;
    pid_t pid;
    int status;

    printf("\n=== Демонстрация Race Condition с POSIX Shared Memory ===\n\n");

    // Создаем разделяемую память
    fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) error_exit("shm_open failed");

    // Устанавливаем размер
    if (ftruncate(fd, sizeof(shared_data_t)) == -1)
        error_exit("ftruncate failed");

    // Подключаем разделяемую память
    shared = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) error_exit("mmap failed");

    close(fd);

    // Инициализируем данные (мьютекс не используется в этом тесте!)
    shared->counter = 0;
    shared->last_pid = 0;
    strcpy(shared->message, "Начальное сообщение");

    printf("Начальное состояние:\n");
    print_shared_data(shared, "Родитель");

    printf("\nЗапускаем 5 процессов для одновременного увеличения счетчика...\n");

    // Создаем несколько процессов
    for (int i = 0; i < 5; i++) {
        pid = fork();

        if (pid < 0) {
            error_exit("fork failed");
        }

        if (pid == 0) {  // Дочерний процесс
            for (int j = 0; j < 3; j++) {
                int current = shared->counter;

                // Имитация длительной операции
                usleep(rand() % 100000);

                // Race condition: между чтением и записью
                // другой процесс может изменить значение
                shared->counter = current + 1;
                shared->last_pid = getpid();

                snprintf(shared->message, sizeof(shared->message),
                        "Обновлен PID %d, итерация %d", getpid(), j);

                printf("Процесс %d: обновил счетчик (было %d, стало %d)\n",
                       getpid(), current, shared->counter);
            }
            exit(0);
        }
    }

    // Ждем завершения всех дочерних процессов
    while (wait(&status) > 0);

    printf("\n=== РЕЗУЛЬТАТ (Race Condition) ===\n");
    printf("Ожидаемое значение счетчика: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение счетчика: %d\n", shared->counter);
    print_shared_data(shared, "Итоговое состояние");

    // Очищаем
    munmap(shared, sizeof(shared_data_t));
    shm_unlink(SHM_NAME);
}

// Демонстрация корректной работы с мьютексами
void demonstrate_with_mutex() {
    int fd;
    shared_data_t *shared;
    pid_t pid;
    int status;

    printf("\n=== Корректная работа с синхронизацией (Мьютексы POSIX) ===\n\n");

    // Создаем разделяемую память
    fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) error_exit("shm_open failed");

    // Устанавливаем размер
    if (ftruncate(fd, sizeof(shared_data_t)) == -1)
        error_exit("ftruncate failed");

    // Подключаем разделяемую память
    shared = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) error_exit("mmap failed");

    close(fd);

    // Инициализируем process-shared мьютекс
    init_process_shared_mutex(&shared->mutex);

    // Инициализируем данные
    shared->counter = 0;
    shared->last_pid = 0;
    strcpy(shared->message, "Синхронизировано мьютексом");

    printf("Начальное состояние:\n");
    print_shared_data(shared, "Родитель");

    printf("\nЗапускаем 5 процессов с синхронизацией через мьютекс...\n");

    for (int i = 0; i < 5; i++) {
        pid = fork();

        if (pid < 0) {
            error_exit("fork failed");
        }

        if (pid == 0) {  // Дочерний процесс
            for (int j = 0; j < 3; j++) {
                // Вход в критическую секцию - блокируем мьютекс
                if (pthread_mutex_lock(&shared->mutex) != 0)
                    error_exit("pthread_mutex_lock failed");

                // КРИТИЧЕСКАЯ СЕКЦИЯ - начало
                int current = shared->counter;

                // Имитация длительной операции внутри критической секции
                usleep(rand() % 50000);

                // Обновляем данные
                shared->counter = current + 1;
                shared->last_pid = getpid();

                snprintf(shared->message, sizeof(shared->message),
                        "Обновлен PID %d (с мьютексом)", getpid());

                printf("Процесс %d: обновил счетчик (было %d, стало %d) [с мьютексом]\n",
                       getpid(), current, shared->counter);

                // Дополнительная задержка внутри критической секции
                usleep(10000);
                // КРИТИЧЕСКАЯ СЕКЦИЯ - конец

                // Выход из критической секции - разблокируем мьютекс
                if (pthread_mutex_unlock(&shared->mutex) != 0)
                    error_exit("pthread_mutex_unlock failed");

                // Работа вне критической секции
                usleep(rand() % 20000);
            }
            exit(0);
        }
    }

    // Ждем завершения всех дочерних процессов
    while (wait(&status) > 0);

    printf("\n=== РЕЗУЛЬТАТ С СИНХРОНИЗАЦИЕЙ (МЬЮТЕКСЫ) ===\n");
    printf("Ожидаемое значение счетчика: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение счетчика: %d\n", shared->counter);
    print_shared_data(shared, "Итоговое состояние");

    // Очищаем ресурсы
    pthread_mutex_destroy(&shared->mutex);
    munmap(shared, sizeof(shared_data_t));
    shm_unlink(SHM_NAME);
}

// Демонстрация с trylock (неблокирующая попытка)
void demonstrate_with_trylock() {
    int fd;
    shared_data_t *shared;
    pid_t pid;
    int status;

    printf("\n=== Демонстрация pthread_mutex_trylock ===\n\n");

    // Создаем разделяемую память
    fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) error_exit("shm_open failed");

    // Устанавливаем размер
    if (ftruncate(fd, sizeof(shared_data_t)) == -1)
        error_exit("ftruncate failed");

    // Подключаем разделяемую память
    shared = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) error_exit("mmap failed");

    close(fd);

    // Инициализируем process-shared мьютекс
    init_process_shared_mutex(&shared->mutex);

    shared->counter = 0;
    shared->last_pid = 0;
    strcpy(shared->message, "Демонстрация trylock");

    printf("Запускаем процесс, который пытается захватить мьютекс...\n");

    pid = fork();
    if (pid == 0) {
        // Дочерний процесс - захватывает мьютекс и держит его
        printf("Дочерний процесс %d: захватываю мьютекс и сплю 3 секунды\n", getpid());
        pthread_mutex_lock(&shared->mutex);
        sleep(3);
        pthread_mutex_unlock(&shared->mutex);
        printf("Дочерний процесс %d: отпустил мьютекс\n", getpid());
        exit(0);
    }

    sleep(1); // Даем дочернему процессу захватить мьютекс

    // Родитель пытается захватить мьютекс с trylock
    printf("Родитель: пробую захватить мьютекс (trylock)...\n");
    int ret = pthread_mutex_trylock(&shared->mutex);
    
    if (ret == EBUSY) {
        printf("Родитель: мьютекс занят, продолжаю работу (EBUSY)\n");
    } else if (ret == 0) {
        printf("Родитель: захватил мьютекс\n");
        pthread_mutex_unlock(&shared->mutex);
    } else {
        printf("Родитель: ошибка %d\n", ret);
    }

    wait(&status);
    printf("Демонстрация trylock завершена\n");

    pthread_mutex_destroy(&shared->mutex);
    munmap(shared, sizeof(shared_data_t));
    shm_unlink(SHM_NAME);
}

int main() {
    printf("===============================================================\n");
    printf("Программа для демонстрации Race Condition в POSIX Shared Memory\n");
    printf("с использованием мьютексов для синхронизации\n");
    printf("===============================================================\n");

    // Инициализация генератора случайных чисел
    srand(time(NULL));

    // Удаляем старую разделяемую память если есть
    shm_unlink(SHM_NAME);

    // Демонстрация race condition (без синхронизации)
    demonstrate_race_condition();

    // Демонстрация с trylock
    demonstrate_with_trylock();

    // Корректная работа с мьютексами
    demonstrate_with_mutex();

    printf("\nПрограмма завершена. Сравните результаты с синхронизацией и без нее.\n");

    return 0;
}
