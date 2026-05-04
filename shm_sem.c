#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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

// Демонстрация корректной работы с семафорами System V
void demonstrate_with_semaphores(key_t shm_key, key_t sem_key) {
    int shmid, semid;
    shared_data_t *shared_data;
    pid_t pid;
    int status;
    union semun sem_union;
    
    printf("\n=== Корректная работа с синхронизацией (Семафоры System V) ===\n\n");
    
    // Создаем разделяемую память
    shmid = shmget(shm_key + 2, SHM_SIZE, IPC_CREAT | 0666);
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
    semid = semget(sem_key, 1, IPC_CREAT | 0666);
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
    //shmctl(shmid, IPC_RMID, NULL);
    
    // Удаляем семафор
    //semctl(semid, 0, IPC_RMID, sem_union);
}

int main(int argc, char *argv[]) {
    printf("===============================================================\n");
    printf("Программа для демонстрации Race Condition в System V Shared Memory\n");
    printf("с использованием семафоров для синхронизации\n");
    printf("===============================================================\n");
    
    // Инициализация генератора случайных чисел
    srand(time(NULL));

    key_t shm_key;
    key_t sem_key;

    if (argc < 3) {
        fprintf(stderr, "Использование: %s <ключ> <ключ_семафора>\n", argv[0]);
        fprintf(stderr, "Пример: %s 0x1234 0x5678\n", argv[0]);
        fprintf(stderr, "       %s 4660 0x5678\n", argv[0]);
        return 1;
    }

    // Поддержка как десятичного, так и 0x... формата
    if (strncmp(argv[1], "0x", 2) == 0 || strncmp(argv[1], "0X", 2) == 0) {
        shm_key = (key_t)strtoul(argv[1], NULL, 16);
    } else {
        shm_key = (key_t)strtoul(argv[1], NULL, 10);
    }
    
    if (strncmp(argv[2], "0x", 2) == 0 || strncmp(argv[2], "0X", 2) == 0) {
        sem_key = (key_t)strtoul(argv[2], NULL, 16);
    } else {
        sem_key = (key_t)strtoul(argv[2], NULL, 10);
    }

    if (shm_key == 0) {
        fprintf(stderr, "Ошибка: некорректный ключ\n");
        return 1;
    }

    if (sem_key == 0) {
        fprintf(stderr, "Ошибка: некорректный ключ\n");
        return 1;
    }

    printf("Используется ключ shared memory: 0x%08X (%u)\n", (unsigned int)shm_key, (unsigned int)shm_key);
    printf("Используется ключ семафор: 0x%08X (%u)\n", (unsigned int)sem_key, (unsigned int)sem_key);

    // Корректная работа с семафорами
    demonstrate_with_semaphores(shm_key, sem_key);
        
    return 0;
}
