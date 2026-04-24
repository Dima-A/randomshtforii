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
#define SHM_SIZE 1024       // Размер разделяемой памяти

// Структура для данных в разделяемой памяти
typedef struct {
    int counter;        // Счетчик, который будет изменяться
    int pid_last;       // PID последнего процесса, изменявшего счетчик
    char message[256];  // Дополнительное поле для демонстрации
} shared_data_t;

void print_shared_data(shared_data_t *data, const char *prefix) {
    printf("%s: counter=%d, last_pid=%d, message='%s'\n", 
           prefix, data->counter, data->pid_last, data->message);
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

int main() {
    printf("===============================================================\n");
    printf("Программа для демонстрации Race Condition в System V Shared Memory\n");
    printf("===============================================================\n");
    
    // Инициализация генератора случайных чисел
    srand(time(NULL));
    
    // Демонстрация race condition (без синхронизации)
    demonstrate_race_condition();
    
    // Сложный пример
    demonstrate_complex_race_condition();

    printf("\nПрограмма завершена. Сравните результаты с синхронизацией и без нее.\n");
    
    return 0;
}
