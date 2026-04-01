#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define SHM_KEY 0x1234      // Ключ для разделяемой памяти
#define SEM_KEY 0x5678      // Ключ для семафора
#define SHM_SIZE 1024       // Размер разделяемой памяти

// Структура для данных в разделяемой памяти
typedef struct {
    int counter;        // Счетчик, который будет изменяться
    int pid_last;       // PID последнего процесса, изменявшего счетчик
    char message[256];  // Дополнительное поле для демонстрации
} shared_data_t;

// Объединение для управления семафором
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void print_shared_data(shared_data_t *data, const char *prefix) {
    printf("%s: counter=%d, last_pid=%d, message='%s'\n", 
           prefix, data->counter, data->pid_last, data->message);
}

// Функция для инициализации семафора
int init_semaphore(int semid, int value) {
    union semun arg;
    arg.val = value;
    return semctl(semid, 0, SETVAL, arg);
}

// Функция для выполнения P (wait) операции над семафором
void sem_wait_op(int semid) {
    struct sembuf sb;
    sb.sem_num = 0;      // Номер семафора
    sb.sem_op = -1;      // P операция (уменьшение)
    sb.sem_flg = 0;      // Флаги
    
    if (semop(semid, &sb, 1) == -1) {
        perror("semop P failed");
        exit(1);
    }
}

// Функция для выполнения V (signal) операции над семафором
void sem_signal_op(int semid) {
    struct sembuf sb;
    sb.sem_num = 0;      // Номер семафора
    sb.sem_op = 1;       // V операция (увеличение)
    sb.sem_flg = 0;      // Флаги
    
    if (semop(semid, &sb, 1) == -1) {
        perror("semop V failed");
        exit(1);
    }
}

// Функция для демонстрации race condition (без синхронизации)
void demonstrate_race_condition() {
    int shmid;
    shared_data_t *shared_data;
    pid_t pid;
    int status;
    
    printf("\n=== Демонстрация Race Condition с System V Shared Memory ===\n\n");
    
    // Создаем разделяемую память
    shmid = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget failed");
        exit(1);
    }
    
    // Подключаем разделяемую память
    shared_data = (shared_data_t *)shmat(shmid, NULL, 0);
    if (shared_data == (shared_data_t *)-1) {
        perror("shmat failed");
        exit(1);
    }
    
    // Инициализируем данные
    shared_data->counter = 0;
    shared_data->pid_last = 0;
    strcpy(shared_data->message, "Initial message");
    
    printf("Начальное состояние:\n");
    print_shared_data(shared_data, "Parent");
    
    printf("\nЗапускаем 5 процессов для одновременного увеличения счетчика...\n");
    
    // Создаем несколько процессов, которые будут одновременно изменять данные
    for (int i = 0; i < 5; i++) {
        pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }
        
        if (pid == 0) {  // Дочерний процесс
            // Каждый процесс пытается увеличить счетчик несколько раз
            for (int j = 0; j < 3; j++) {
                int current = shared_data->counter;
                
                // Имитация длительной операции
                usleep(rand() % 100000);
                
                // Проблема race condition: между чтением и записью
                // другой процесс может изменить значение
                shared_data->counter = current + 1;
                shared_data->pid_last = getpid();
                
                // Демонстрация проблемы с обновлением строки
                snprintf(shared_data->message, sizeof(shared_data->message), 
                        "Updated by PID %d, iteration %d", getpid(), j);
                
                printf("Процесс %d: обновил счетчик (было %d, стало %d)\n", 
                       getpid(), current, shared_data->counter);
            }
            
            exit(0);
        }
    }
    
    // Ждем завершения всех дочерних процессов
    while (wait(&status) > 0);
    
    printf("\n=== РЕЗУЛЬТАТ (Race Condition) ===\n");
    printf("Ожидаемое значение счетчика: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение счетчика: %d\n", shared_data->counter);
    print_shared_data(shared_data, "Итоговое состояние");
    
    // Очищаем разделяемую память
    shmdt(shared_data);
    shmctl(shmid, IPC_RMID, NULL);
}

// Функция для демонстрации более сложного race condition
void demonstrate_complex_race_condition() {
    int shmid;
    shared_data_t *shared_data;
    pid_t pid;
    int status;
    
    printf("\n=== Сложный пример Race Condition (параллельное обновление) ===\n\n");
    
    // Создаем разделяемую память
    shmid = shmget(SHM_KEY + 1, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget failed");
        exit(1);
    }
    
    // Подключаем разделяемую память
    shared_data = (shared_data_t *)shmat(shmid, NULL, 0);
    if (shared_data == (shared_data_t *)-1) {
        perror("shmat failed");
        exit(1);
    }
    
    // Инициализируем данные
    shared_data->counter = 100;
    shared_data->pid_last = 0;
    strcpy(shared_data->message, "Complex test");
    
    printf("Начальное состояние:\n");
    print_shared_data(shared_data, "Parent");
    
    printf("\nЗапускаем процессы для одновременного чтения и записи...\n");
    
    // Создаем процессы для демонстрации race condition
    for (int i = 0; i < 3; i++) {
        pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }
        
        if (pid == 0) {  // Дочерний процесс
            if (i == 0) {
                // Процесс 1: читает и изменяет данные
                for (int j = 0; j < 5; j++) {
                    int temp = shared_data->counter;
                    usleep(10000);
                    shared_data->counter = temp + 10;
                    shared_data->pid_last = getpid();
                    printf("Процесс-писатель %d: counter=%d\n", getpid(), shared_data->counter);
                }
            } else {
                // Процессы 2 и 3: читают данные и выводят
                for (int j = 0; j < 5; j++) {
                    int val = shared_data->counter;
                    usleep(5000);
                    printf("Процесс-читатель %d: прочитал counter=%d\n", getpid(), val);
                }
            }
            exit(0);
        }
    }
    
    // Ждем завершения всех дочерних процессов
    while (wait(&status) > 0);
    
    printf("\n=== Финальное состояние ===\n");
    print_shared_data(shared_data, "Parent");
    
    // Очищаем разделяемую память
    shmdt(shared_data);
    shmctl(shmid, IPC_RMID, NULL);
}

// Демонстрация корректной работы с семафорами System V
void demonstrate_with_semaphores() {
    int shmid, semid;
    shared_data_t *shared_data;
    pid_t pid;
    int status;
    union semun sem_union;
    
    printf("\n=== Корректная работа с синхронизацией (Семафоры System V) ===\n\n");
    
    // Создаем разделяемую память
    shmid = shmget(SHM_KEY + 2, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget failed");
        exit(1);
    }
    
    // Подключаем разделяемую память
    shared_data = (shared_data_t *)shmat(shmid, NULL, 0);
    if (shared_data == (shared_data_t *)-1) {
        perror("shmat failed");
        exit(1);
    }
    
    // Создаем семафор
    semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (semid < 0) {
        perror("semget failed");
        exit(1);
    }
    
    // Инициализируем семафор значением 1 (бинарный семафор/мьютекс)
    if (init_semaphore(semid, 1) == -1) {
        perror("semctl init failed");
        exit(1);
    }
    
    // Инициализируем данные
    shared_data->counter = 0;
    shared_data->pid_last = 0;
    strcpy(shared_data->message, "Synchronized with Semaphore");
    
    printf("Начальное состояние:\n");
    print_shared_data(shared_data, "Parent");
    
    printf("\nЗапускаем 5 процессов с синхронизацией через семафор...\n");
    
    for (int i = 0; i < 5; i++) {
        pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }
        
        if (pid == 0) {  // Дочерний процесс
            for (int j = 0; j < 3; j++) {
                // Вход в критическую секцию - P операция (wait)
                sem_wait_op(semid);
                
                // КРИТИЧЕСКАЯ СЕКЦИЯ - начало
                int current = shared_data->counter;
                
                // Имитация длительной операции внутри критической секции
                usleep(rand() % 50000);
                
                // Обновляем данные
                shared_data->counter = current + 1;
                shared_data->pid_last = getpid();
                
                // Обновляем сообщение
                snprintf(shared_data->message, sizeof(shared_data->message), 
                        "Updated by PID %d (sem)", getpid());
                
                printf("Процесс %d: обновил счетчик (было %d, стало %d) [с семафором]\n", 
                       getpid(), current, shared_data->counter);
                
                // Дополнительная задержка внутри критической секции
                usleep(10000);
                // КРИТИЧЕСКАЯ СЕКЦИЯ - конец
                
                // Выход из критической секции - V операция (signal)
                sem_signal_op(semid);
                
                // Работа вне критической секции
                usleep(rand() % 20000);
            }
            
            exit(0);
        }
    }
    
    // Ждем завершения всех дочерних процессов
    while (wait(&status) > 0);
    
    printf("\n=== РЕЗУЛЬТАТ С СИНХРОНИЗАЦИЕЙ (СЕМАФОРЫ) ===\n");
    printf("Ожидаемое значение счетчика: 15 (5 процессов * 3 итерации)\n");
    printf("Фактическое значение счетчика: %d\n", shared_data->counter);
    print_shared_data(shared_data, "Итоговое состояние");
    
    // Очищаем ресурсы
    shmdt(shared_data);
    shmctl(shmid, IPC_RMID, NULL);
    
    // Удаляем семафор
    semctl(semid, 0, IPC_RMID, sem_union);
}

int main() {
    printf("===============================================================\n");
    printf("Программа для демонстрации Race Condition в System V Shared Memory\n");
    printf("с использованием семафоров для синхронизации\n");
    printf("===============================================================\n");
    
    // Инициализация генератора случайных чисел
    srand(time(NULL));
    
    // Демонстрация race condition (без синхронизации)
    demonstrate_race_condition();
    
    // Сложный пример
    demonstrate_complex_race_condition();
    
    // Корректная работа с семафорами
    demonstrate_with_semaphores();
    
    printf("\nПрограмма завершена. Сравните результаты с синхронизацией и без нее.\n");
    
    return 0;
}
